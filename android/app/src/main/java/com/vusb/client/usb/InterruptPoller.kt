package com.vusb.client.usb

import android.hardware.usb.UsbConstants
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbDeviceConnection
import android.hardware.usb.UsbEndpoint
import android.util.Log
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.asSharedFlow
import java.util.concurrent.ConcurrentHashMap

/**
 * Interrupt Endpoint Poller
 * 
 * Continuously polls interrupt IN endpoints (used by game controllers, mice, keyboards)
 * and emits the data for forwarding to the server.
 * 
 * Game controllers use interrupt endpoints to report:
 * - Button states
 * - Joystick/axis positions
 * - Trigger values
 * - D-pad directions
 */
class InterruptPoller(private val usbManager: UsbDeviceManager) {
    
    companion object {
        private const val TAG = "InterruptPoller"
        private const val DEFAULT_POLL_TIMEOUT_MS = 50 // 50ms = 20Hz minimum, actual rate depends on endpoint interval
    }
    
    /**
     * Data class representing interrupt data received from an endpoint
     */
    data class InterruptData(
        val deviceId: Int,
        val endpointAddress: Int,
        val data: ByteArray,
        val timestamp: Long = System.currentTimeMillis()
    ) {
        override fun equals(other: Any?): Boolean {
            if (this === other) return true
            if (javaClass != other?.javaClass) return false
            other as InterruptData
            return deviceId == other.deviceId && 
                   endpointAddress == other.endpointAddress &&
                   data.contentEquals(other.data)
        }
        
        override fun hashCode(): Int {
            var result = deviceId
            result = 31 * result + endpointAddress
            result = 31 * result + data.contentHashCode()
            return result
        }
    }
    
    // Active polling jobs per device
    private val pollingJobs = ConcurrentHashMap<Int, Job>()
    
    // Endpoints being polled per device
    private val polledEndpoints = ConcurrentHashMap<Int, MutableList<UsbEndpoint>>()
    
    // Flow for interrupt data events
    private val _interruptData = MutableSharedFlow<InterruptData>(
        replay = 0,
        extraBufferCapacity = 64 // Buffer some events to prevent loss
    )
    val interruptData: SharedFlow<InterruptData> = _interruptData.asSharedFlow()
    
    // Coroutine scope for polling
    private val pollingScope = CoroutineScope(Dispatchers.IO + SupervisorJob())
    
    /**
     * Start polling interrupt endpoints for a device
     * 
     * @param device The USB device to poll
     * @return Number of interrupt IN endpoints found and being polled
     */
    fun startPolling(device: UsbDevice): Int {
        if (pollingJobs.containsKey(device.deviceId)) {
            Log.w(TAG, "Already polling device: ${device.deviceId}")
            return polledEndpoints[device.deviceId]?.size ?: 0
        }
        
        // Find all interrupt IN endpoints
        val interruptEndpoints = findInterruptInEndpoints(device)
        
        if (interruptEndpoints.isEmpty()) {
            Log.d(TAG, "No interrupt IN endpoints found for device: ${device.deviceId}")
            return 0
        }
        
        Log.d(TAG, "Found ${interruptEndpoints.size} interrupt IN endpoint(s) for device ${device.deviceId}")
        interruptEndpoints.forEach { ep ->
            Log.d(TAG, "  - Endpoint 0x${ep.address.toString(16)}, interval=${ep.interval}ms, maxPacket=${ep.maxPacketSize}")
        }
        
        polledEndpoints[device.deviceId] = interruptEndpoints.toMutableList()
        
        // Start polling job
        val job = pollingScope.launch {
            pollDevice(device, interruptEndpoints)
        }
        pollingJobs[device.deviceId] = job
        
        return interruptEndpoints.size
    }
    
    /**
     * Stop polling for a device
     */
    fun stopPolling(deviceId: Int) {
        pollingJobs.remove(deviceId)?.cancel()
        polledEndpoints.remove(deviceId)
        Log.d(TAG, "Stopped polling device: $deviceId")
    }
    
    /**
     * Stop all polling
     */
    fun stopAll() {
        pollingJobs.keys.forEach { stopPolling(it) }
        pollingScope.cancel()
    }
    
    /**
     * Check if a device is being polled
     */
    fun isPolling(deviceId: Int): Boolean {
        return pollingJobs.containsKey(deviceId) && pollingJobs[deviceId]?.isActive == true
    }
    
    /**
     * Get list of polled endpoints for a device
     */
    fun getPolledEndpoints(deviceId: Int): List<UsbEndpoint> {
        return polledEndpoints[deviceId]?.toList() ?: emptyList()
    }
    
    /**
     * Find all interrupt IN endpoints on a device
     */
    private fun findInterruptInEndpoints(device: UsbDevice): List<UsbEndpoint> {
        val endpoints = mutableListOf<UsbEndpoint>()
        
        for (i in 0 until device.interfaceCount) {
            val intf = device.getInterface(i)
            
            for (j in 0 until intf.endpointCount) {
                val endpoint = intf.getEndpoint(j)
                
                // Check for interrupt IN endpoint
                if (endpoint.type == UsbConstants.USB_ENDPOINT_XFER_INT &&
                    endpoint.direction == UsbConstants.USB_DIR_IN) {
                    endpoints.add(endpoint)
                }
            }
        }
        
        return endpoints
    }
    
    /**
     * Main polling loop for a device
     */
    private suspend fun pollDevice(device: UsbDevice, endpoints: List<UsbEndpoint>) {
        val connection = usbManager.getConnection(device.deviceId)
        if (connection == null) {
            Log.e(TAG, "No connection for device: ${device.deviceId}")
            return
        }
        
        Log.d(TAG, "Starting interrupt polling for device: ${device.deviceId}")
        
        // Track last data for each endpoint to detect changes (optional optimization)
        val lastData = ConcurrentHashMap<Int, ByteArray>()
        
        try {
            while (isActive) {
                for (endpoint in endpoints) {
                    if (!isActive) break
                    
                    try {
                        val buffer = ByteArray(endpoint.maxPacketSize)
                        
                        // Use endpoint's interval as timeout, minimum 1ms
                        val timeout = maxOf(endpoint.interval, 1)
                        
                        val bytesRead = connection.bulkTransfer(
                            endpoint,
                            buffer,
                            buffer.size,
                            timeout
                        )
                        
                        if (bytesRead > 0) {
                            val data = buffer.copyOf(bytesRead)
                            
                            // Optional: Only emit if data changed (reduces network traffic)
                            // Comment out this check if you need every poll result
                            val lastBytes = lastData[endpoint.address]
                            if (lastBytes == null || !lastBytes.contentEquals(data)) {
                                lastData[endpoint.address] = data
                                
                                // Emit the interrupt data
                                _interruptData.emit(
                                    InterruptData(
                                        deviceId = device.deviceId,
                                        endpointAddress = endpoint.address,
                                        data = data
                                    )
                                )
                            }
                        }
                        // bytesRead == 0 means timeout (no data), which is normal
                        // bytesRead < 0 means error
                        
                    } catch (e: Exception) {
                        if (isActive) {
                            Log.w(TAG, "Error polling endpoint 0x${endpoint.address.toString(16)}: ${e.message}")
                        }
                    }
                }
                
                // Small yield to prevent tight loop
                yield()
            }
        } catch (e: CancellationException) {
            Log.d(TAG, "Polling cancelled for device: ${device.deviceId}")
        } finally {
            Log.d(TAG, "Polling stopped for device: ${device.deviceId}")
        }
    }
}
