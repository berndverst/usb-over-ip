package com.vusb.client.usb

import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.hardware.usb.*
import android.os.Build
import android.util.Log
import com.vusb.client.protocol.DeviceInfo
import com.vusb.client.protocol.VusbProtocol
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import java.util.concurrent.ConcurrentHashMap

/**
 * USB Device Manager - handles USB device enumeration and communication
 * 
 * This class manages:
 * - Device enumeration
 * - Permission requests
 * - Device connections
 * - Data transfers
 */
class UsbDeviceManager(private val context: Context) {
    
    companion object {
        private const val TAG = "UsbDeviceManager"
        const val ACTION_USB_PERMISSION = "com.vusb.client.USB_PERMISSION"
    }
    
    private val usbManager: UsbManager = context.getSystemService(Context.USB_SERVICE) as UsbManager
    
    // Connected devices with their connections
    private val connectedDevices = ConcurrentHashMap<Int, UsbDeviceConnection>()
    private val deviceInterfaces = ConcurrentHashMap<Int, MutableList<UsbInterface>>()
    
    // State flows for reactive updates
    private val _availableDevices = MutableStateFlow<List<UsbDevice>>(emptyList())
    val availableDevices: StateFlow<List<UsbDevice>> = _availableDevices.asStateFlow()
    
    private val _permissionGranted = MutableStateFlow<Pair<UsbDevice, Boolean>?>(null)
    val permissionGranted: StateFlow<Pair<UsbDevice, Boolean>?> = _permissionGranted.asStateFlow()
    
    // Permission receiver
    private val permissionReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            if (ACTION_USB_PERMISSION == intent.action) {
                synchronized(this) {
                    val device = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                        intent.getParcelableExtra(UsbManager.EXTRA_DEVICE, UsbDevice::class.java)
                    } else {
                        @Suppress("DEPRECATION")
                        intent.getParcelableExtra(UsbManager.EXTRA_DEVICE)
                    }
                    
                    val granted = intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false)
                    Log.d(TAG, "Permission ${if (granted) "granted" else "denied"} for device: ${device?.deviceName}")
                    
                    device?.let {
                        _permissionGranted.value = Pair(it, granted)
                    }
                }
            }
        }
    }
    
    // Device attach/detach receiver
    private val deviceReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            when (intent.action) {
                UsbManager.ACTION_USB_DEVICE_ATTACHED -> {
                    Log.d(TAG, "USB device attached")
                    refreshDeviceList()
                }
                UsbManager.ACTION_USB_DEVICE_DETACHED -> {
                    val device = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                        intent.getParcelableExtra(UsbManager.EXTRA_DEVICE, UsbDevice::class.java)
                    } else {
                        @Suppress("DEPRECATION")
                        intent.getParcelableExtra(UsbManager.EXTRA_DEVICE)
                    }
                    Log.d(TAG, "USB device detached: ${device?.deviceName}")
                    device?.let { closeDevice(it.deviceId) }
                    refreshDeviceList()
                }
            }
        }
    }
    
    private var isRegistered = false
    
    /**
     * Initialize the USB manager and register receivers
     */
    fun initialize() {
        if (isRegistered) return
        
        val permissionFilter = IntentFilter(ACTION_USB_PERMISSION)
        val deviceFilter = IntentFilter().apply {
            addAction(UsbManager.ACTION_USB_DEVICE_ATTACHED)
            addAction(UsbManager.ACTION_USB_DEVICE_DETACHED)
        }
        
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            context.registerReceiver(permissionReceiver, permissionFilter, Context.RECEIVER_NOT_EXPORTED)
            context.registerReceiver(deviceReceiver, deviceFilter, Context.RECEIVER_EXPORTED)
        } else {
            context.registerReceiver(permissionReceiver, permissionFilter)
            context.registerReceiver(deviceReceiver, deviceFilter)
        }
        
        isRegistered = true
        refreshDeviceList()
    }
    
    /**
     * Cleanup resources
     */
    fun cleanup() {
        if (isRegistered) {
            try {
                context.unregisterReceiver(permissionReceiver)
                context.unregisterReceiver(deviceReceiver)
            } catch (e: Exception) {
                Log.w(TAG, "Error unregistering receivers", e)
            }
            isRegistered = false
        }
        
        // Close all connections
        connectedDevices.keys.forEach { closeDevice(it) }
    }
    
    /**
     * Refresh the list of available USB devices
     */
    fun refreshDeviceList() {
        val devices = usbManager.deviceList.values.toList()
        Log.d(TAG, "Found ${devices.size} USB devices")
        _availableDevices.value = devices
    }
    
    /**
     * Get all available USB devices
     */
    fun getDevices(): List<UsbDevice> {
        return usbManager.deviceList.values.toList()
    }
    
    /**
     * Check if we have permission for a device
     */
    fun hasPermission(device: UsbDevice): Boolean {
        return usbManager.hasPermission(device)
    }
    
    /**
     * Request permission for a USB device
     */
    fun requestPermission(device: UsbDevice) {
        if (!hasPermission(device)) {
            val flags = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                PendingIntent.FLAG_MUTABLE
            } else {
                0
            }
            val permissionIntent = PendingIntent.getBroadcast(
                context, 
                0, 
                Intent(ACTION_USB_PERMISSION),
                flags
            )
            usbManager.requestPermission(device, permissionIntent)
        }
    }
    
    /**
     * Open a USB device for communication
     */
    fun openDevice(device: UsbDevice): Boolean {
        if (!hasPermission(device)) {
            Log.e(TAG, "No permission for device: ${device.deviceName}")
            return false
        }
        
        val connection = usbManager.openDevice(device)
        if (connection == null) {
            Log.e(TAG, "Failed to open device: ${device.deviceName}")
            return false
        }
        
        connectedDevices[device.deviceId] = connection
        deviceInterfaces[device.deviceId] = mutableListOf()
        
        // Claim all interfaces
        for (i in 0 until device.interfaceCount) {
            val intf = device.getInterface(i)
            if (connection.claimInterface(intf, true)) {
                deviceInterfaces[device.deviceId]?.add(intf)
                Log.d(TAG, "Claimed interface $i on device ${device.deviceName}")
            } else {
                Log.w(TAG, "Failed to claim interface $i on device ${device.deviceName}")
            }
        }
        
        Log.d(TAG, "Opened device: ${device.deviceName}")
        return true
    }
    
    /**
     * Close a USB device connection
     */
    fun closeDevice(deviceId: Int) {
        val connection = connectedDevices.remove(deviceId)
        val interfaces = deviceInterfaces.remove(deviceId)
        
        interfaces?.forEach { intf ->
            try {
                connection?.releaseInterface(intf)
            } catch (e: Exception) {
                Log.w(TAG, "Error releasing interface", e)
            }
        }
        
        connection?.close()
        Log.d(TAG, "Closed device: $deviceId")
    }
    
    /**
     * Check if a device is open
     */
    fun isDeviceOpen(deviceId: Int): Boolean {
        return connectedDevices.containsKey(deviceId)
    }
    
    /**
     * Get the connection for a device
     */
    fun getConnection(deviceId: Int): UsbDeviceConnection? {
        return connectedDevices[deviceId]
    }
    
    /**
     * Get device by ID
     */
    fun getDevice(deviceId: Int): UsbDevice? {
        return usbManager.deviceList.values.find { it.deviceId == deviceId }
    }
    
    /**
     * Convert UsbDevice to our DeviceInfo protocol structure
     */
    fun getDeviceInfo(device: UsbDevice): DeviceInfo {
        val speed = when {
            Build.VERSION.SDK_INT >= Build.VERSION_CODES.M -> {
                // Try to determine speed from device class/capabilities
                // Note: Android doesn't expose USB speed directly in all versions
                VusbProtocol.UsbSpeed.HIGH // Default assumption
            }
            else -> VusbProtocol.UsbSpeed.FULL
        }
        
        return DeviceInfo(
            deviceId = device.deviceId,
            vendorId = device.vendorId,
            productId = device.productId,
            deviceClass = device.deviceClass.toByte(),
            deviceSubclass = device.deviceSubclass.toByte(),
            deviceProtocol = device.deviceProtocol.toByte(),
            speed = speed,
            manufacturer = device.manufacturerName ?: "",
            product = device.productName ?: "",
            serialNumber = device.serialNumber ?: "",
            deviceDescriptor = getDeviceDescriptor(device),
            configDescriptor = getConfigDescriptor(device)
        )
    }
    
    /**
     * Get device descriptor bytes
     */
    private fun getDeviceDescriptor(device: UsbDevice): ByteArray {
        val connection = connectedDevices[device.deviceId]
        if (connection != null) {
            val buffer = ByteArray(18) // Standard device descriptor size
            val result = connection.controlTransfer(
                0x80, // USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE
                0x06, // GET_DESCRIPTOR
                0x0100, // DEVICE descriptor
                0,
                buffer,
                buffer.size,
                1000
            )
            if (result > 0) {
                return buffer.copyOf(result)
            }
        }
        
        // Build descriptor manually if we can't get it from device
        return buildDeviceDescriptor(device)
    }
    
    /**
     * Build a device descriptor manually from UsbDevice info
     */
    private fun buildDeviceDescriptor(device: UsbDevice): ByteArray {
        return byteArrayOf(
            18,                                    // bLength
            1,                                     // bDescriptorType (DEVICE)
            0x00, 0x02,                           // bcdUSB (2.0)
            device.deviceClass.toByte(),          // bDeviceClass
            device.deviceSubclass.toByte(),       // bDeviceSubClass
            device.deviceProtocol.toByte(),       // bDeviceProtocol
            64,                                   // bMaxPacketSize0
            (device.vendorId and 0xFF).toByte(),
            ((device.vendorId shr 8) and 0xFF).toByte(),
            (device.productId and 0xFF).toByte(),
            ((device.productId shr 8) and 0xFF).toByte(),
            0x00, 0x01,                           // bcdDevice
            1,                                    // iManufacturer
            2,                                    // iProduct
            3,                                    // iSerialNumber
            device.configurationCount.toByte()    // bNumConfigurations
        )
    }
    
    /**
     * Get configuration descriptor bytes
     */
    private fun getConfigDescriptor(device: UsbDevice): ByteArray {
        val connection = connectedDevices[device.deviceId]
        if (connection != null) {
            // First, get just the header to find the total length
            val header = ByteArray(9)
            var result = connection.controlTransfer(
                0x80, // USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE
                0x06, // GET_DESCRIPTOR
                0x0200, // CONFIGURATION descriptor, index 0
                0,
                header,
                header.size,
                1000
            )
            
            if (result >= 4) {
                val totalLength = (header[2].toInt() and 0xFF) or ((header[3].toInt() and 0xFF) shl 8)
                val fullDescriptor = ByteArray(totalLength)
                result = connection.controlTransfer(
                    0x80,
                    0x06,
                    0x0200,
                    0,
                    fullDescriptor,
                    fullDescriptor.size,
                    1000
                )
                if (result > 0) {
                    return fullDescriptor.copyOf(result)
                }
            }
        }
        
        // Build descriptor manually if we can't get it from device
        return buildConfigDescriptor(device)
    }
    
    /**
     * Build a configuration descriptor manually
     */
    private fun buildConfigDescriptor(device: UsbDevice): ByteArray {
        val descriptors = mutableListOf<Byte>()
        
        // Calculate total length first
        var totalLength = 9 // Config descriptor header
        for (i in 0 until device.interfaceCount) {
            val intf = device.getInterface(i)
            totalLength += 9 // Interface descriptor
            for (j in 0 until intf.endpointCount) {
                totalLength += 7 // Endpoint descriptor
            }
        }
        
        // Configuration descriptor
        descriptors.addAll(listOf(
            9.toByte(),                           // bLength
            2.toByte(),                           // bDescriptorType (CONFIGURATION)
            (totalLength and 0xFF).toByte(),
            ((totalLength shr 8) and 0xFF).toByte(),
            device.interfaceCount.toByte(),       // bNumInterfaces
            1.toByte(),                           // bConfigurationValue
            0.toByte(),                           // iConfiguration
            0x80.toByte(),                        // bmAttributes (bus powered)
            250.toByte()                          // bMaxPower (500mA)
        ))
        
        // Add interface and endpoint descriptors
        for (i in 0 until device.interfaceCount) {
            val intf = device.getInterface(i)
            
            // Interface descriptor
            descriptors.addAll(listOf(
                9.toByte(),                           // bLength
                4.toByte(),                           // bDescriptorType (INTERFACE)
                intf.id.toByte(),                     // bInterfaceNumber
                intf.alternateSetting.toByte(),       // bAlternateSetting
                intf.endpointCount.toByte(),          // bNumEndpoints
                intf.interfaceClass.toByte(),         // bInterfaceClass
                intf.interfaceSubclass.toByte(),      // bInterfaceSubclass
                intf.interfaceProtocol.toByte(),      // bInterfaceProtocol
                0.toByte()                            // iInterface
            ))
            
            // Endpoint descriptors
            for (j in 0 until intf.endpointCount) {
                val ep = intf.getEndpoint(j)
                descriptors.addAll(listOf(
                    7.toByte(),                       // bLength
                    5.toByte(),                       // bDescriptorType (ENDPOINT)
                    ep.address.toByte(),              // bEndpointAddress
                    ep.type.toByte(),                 // bmAttributes
                    (ep.maxPacketSize and 0xFF).toByte(),
                    ((ep.maxPacketSize shr 8) and 0xFF).toByte(),
                    ep.interval.toByte()              // bInterval
                ))
            }
        }
        
        return descriptors.toByteArray()
    }
    
    /**
     * Perform a control transfer
     */
    fun controlTransfer(
        deviceId: Int,
        requestType: Int,
        request: Int,
        value: Int,
        index: Int,
        buffer: ByteArray?,
        length: Int,
        timeout: Int = 1000
    ): Int {
        val connection = connectedDevices[deviceId] ?: return -1
        return connection.controlTransfer(requestType, request, value, index, buffer, length, timeout)
    }
    
    /**
     * Perform a bulk transfer
     */
    fun bulkTransfer(
        deviceId: Int,
        endpoint: UsbEndpoint,
        buffer: ByteArray,
        length: Int,
        timeout: Int = 1000
    ): Int {
        val connection = connectedDevices[deviceId] ?: return -1
        return connection.bulkTransfer(endpoint, buffer, length, timeout)
    }
    
    /**
     * Find endpoint by address
     */
    fun findEndpoint(device: UsbDevice, endpointAddress: Int): UsbEndpoint? {
        for (i in 0 until device.interfaceCount) {
            val intf = device.getInterface(i)
            for (j in 0 until intf.endpointCount) {
                val ep = intf.getEndpoint(j)
                if (ep.address == endpointAddress) {
                    return ep
                }
            }
        }
        return null
    }
}
