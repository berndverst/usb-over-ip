package com.vusb.client.service

import android.app.*
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.hardware.usb.UsbDevice
import android.os.Binder
import android.os.Build
import android.os.IBinder
import android.os.PowerManager
import android.util.Log
import androidx.core.app.NotificationCompat
import androidx.lifecycle.LifecycleService
import androidx.lifecycle.lifecycleScope
import com.vusb.client.R
import com.vusb.client.network.VusbNetworkClient
import com.vusb.client.protocol.*
import com.vusb.client.ui.MainActivity
import com.vusb.client.usb.UrbHandler
import com.vusb.client.usb.UsbDeviceManager
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import java.util.concurrent.ConcurrentHashMap

/**
 * Foreground service for USB device forwarding
 * 
 * This service:
 * - Runs in the foreground to maintain persistent operation
 * - Manages USB device connections
 * - Handles network communication with VUSB server
 * - Processes URB requests and responses
 */
class UsbForwardingService : LifecycleService() {
    
    companion object {
        private const val TAG = "UsbForwardingService"
        private const val NOTIFICATION_ID = 1001
        private const val CHANNEL_ID = "vusb_service_channel"
        
        // Intent actions
        const val ACTION_START = "com.vusb.client.START_FORWARDING"
        const val ACTION_STOP = "com.vusb.client.STOP_FORWARDING"
        const val ACTION_ATTACH_DEVICE = "com.vusb.client.ATTACH_DEVICE"
        const val ACTION_DETACH_DEVICE = "com.vusb.client.DETACH_DEVICE"
        
        // Intent extras
        const val EXTRA_SERVER_ADDRESS = "server_address"
        const val EXTRA_SERVER_PORT = "server_port"
        const val EXTRA_DEVICE_ID = "device_id"
        const val EXTRA_AUTO_ATTACH = "auto_attach"
    }
    
    // Service state
    sealed class ServiceState {
        data object Stopped : ServiceState()
        data object Starting : ServiceState()
        data class Running(val serverAddress: String, val attachedDevices: Int) : ServiceState()
        data class Error(val message: String) : ServiceState()
    }
    
    private val binder = LocalBinder()
    
    // Components
    private lateinit var usbManager: UsbDeviceManager
    private lateinit var networkClient: VusbNetworkClient
    private lateinit var urbHandler: UrbHandler
    
    // Attached devices (deviceId -> DeviceInfo)
    private val attachedDevices = ConcurrentHashMap<Int, DeviceInfo>()
    
    // State
    private val _serviceState = MutableStateFlow<ServiceState>(ServiceState.Stopped)
    val serviceState: StateFlow<ServiceState> = _serviceState.asStateFlow()
    
    private var serverAddress: String = ""
    private var serverPort: Int = VusbProtocol.DEFAULT_PORT
    private var autoAttach: Boolean = false
    
    private var wakeLock: PowerManager.WakeLock? = null
    
    inner class LocalBinder : Binder() {
        fun getService(): UsbForwardingService = this@UsbForwardingService
    }
    
    override fun onCreate() {
        super.onCreate()
        Log.d(TAG, "Service created")
        
        // Initialize components
        usbManager = UsbDeviceManager(this)
        networkClient = VusbNetworkClient()
        urbHandler = UrbHandler(usbManager)
        
        usbManager.initialize()
        
        // Setup network message handler
        networkClient.onMessageReceived = { header, payload ->
            handleServerMessage(header, payload)
        }
        
        networkClient.onDisconnected = {
            handleDisconnection()
        }
        
        createNotificationChannel()
    }
    
    override fun onDestroy() {
        Log.d(TAG, "Service destroyed")
        stopForwarding()
        usbManager.cleanup()
        super.onDestroy()
    }
    
    override fun onBind(intent: Intent): IBinder {
        super.onBind(intent)
        return binder
    }
    
    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        super.onStartCommand(intent, flags, startId)
        
        when (intent?.action) {
            ACTION_START -> {
                serverAddress = intent.getStringExtra(EXTRA_SERVER_ADDRESS) ?: ""
                serverPort = intent.getIntExtra(EXTRA_SERVER_PORT, VusbProtocol.DEFAULT_PORT)
                autoAttach = intent.getBooleanExtra(EXTRA_AUTO_ATTACH, false)
                
                if (serverAddress.isNotEmpty()) {
                    startForwarding()
                }
            }
            ACTION_STOP -> {
                stopForwarding()
                stopSelf()
            }
            ACTION_ATTACH_DEVICE -> {
                val deviceId = intent.getIntExtra(EXTRA_DEVICE_ID, -1)
                if (deviceId >= 0) {
                    lifecycleScope.launch {
                        attachDevice(deviceId)
                    }
                }
            }
            ACTION_DETACH_DEVICE -> {
                val deviceId = intent.getIntExtra(EXTRA_DEVICE_ID, -1)
                if (deviceId >= 0) {
                    lifecycleScope.launch {
                        detachDevice(deviceId)
                    }
                }
            }
        }
        
        return START_STICKY
    }
    
    /**
     * Start the forwarding service
     */
    private fun startForwarding() {
        if (_serviceState.value is ServiceState.Running) {
            Log.w(TAG, "Already running")
            return
        }
        
        Log.d(TAG, "Starting forwarding to $serverAddress:$serverPort")
        _serviceState.value = ServiceState.Starting
        
        // Start foreground
        startForegroundService()
        
        // Acquire wake lock
        acquireWakeLock()
        
        // Connect to server
        lifecycleScope.launch(Dispatchers.IO) {
            try {
                val connected = networkClient.connect(serverAddress, serverPort, "AndroidVUSB")
                
                if (connected) {
                    _serviceState.value = ServiceState.Running(serverAddress, 0)
                    
                    // Auto-attach devices if enabled
                    if (autoAttach) {
                        delay(500) // Wait for connection to stabilize
                        autoAttachDevices()
                    }
                } else {
                    _serviceState.value = ServiceState.Error("Connection failed")
                    stopForeground(STOP_FOREGROUND_REMOVE)
                }
            } catch (e: Exception) {
                Log.e(TAG, "Failed to start forwarding", e)
                _serviceState.value = ServiceState.Error(e.message ?: "Unknown error")
                stopForeground(STOP_FOREGROUND_REMOVE)
            }
        }
    }
    
    /**
     * Stop the forwarding service
     */
    fun stopForwarding() {
        Log.d(TAG, "Stopping forwarding")
        
        // Detach all devices
        lifecycleScope.launch {
            attachedDevices.keys.forEach { deviceId ->
                try {
                    detachDevice(deviceId)
                } catch (e: Exception) {
                    Log.w(TAG, "Error detaching device $deviceId", e)
                }
            }
        }
        
        networkClient.disconnect()
        releaseWakeLock()
        
        _serviceState.value = ServiceState.Stopped
    }
    
    /**
     * Attach a USB device
     */
    suspend fun attachDevice(deviceId: Int): Boolean {
        val device = usbManager.getDevice(deviceId)
        if (device == null) {
            Log.e(TAG, "Device not found: $deviceId")
            return false
        }
        
        return attachDevice(device)
    }
    
    /**
     * Attach a USB device
     */
    suspend fun attachDevice(device: UsbDevice): Boolean = withContext(Dispatchers.IO) {
        try {
            // Check permission
            if (!usbManager.hasPermission(device)) {
                Log.w(TAG, "No permission for device ${device.deviceName}, requesting...")
                usbManager.requestPermission(device)
                return@withContext false
            }
            
            // Open device
            if (!usbManager.isDeviceOpen(device.deviceId)) {
                if (!usbManager.openDevice(device)) {
                    Log.e(TAG, "Failed to open device ${device.deviceName}")
                    return@withContext false
                }
            }
            
            // Get device info
            val deviceInfo = usbManager.getDeviceInfo(device)
            
            // Send attach to server
            networkClient.attachDevice(deviceInfo)
            
            // Track attached device
            attachedDevices[device.deviceId] = deviceInfo
            updateServiceState()
            
            Log.d(TAG, "Device attached: ${device.deviceName}")
            true
        } catch (e: Exception) {
            Log.e(TAG, "Failed to attach device", e)
            false
        }
    }
    
    /**
     * Detach a USB device
     */
    suspend fun detachDevice(deviceId: Int): Boolean = withContext(Dispatchers.IO) {
        try {
            if (!attachedDevices.containsKey(deviceId)) {
                Log.w(TAG, "Device not attached: $deviceId")
                return@withContext false
            }
            
            // Send detach to server
            networkClient.detachDevice(deviceId)
            
            // Close device
            usbManager.closeDevice(deviceId)
            
            // Remove from tracking
            attachedDevices.remove(deviceId)
            updateServiceState()
            
            Log.d(TAG, "Device detached: $deviceId")
            true
        } catch (e: Exception) {
            Log.e(TAG, "Failed to detach device", e)
            false
        }
    }
    
    /**
     * Auto-attach all available USB devices
     */
    private suspend fun autoAttachDevices() {
        val devices = usbManager.getDevices()
        Log.d(TAG, "Auto-attaching ${devices.size} devices")
        
        for (device in devices) {
            if (usbManager.hasPermission(device)) {
                attachDevice(device)
            } else {
                Log.d(TAG, "Requesting permission for ${device.deviceName}")
                usbManager.requestPermission(device)
            }
        }
    }
    
    /**
     * Handle messages from server
     */
    private fun handleServerMessage(header: VusbHeader, payload: ByteArray) {
        lifecycleScope.launch(Dispatchers.IO) {
            when (header.command) {
                VusbProtocol.Command.URB_SUBMIT -> {
                    handleUrbSubmit(payload)
                }
                VusbProtocol.Command.RESET -> {
                    handleReset(payload)
                }
                VusbProtocol.Command.ERROR -> {
                    handleError(payload)
                }
            }
        }
    }
    
    /**
     * Handle URB submit from server
     */
    private suspend fun handleUrbSubmit(payload: ByteArray) {
        try {
            val urbSubmit = UrbSubmit.fromByteArray(payload)
            
            // Verify device is attached
            if (!attachedDevices.containsKey(urbSubmit.deviceId)) {
                Log.w(TAG, "URB for unknown device: ${urbSubmit.deviceId}")
                val complete = UrbComplete(
                    urbId = urbSubmit.urbId,
                    status = VusbProtocol.UsbdStatus.ERROR_BUSY,
                    actualLength = 0
                )
                networkClient.sendUrbComplete(complete)
                return
            }
            
            // Process URB
            val complete = urbHandler.processUrb(urbSubmit)
            
            // Send completion
            networkClient.sendUrbComplete(complete)
            
        } catch (e: Exception) {
            Log.e(TAG, "Error processing URB", e)
        }
    }
    
    /**
     * Handle device reset request
     */
    private suspend fun handleReset(payload: ByteArray) {
        if (payload.size < 4) return
        
        val deviceId = (payload[0].toInt() and 0xFF) or
                       ((payload[1].toInt() and 0xFF) shl 8) or
                       ((payload[2].toInt() and 0xFF) shl 16) or
                       ((payload[3].toInt() and 0xFF) shl 24)
        
        Log.d(TAG, "Reset request for device: $deviceId")
        
        // Re-open device
        val device = usbManager.getDevice(deviceId)
        if (device != null) {
            usbManager.closeDevice(deviceId)
            delay(100)
            usbManager.openDevice(device)
        }
    }
    
    /**
     * Handle error from server
     */
    private fun handleError(payload: ByteArray) {
        val message = payload.toString(Charsets.UTF_8)
        Log.e(TAG, "Server error: $message")
    }
    
    /**
     * Handle disconnection
     */
    private fun handleDisconnection() {
        Log.d(TAG, "Disconnected from server")
        attachedDevices.clear()
        _serviceState.value = ServiceState.Stopped
        stopForeground(STOP_FOREGROUND_REMOVE)
    }
    
    /**
     * Update service state
     */
    private fun updateServiceState() {
        if (_serviceState.value is ServiceState.Running) {
            _serviceState.value = ServiceState.Running(serverAddress, attachedDevices.size)
            updateNotification()
        }
    }
    
    /**
     * Get list of attached devices
     */
    fun getAttachedDevices(): List<DeviceInfo> {
        return attachedDevices.values.toList()
    }
    
    /**
     * Get USB device manager
     */
    fun getUsbManager(): UsbDeviceManager = usbManager
    
    /**
     * Create notification channel
     */
    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val channel = NotificationChannel(
                CHANNEL_ID,
                "USB Forwarding Service",
                NotificationManager.IMPORTANCE_LOW
            ).apply {
                description = "Shows when USB devices are being forwarded"
                setShowBadge(false)
            }
            
            val notificationManager = getSystemService(NotificationManager::class.java)
            notificationManager.createNotificationChannel(channel)
        }
    }
    
    /**
     * Start as foreground service
     */
    private fun startForegroundService() {
        val notification = buildNotification()
        
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            startForeground(
                NOTIFICATION_ID, 
                notification,
                ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE
            )
        } else {
            startForeground(NOTIFICATION_ID, notification)
        }
    }
    
    /**
     * Build notification
     */
    private fun buildNotification(): Notification {
        val pendingIntent = PendingIntent.getActivity(
            this,
            0,
            Intent(this, MainActivity::class.java),
            PendingIntent.FLAG_IMMUTABLE
        )
        
        val stopIntent = PendingIntent.getService(
            this,
            0,
            Intent(this, UsbForwardingService::class.java).apply {
                action = ACTION_STOP
            },
            PendingIntent.FLAG_IMMUTABLE
        )
        
        val deviceCount = attachedDevices.size
        val contentText = when {
            serverAddress.isEmpty() -> "Initializing..."
            deviceCount == 0 -> "Connected to $serverAddress (no devices)"
            deviceCount == 1 -> "Forwarding 1 device to $serverAddress"
            else -> "Forwarding $deviceCount devices to $serverAddress"
        }
        
        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle("VUSB Client")
            .setContentText(contentText)
            .setSmallIcon(R.drawable.ic_usb)
            .setOngoing(true)
            .setContentIntent(pendingIntent)
            .addAction(R.drawable.ic_stop, "Stop", stopIntent)
            .build()
    }
    
    /**
     * Update notification
     */
    private fun updateNotification() {
        val notification = buildNotification()
        val notificationManager = getSystemService(NotificationManager::class.java)
        notificationManager.notify(NOTIFICATION_ID, notification)
    }
    
    /**
     * Acquire wake lock
     */
    private fun acquireWakeLock() {
        val powerManager = getSystemService(Context.POWER_SERVICE) as PowerManager
        wakeLock = powerManager.newWakeLock(
            PowerManager.PARTIAL_WAKE_LOCK,
            "VUSBClient::ForwardingWakeLock"
        ).apply {
            acquire(10 * 60 * 1000L) // 10 minutes max
        }
    }
    
    /**
     * Release wake lock
     */
    private fun releaseWakeLock() {
        wakeLock?.let {
            if (it.isHeld) {
                it.release()
            }
        }
        wakeLock = null
    }
}
