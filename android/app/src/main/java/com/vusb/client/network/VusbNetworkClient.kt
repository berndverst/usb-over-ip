package com.vusb.client.network

import android.util.Log
import com.vusb.client.protocol.*
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import java.io.DataInputStream
import java.io.DataOutputStream
import java.net.InetSocketAddress
import java.net.Socket
import java.net.SocketTimeoutException
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicInteger

/**
 * Network client for VUSB protocol
 * 
 * Handles:
 * - TCP connection to server
 * - Protocol message encoding/decoding
 * - Message sending and receiving
 * - Keep-alive mechanism
 */
class VusbNetworkClient {
    
    companion object {
        private const val TAG = "VusbNetworkClient"
        private const val CONNECT_TIMEOUT = 10000 // 10 seconds
        private const val READ_TIMEOUT = 30000 // 30 seconds
        private const val KEEPALIVE_INTERVAL = 10000L // 10 seconds
    }
    
    // Connection state
    sealed class ConnectionState {
        data object Disconnected : ConnectionState()
        data object Connecting : ConnectionState()
        data class Connected(val serverAddress: String) : ConnectionState()
        data class Error(val message: String) : ConnectionState()
    }
    
    private var socket: Socket? = null
    private var outputStream: DataOutputStream? = null
    private var inputStream: DataInputStream? = null
    
    private val sequenceNumber = AtomicInteger(0)
    private val isRunning = AtomicBoolean(false)
    
    private val _connectionState = MutableStateFlow<ConnectionState>(ConnectionState.Disconnected)
    val connectionState: StateFlow<ConnectionState> = _connectionState.asStateFlow()
    
    private var scope: CoroutineScope? = null
    private var receiveJob: Job? = null
    private var keepAliveJob: Job? = null
    
    // Message callback
    var onMessageReceived: ((VusbHeader, ByteArray) -> Unit)? = null
    var onDisconnected: (() -> Unit)? = null
    
    /**
     * Connect to the VUSB server
     */
    suspend fun connect(
        serverAddress: String, 
        port: Int = VusbProtocol.DEFAULT_PORT,
        clientName: String = "AndroidClient"
    ): Boolean = withContext(Dispatchers.IO) {
        
        if (isRunning.get()) {
            Log.w(TAG, "Already connected or connecting")
            return@withContext false
        }
        
        _connectionState.value = ConnectionState.Connecting
        
        try {
            Log.d(TAG, "Connecting to $serverAddress:$port")
            
            socket = Socket().apply {
                soTimeout = READ_TIMEOUT
                connect(InetSocketAddress(serverAddress, port), CONNECT_TIMEOUT)
            }
            
            outputStream = DataOutputStream(socket!!.getOutputStream())
            inputStream = DataInputStream(socket!!.getInputStream())
            
            isRunning.set(true)
            
            // Send connect message
            val connectMsg = ConnectMessage(clientName)
            sendMessage(VusbProtocol.Command.CONNECT, connectMsg.toByteArray())
            
            // Wait for connect response (server echoes CONNECT command with status)
            val (header, _) = receiveMessage()
            if (header.command != VusbProtocol.Command.CONNECT) {
                throw Exception("Expected CONNECT response, got ${header.command}")
            }
            
            Log.d(TAG, "Connected successfully to $serverAddress:$port")
            _connectionState.value = ConnectionState.Connected(serverAddress)
            
            // Start background tasks
            scope = CoroutineScope(Dispatchers.IO + SupervisorJob())
            startReceiveLoop()
            startKeepAlive()
            
            true
        } catch (e: Exception) {
            Log.e(TAG, "Connection failed", e)
            _connectionState.value = ConnectionState.Error(e.message ?: "Connection failed")
            disconnect()
            false
        }
    }
    
    /**
     * Disconnect from server
     */
    fun disconnect() {
        if (!isRunning.getAndSet(false)) {
            return
        }
        
        Log.d(TAG, "Disconnecting...")
        
        // Cancel background jobs
        receiveJob?.cancel()
        keepAliveJob?.cancel()
        scope?.cancel()
        
        // Send disconnect message (best effort)
        try {
            sendMessageSync(VusbProtocol.Command.DISCONNECT, ByteArray(0))
        } catch (e: Exception) {
            Log.w(TAG, "Failed to send disconnect message", e)
        }
        
        // Close streams and socket
        try {
            outputStream?.close()
            inputStream?.close()
            socket?.close()
        } catch (e: Exception) {
            Log.w(TAG, "Error closing connection", e)
        }
        
        outputStream = null
        inputStream = null
        socket = null
        
        _connectionState.value = ConnectionState.Disconnected
        onDisconnected?.invoke()
        
        Log.d(TAG, "Disconnected")
    }
    
    /**
     * Check if connected
     */
    fun isConnected(): Boolean {
        return isRunning.get() && socket?.isConnected == true && !socket!!.isClosed
    }
    
    /**
     * Send a protocol message
     */
    suspend fun sendMessage(command: Short, payload: ByteArray) = withContext(Dispatchers.IO) {
        sendMessageSync(command, payload)
    }
    
    /**
     * Send message synchronously (for use in IO context)
     */
    @Synchronized
    private fun sendMessageSync(command: Short, payload: ByteArray) {
        val output = outputStream ?: throw Exception("Not connected")
        
        val header = VusbHeader(
            command = command,
            length = payload.size,
            sequence = sequenceNumber.incrementAndGet()
        )
        
        val headerBytes = header.toByteArray()
        
        Log.v(TAG, "Sending message: cmd=0x${command.toString(16)}, len=${payload.size}, seq=${header.sequence}")
        
        output.write(headerBytes)
        if (payload.isNotEmpty()) {
            output.write(payload)
        }
        output.flush()
    }
    
    /**
     * Receive a single message
     */
    @Synchronized
    private fun receiveMessage(): Pair<VusbHeader, ByteArray> {
        val input = inputStream ?: throw Exception("Not connected")
        
        // Read header
        val headerBytes = ByteArray(VusbProtocol.HEADER_SIZE)
        input.readFully(headerBytes)
        
        val header = VusbHeader.fromByteArray(headerBytes)
        
        // Verify magic
        if (header.magic != VusbProtocol.MAGIC) {
            throw Exception("Invalid protocol magic: 0x${header.magic.toString(16)}")
        }
        
        // Read payload
        val payload = if (header.length > 0) {
            if (header.length > VusbProtocol.MAX_PAYLOAD_SIZE) {
                throw Exception("Payload too large: ${header.length}")
            }
            ByteArray(header.length).also { input.readFully(it) }
        } else {
            ByteArray(0)
        }
        
        Log.v(TAG, "Received message: cmd=0x${header.command.toString(16)}, len=${payload.size}, seq=${header.sequence}")
        
        return Pair(header, payload)
    }
    
    /**
     * Start the receive loop
     */
    private fun startReceiveLoop() {
        receiveJob = scope?.launch {
            while (isActive && isRunning.get()) {
                try {
                    val (header, payload) = receiveMessage()
                    
                    when (header.command) {
                        VusbProtocol.Command.PONG -> {
                            // Response to our ping
                            Log.v(TAG, "Received pong")
                        }
                        VusbProtocol.Command.DISCONNECT -> {
                            Log.d(TAG, "Server disconnected")
                            disconnect()
                            break
                        }
                        else -> {
                            // Dispatch to message handler
                            onMessageReceived?.invoke(header, payload)
                        }
                    }
                } catch (e: SocketTimeoutException) {
                    // Timeout is expected, continue
                    continue
                } catch (e: Exception) {
                    if (isRunning.get()) {
                        Log.e(TAG, "Receive error", e)
                        _connectionState.value = ConnectionState.Error(e.message ?: "Receive error")
                        disconnect()
                    }
                    break
                }
            }
        }
    }
    
    /**
     * Start keep-alive mechanism
     */
    private fun startKeepAlive() {
        keepAliveJob = scope?.launch {
            while (isActive && isRunning.get()) {
                delay(KEEPALIVE_INTERVAL)
                try {
                    if (isRunning.get()) {
                        sendMessage(VusbProtocol.Command.PING, ByteArray(0))
                    }
                } catch (e: Exception) {
                    Log.w(TAG, "Keepalive failed", e)
                }
            }
        }
    }
    
    /**
     * Attach a device to the server
     */
    suspend fun attachDevice(deviceInfo: DeviceInfo) {
        Log.d(TAG, "Attaching device: VID=${deviceInfo.vendorId.toString(16)}, PID=${deviceInfo.productId.toString(16)}")
        sendMessage(VusbProtocol.Command.ATTACH, deviceInfo.toByteArray())
    }
    
    /**
     * Detach a device from the server
     */
    suspend fun detachDevice(deviceId: Int) {
        Log.d(TAG, "Detaching device: $deviceId")
        val buffer = ByteBuffer.allocate(4)
        buffer.order(ByteOrder.LITTLE_ENDIAN)
        buffer.putInt(deviceId)
        sendMessage(VusbProtocol.Command.DETACH, buffer.array())
    }
    
    /**
     * Send URB completion response
     */
    suspend fun sendUrbComplete(urbComplete: UrbComplete) {
        Log.d(TAG, "Sending URB complete: id=${urbComplete.urbId}, status=${urbComplete.status}, len=${urbComplete.actualLength}")
        sendMessage(VusbProtocol.Command.URB_COMPLETE, urbComplete.toByteArray())
    }
}
