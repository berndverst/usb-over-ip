package com.vusb.client.protocol

import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * VUSB Protocol definitions - compatible with Windows server
 * 
 * Protocol header format:
 * - Magic: 4 bytes (0x56555342 = "VUSB")
 * - Version: 2 bytes (major.minor)
 * - Command: 2 bytes
 * - Length: 4 bytes (payload length)
 * - Sequence: 4 bytes (sequence number)
 * - Payload: variable
 */
object VusbProtocol {
    
    // Protocol constants
    const val MAGIC = 0x56555342  // "VUSB" in little-endian
    const val VERSION_MAJOR = 1
    const val VERSION_MINOR = 0
    const val HEADER_SIZE = 16
    const val DEFAULT_PORT = 7575
    const val MAX_PAYLOAD_SIZE = 65536
    
    // Protocol commands (must match vusb_protocol.h)
    object Command {
        // Connection Management
        const val CONNECT: Short = 0x0001
        const val DISCONNECT: Short = 0x0002
        const val PING: Short = 0x0003
        const val PONG: Short = 0x0004
        
        // Device Management
        const val ATTACH: Short = 0x0010
        const val DETACH: Short = 0x0011
        const val DEVICE_LIST: Short = 0x0012
        const val DEVICE_INFO: Short = 0x0013
        
        // USB Transfers
        const val URB_SUBMIT: Short = 0x0020
        const val URB_COMPLETE: Short = 0x0021
        const val URB_CANCEL: Short = 0x0022
        
        // Descriptor Requests
        const val GET_DESCRIPTOR: Short = 0x0030
        const val DESCRIPTOR_DATA: Short = 0x0031
        
        // Control Transfers
        const val CONTROL_TRANSFER: Short = 0x0040
        const val CONTROL_RESPONSE: Short = 0x0041
        
        // Bulk/Interrupt Transfers
        const val BULK_TRANSFER: Short = 0x0050
        const val INTERRUPT_TRANSFER: Short = 0x0051
        const val TRANSFER_COMPLETE: Short = 0x0052
        
        // Isochronous Transfers
        const val ISO_TRANSFER: Short = 0x0060
        const val ISO_COMPLETE: Short = 0x0061
        
        // Error/Status
        const val ERROR: Short = 0x00FF.toShort()
        const val STATUS: Short = 0x00FE.toShort()
    }
    
    // USB speeds
    object UsbSpeed {
        const val LOW: Byte = 1      // 1.5 Mbps
        const val FULL: Byte = 2     // 12 Mbps
        const val HIGH: Byte = 3     // 480 Mbps
        const val SUPER: Byte = 4    // 5 Gbps
    }
    
    // URB Functions
    object UrbFunction {
        const val SELECT_CONFIGURATION: Short = 0x0000
        const val SELECT_INTERFACE: Short = 0x0001
        const val ABORT_PIPE: Short = 0x0002
        const val TAKE_FRAME_LENGTH_CONTROL: Short = 0x0003
        const val RELEASE_FRAME_LENGTH_CONTROL: Short = 0x0004
        const val GET_FRAME_LENGTH: Short = 0x0005
        const val SET_FRAME_LENGTH: Short = 0x0006
        const val GET_CURRENT_FRAME_NUMBER: Short = 0x0007
        const val CONTROL_TRANSFER: Short = 0x0008
        const val BULK_OR_INTERRUPT_TRANSFER: Short = 0x0009
        const val ISOCH_TRANSFER: Short = 0x000A
        const val GET_DESCRIPTOR_FROM_DEVICE: Short = 0x000B
        const val SET_DESCRIPTOR_TO_DEVICE: Short = 0x000C
        const val SET_FEATURE_TO_DEVICE: Short = 0x000D
        const val SET_FEATURE_TO_INTERFACE: Short = 0x000E
        const val SET_FEATURE_TO_ENDPOINT: Short = 0x000F
        const val CLEAR_FEATURE_TO_DEVICE: Short = 0x0010
        const val CLEAR_FEATURE_TO_INTERFACE: Short = 0x0011
        const val CLEAR_FEATURE_TO_ENDPOINT: Short = 0x0012
        const val GET_STATUS_FROM_DEVICE: Short = 0x0013
        const val GET_STATUS_FROM_INTERFACE: Short = 0x0014
        const val GET_STATUS_FROM_ENDPOINT: Short = 0x0015
        const val SYNC_RESET_PIPE: Short = 0x001E
        const val SYNC_CLEAR_STALL: Short = 0x001F
    }
    
    // USBD Status codes
    object UsbdStatus {
        const val SUCCESS: Int = 0x00000000
        const val PENDING: Int = 0x40000000
        const val CANCELED: Int = -0x00010000  // 0xC0010000
        const val STALL_PID: Int = -0x00030001  // 0xC0000004
        const val ERROR_BUSY: Int = -0x00060000
        const val ERROR_SHORT_TRANSFER: Int = -0x00090000
    }
    
    // Transfer direction
    const val TRANSFER_DIRECTION_OUT: Byte = 0
    const val TRANSFER_DIRECTION_IN: Byte = 1
}

/**
 * Protocol message header
 */
data class VusbHeader(
    val magic: Int = VusbProtocol.MAGIC,
    val version: Short = ((VusbProtocol.VERSION_MAJOR shl 8) or VusbProtocol.VERSION_MINOR).toShort(),
    val command: Short,
    val length: Int,
    val sequence: Int
) {
    fun toByteArray(): ByteArray {
        val buffer = ByteBuffer.allocate(VusbProtocol.HEADER_SIZE)
        buffer.order(ByteOrder.LITTLE_ENDIAN)
        buffer.putInt(magic)
        buffer.putShort(version)
        buffer.putShort(command)
        buffer.putInt(length)
        buffer.putInt(sequence)
        return buffer.array()
    }
    
    companion object {
        fun fromByteArray(data: ByteArray): VusbHeader {
            require(data.size >= VusbProtocol.HEADER_SIZE) { "Invalid header size" }
            val buffer = ByteBuffer.wrap(data)
            buffer.order(ByteOrder.LITTLE_ENDIAN)
            return VusbHeader(
                magic = buffer.int,
                version = buffer.short,
                command = buffer.short,
                length = buffer.int,
                sequence = buffer.int
            )
        }
    }
}

/**
 * Device info for ATTACH command
 * Must match VUSB_DEVICE_INFO in vusb_protocol.h (208 bytes total)
 */
data class DeviceInfo(
    val deviceId: Int,
    val vendorId: Int,
    val productId: Int,
    val deviceClass: Byte,
    val deviceSubclass: Byte,
    val deviceProtocol: Byte,
    val speed: Byte,
    val numConfigurations: Byte = 1,
    val numInterfaces: Byte = 1,
    val manufacturer: String = "",
    val product: String = "",
    val serialNumber: String = "",
    val deviceDescriptor: ByteArray = ByteArray(0),
    val configDescriptor: ByteArray = ByteArray(0)
) {
    /**
     * Serialize to protocol format matching VUSB_DEVICE_INFO (208 bytes)
     * followed by descriptorLength (4 bytes) and descriptors
     */
    fun toByteArray(): ByteArray {
        // VUSB_DEVICE_INFO is exactly 208 bytes
        val infoSize = 208
        val descriptors = deviceDescriptor + configDescriptor
        val totalSize = infoSize + 4 + descriptors.size  // info + length + descriptors
        
        val buffer = ByteBuffer.allocate(totalSize)
        buffer.order(ByteOrder.LITTLE_ENDIAN)
        
        // DeviceId (4 bytes)
        buffer.putInt(deviceId)
        
        // VendorId, ProductId (2 bytes each)
        buffer.putShort(vendorId.toShort())
        buffer.putShort(productId.toShort())
        
        // DeviceClass, DeviceSubClass, DeviceProtocol, Speed (1 byte each)
        buffer.put(deviceClass)
        buffer.put(deviceSubclass)
        buffer.put(deviceProtocol)
        buffer.put(speed)
        
        // NumConfigurations, NumInterfaces, Reserved[2] (4 bytes total)
        buffer.put(numConfigurations)
        buffer.put(numInterfaces)
        buffer.put(0) // Reserved[0]
        buffer.put(0) // Reserved[1]
        
        // Fixed-size strings (64 bytes each, null-padded)
        buffer.put(fixedString(manufacturer, 64))
        buffer.put(fixedString(product, 64))
        buffer.put(fixedString(serialNumber, 64))
        
        // Descriptor length (4 bytes) followed by descriptors
        buffer.putInt(descriptors.size)
        buffer.put(descriptors)
        
        return buffer.array()
    }
    
    private fun fixedString(str: String, length: Int): ByteArray {
        val result = ByteArray(length)
        val bytes = str.toByteArray(Charsets.UTF_8)
        val copyLen = minOf(bytes.size, length - 1)  // Leave room for null terminator
        System.arraycopy(bytes, 0, result, 0, copyLen)
        return result
    }
    
    override fun equals(other: Any?): Boolean {
        if (this === other) return true
        if (javaClass != other?.javaClass) return false
        other as DeviceInfo
        return deviceId == other.deviceId &&
               vendorId == other.vendorId &&
               productId == other.productId
    }
    
    override fun hashCode(): Int {
        var result = deviceId
        result = 31 * result + vendorId
        result = 31 * result + productId
        return result
    }
}

/**
 * URB submit message
 */
data class UrbSubmit(
    val urbId: Int,
    val deviceId: Int,
    val function: Short,
    val endpoint: Byte,
    val direction: Byte,
    val transferFlags: Int,
    val bufferLength: Int,
    val setupPacket: ByteArray = ByteArray(8),
    val data: ByteArray = ByteArray(0)
) {
    companion object {
        fun fromByteArray(data: ByteArray): UrbSubmit {
            val buffer = ByteBuffer.wrap(data)
            buffer.order(ByteOrder.LITTLE_ENDIAN)
            
            val urbId = buffer.int
            val deviceId = buffer.int
            val function = buffer.short
            val endpoint = buffer.get()
            val direction = buffer.get()
            val transferFlags = buffer.int
            val bufferLength = buffer.int
            
            val setupPacket = ByteArray(8)
            buffer.get(setupPacket)
            
            val transferData = if (buffer.remaining() > 0) {
                ByteArray(buffer.remaining()).also { buffer.get(it) }
            } else {
                ByteArray(0)
            }
            
            return UrbSubmit(
                urbId = urbId,
                deviceId = deviceId,
                function = function,
                endpoint = endpoint,
                direction = direction,
                transferFlags = transferFlags,
                bufferLength = bufferLength,
                setupPacket = setupPacket,
                data = transferData
            )
        }
    }
    
    override fun equals(other: Any?): Boolean {
        if (this === other) return true
        if (javaClass != other?.javaClass) return false
        other as UrbSubmit
        return urbId == other.urbId && deviceId == other.deviceId
    }
    
    override fun hashCode(): Int {
        return 31 * urbId + deviceId
    }
}

/**
 * URB complete message
 */
data class UrbComplete(
    val urbId: Int,
    val status: Int,
    val actualLength: Int,
    val data: ByteArray = ByteArray(0)
) {
    fun toByteArray(): ByteArray {
        val buffer = ByteBuffer.allocate(12 + data.size)
        buffer.order(ByteOrder.LITTLE_ENDIAN)
        buffer.putInt(urbId)
        buffer.putInt(status)
        buffer.putInt(actualLength)
        buffer.put(data)
        return buffer.array()
    }
    
    override fun equals(other: Any?): Boolean {
        if (this === other) return true
        if (javaClass != other?.javaClass) return false
        other as UrbComplete
        return urbId == other.urbId
    }
    
    override fun hashCode(): Int = urbId
}

/**
 * Connect message payload matching VUSB_CONNECT_REQUEST (72 bytes)
 * - ClientVersion: 4 bytes
 * - Capabilities: 4 bytes  
 * - ClientName: 64 bytes (fixed, null-padded)
 */
data class ConnectMessage(
    val clientName: String,
    val clientVersion: Int = 0x00010000,
    val capabilities: Int = 0
) {
    fun toByteArray(): ByteArray {
        val buffer = ByteBuffer.allocate(72)
        buffer.order(ByteOrder.LITTLE_ENDIAN)
        
        // ClientVersion (4 bytes)
        buffer.putInt(clientVersion)
        
        // Capabilities (4 bytes)
        buffer.putInt(capabilities)
        
        // ClientName (64 bytes, fixed, null-padded)
        val nameBytes = clientName.toByteArray(Charsets.UTF_8)
        val nameBuffer = ByteArray(64)
        val copyLen = minOf(nameBytes.size, 63)  // Leave room for null terminator
        System.arraycopy(nameBytes, 0, nameBuffer, 0, copyLen)
        buffer.put(nameBuffer)
        
        return buffer.array()
    }
}
