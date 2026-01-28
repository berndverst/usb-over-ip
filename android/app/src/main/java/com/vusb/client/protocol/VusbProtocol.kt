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
    
    // Protocol commands
    object Command {
        const val CONNECT: Short = 0x0001
        const val CONNECT_ACK: Short = 0x0002
        const val DISCONNECT: Short = 0x0003
        const val ATTACH: Short = 0x0004
        const val DETACH: Short = 0x0005
        const val URB_SUBMIT: Short = 0x0006
        const val URB_COMPLETE: Short = 0x0007
        const val RESET: Short = 0x0008
        const val KEEPALIVE: Short = 0x0009
        const val ERROR: Short = 0x000A
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
 */
data class DeviceInfo(
    val deviceId: Int,
    val vendorId: Int,
    val productId: Int,
    val deviceClass: Byte,
    val deviceSubclass: Byte,
    val deviceProtocol: Byte,
    val speed: Byte,
    val manufacturer: String = "",
    val product: String = "",
    val serialNumber: String = "",
    val deviceDescriptor: ByteArray = ByteArray(0),
    val configDescriptor: ByteArray = ByteArray(0)
) {
    fun toByteArray(): ByteArray {
        val manufacturerBytes = manufacturer.toByteArray(Charsets.UTF_8)
        val productBytes = product.toByteArray(Charsets.UTF_8)
        val serialBytes = serialNumber.toByteArray(Charsets.UTF_8)
        
        // Calculate total size
        val size = 4 + // deviceId
                   2 + // vendorId
                   2 + // productId
                   1 + // deviceClass
                   1 + // deviceSubclass
                   1 + // deviceProtocol
                   1 + // speed
                   1 + manufacturerBytes.size + // manufacturer length + data
                   1 + productBytes.size + // product length + data
                   1 + serialBytes.size + // serial length + data
                   2 + deviceDescriptor.size + // descriptor length + data
                   2 + configDescriptor.size   // config descriptor length + data
        
        val buffer = ByteBuffer.allocate(size)
        buffer.order(ByteOrder.LITTLE_ENDIAN)
        
        buffer.putInt(deviceId)
        buffer.putShort(vendorId.toShort())
        buffer.putShort(productId.toShort())
        buffer.put(deviceClass)
        buffer.put(deviceSubclass)
        buffer.put(deviceProtocol)
        buffer.put(speed)
        
        buffer.put(manufacturerBytes.size.toByte())
        buffer.put(manufacturerBytes)
        
        buffer.put(productBytes.size.toByte())
        buffer.put(productBytes)
        
        buffer.put(serialBytes.size.toByte())
        buffer.put(serialBytes)
        
        buffer.putShort(deviceDescriptor.size.toShort())
        buffer.put(deviceDescriptor)
        
        buffer.putShort(configDescriptor.size.toShort())
        buffer.put(configDescriptor)
        
        return buffer.array()
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
 * Connect message payload
 */
data class ConnectMessage(
    val clientName: String,
    val clientVersion: String = "1.0.0",
    val platform: String = "Android"
) {
    fun toByteArray(): ByteArray {
        val nameBytes = clientName.toByteArray(Charsets.UTF_8)
        val versionBytes = clientVersion.toByteArray(Charsets.UTF_8)
        val platformBytes = platform.toByteArray(Charsets.UTF_8)
        
        val buffer = ByteBuffer.allocate(3 + nameBytes.size + versionBytes.size + platformBytes.size)
        buffer.order(ByteOrder.LITTLE_ENDIAN)
        
        buffer.put(nameBytes.size.toByte())
        buffer.put(nameBytes)
        buffer.put(versionBytes.size.toByte())
        buffer.put(versionBytes)
        buffer.put(platformBytes.size.toByte())
        buffer.put(platformBytes)
        
        return buffer.array()
    }
}
