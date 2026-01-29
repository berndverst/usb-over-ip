//
//  VusbProtocol.swift
//  VusbClient
//
//  VUSB Protocol definitions - compatible with Windows server
//

import Foundation

// MARK: - Protocol Constants

/// VUSB Protocol magic number "VUSB" in little-endian
let VUSB_PROTOCOL_MAGIC: UInt32 = 0x56555342

/// Protocol version (1.0)
let VUSB_PROTOCOL_VERSION: UInt16 = 0x0100

/// Default server port
let VUSB_DEFAULT_PORT: UInt16 = 7575

/// Maximum packet size
let VUSB_MAX_PACKET_SIZE: Int = 65536

/// Maximum number of devices
let VUSB_MAX_DEVICES: Int = 16

/// Protocol header size in bytes
let VUSB_HEADER_SIZE: Int = 16

// MARK: - Protocol Commands

/// Command types for VUSB protocol messages
enum VusbCommand: UInt16 {
    // Connection Management
    case connect = 0x0001
    case disconnect = 0x0002
    case ping = 0x0003
    case pong = 0x0004
    
    // Device Management
    case deviceAttach = 0x0010
    case deviceDetach = 0x0011
    case deviceList = 0x0012
    case deviceInfo = 0x0013
    
    // USB Transfers
    case urbSubmit = 0x0020
    case urbComplete = 0x0021
    case urbCancel = 0x0022
    
    // Descriptor Requests
    case getDescriptor = 0x0030
    case descriptorData = 0x0031
    
    // Control Transfers
    case controlTransfer = 0x0040
    case controlResponse = 0x0041
    
    // Bulk/Interrupt Transfers
    case bulkTransfer = 0x0050
    case interruptTransfer = 0x0051
    case transferComplete = 0x0052
    
    // Isochronous Transfers
    case isoTransfer = 0x0060
    case isoComplete = 0x0061
    
    // Error/Status
    case error = 0x00FF
    case status = 0x00FE
}

// MARK: - Status Codes

/// VUSB status codes
enum VusbStatus: UInt16 {
    case success = 0x0000
    case pending = 0x0001
    case error = 0x0002
    case stall = 0x0003
    case timeout = 0x0004
    case canceled = 0x0005
    case noDevice = 0x0006
    case invalidParam = 0x0007
    case noMemory = 0x0008
    case notSupported = 0x0009
    case disconnected = 0x000A
}

// MARK: - USB Speed

/// USB device speed
enum VusbSpeed: UInt8 {
    case unknown = 0
    case low = 1        // 1.5 Mbps
    case full = 2       // 12 Mbps
    case high = 3       // 480 Mbps
    case `super` = 4    // 5 Gbps
    case superPlus = 5  // 10 Gbps
    
    var description: String {
        switch self {
        case .unknown: return "Unknown"
        case .low: return "Low Speed (1.5 Mbps)"
        case .full: return "Full Speed (12 Mbps)"
        case .high: return "High Speed (480 Mbps)"
        case .super: return "SuperSpeed (5 Gbps)"
        case .superPlus: return "SuperSpeed+ (10 Gbps)"
        }
    }
}

// MARK: - Transfer Types

/// USB transfer types
enum VusbTransferType: UInt8 {
    case control = 0
    case isochronous = 1
    case bulk = 2
    case interrupt = 3
}

/// USB transfer direction
enum VusbDirection: UInt8 {
    case out = 0    // Host to device
    case `in` = 1   // Device to host
}

// MARK: - URB Functions

/// URB function codes
enum UrbFunction: UInt16 {
    case selectConfiguration = 0x0000
    case selectInterface = 0x0001
    case abortPipe = 0x0002
    case takeFrameLengthControl = 0x0003
    case releaseFrameLengthControl = 0x0004
    case getFrameLength = 0x0005
    case setFrameLength = 0x0006
    case getCurrentFrameNumber = 0x0007
    case controlTransfer = 0x0008
    case bulkOrInterruptTransfer = 0x0009
    case isochTransfer = 0x000A
    case getDescriptorFromDevice = 0x000B
    case setDescriptorToDevice = 0x000C
    case setFeatureToDevice = 0x000D
    case setFeatureToInterface = 0x000E
    case setFeatureToEndpoint = 0x000F
    case clearFeatureToDevice = 0x0010
    case clearFeatureToInterface = 0x0011
    case clearFeatureToEndpoint = 0x0012
    case getStatusFromDevice = 0x0013
    case getStatusFromInterface = 0x0014
    case getStatusFromEndpoint = 0x0015
    case syncResetPipe = 0x001E
    case syncClearStall = 0x001F
}

// MARK: - USBD Status Codes

/// USBD status codes for URB completion
struct UsbdStatus {
    static let success: Int32 = 0x00000000
    static let pending: Int32 = 0x40000000
    static let canceled: Int32 = -0x00010000  // 0xC0010000
    static let stallPid: Int32 = -0x00030001  // 0xC0000004
    static let errorBusy: Int32 = -0x00060000
    static let errorShortTransfer: Int32 = -0x00090000
}

// MARK: - Protocol Header

/// VUSB protocol message header
struct VusbHeader {
    let magic: UInt32
    let version: UInt16
    let command: UInt16
    let length: UInt32
    let sequence: UInt32
    
    init(command: VusbCommand, length: UInt32, sequence: UInt32) {
        self.magic = VUSB_PROTOCOL_MAGIC
        self.version = VUSB_PROTOCOL_VERSION
        self.command = command.rawValue
        self.length = length
        self.sequence = sequence
    }
    
    init(magic: UInt32, version: UInt16, command: UInt16, length: UInt32, sequence: UInt32) {
        self.magic = magic
        self.version = version
        self.command = command
        self.length = length
        self.sequence = sequence
    }
    
    /// Serialize header to bytes (little-endian)
    func toData() -> Data {
        var data = Data(capacity: VUSB_HEADER_SIZE)
        data.append(contentsOf: withUnsafeBytes(of: magic.littleEndian) { Array($0) })
        data.append(contentsOf: withUnsafeBytes(of: version.littleEndian) { Array($0) })
        data.append(contentsOf: withUnsafeBytes(of: command.littleEndian) { Array($0) })
        data.append(contentsOf: withUnsafeBytes(of: length.littleEndian) { Array($0) })
        data.append(contentsOf: withUnsafeBytes(of: sequence.littleEndian) { Array($0) })
        return data
    }
    
    /// Parse header from bytes
    static func fromData(_ data: Data) -> VusbHeader? {
        guard data.count >= VUSB_HEADER_SIZE else { return nil }
        
        let magic = data.subdata(in: 0..<4).withUnsafeBytes { $0.load(as: UInt32.self).littleEndian }
        let version = data.subdata(in: 4..<6).withUnsafeBytes { $0.load(as: UInt16.self).littleEndian }
        let command = data.subdata(in: 6..<8).withUnsafeBytes { $0.load(as: UInt16.self).littleEndian }
        let length = data.subdata(in: 8..<12).withUnsafeBytes { $0.load(as: UInt32.self).littleEndian }
        let sequence = data.subdata(in: 12..<16).withUnsafeBytes { $0.load(as: UInt32.self).littleEndian }
        
        return VusbHeader(magic: magic, version: version, command: command, length: length, sequence: sequence)
    }
    
    var commandType: VusbCommand? {
        return VusbCommand(rawValue: command)
    }
}

// MARK: - Connect Message

/// Connect message payload matching VUSB_CONNECT_REQUEST (72 bytes)
/// - ClientVersion: 4 bytes
/// - Capabilities: 4 bytes
/// - ClientName: 64 bytes (fixed, null-padded)
struct ConnectMessage {
    let clientName: String
    let clientVersion: UInt32
    let capabilities: UInt32
    
    init(clientName: String, clientVersion: UInt32 = 0x00010000, capabilities: UInt32 = 0) {
        self.clientName = clientName
        self.clientVersion = clientVersion
        self.capabilities = capabilities
    }
    
    func toData() -> Data {
        var data = Data(capacity: 72)
        
        // ClientVersion (4 bytes)
        data.append(contentsOf: withUnsafeBytes(of: clientVersion.littleEndian) { Array($0) })
        
        // Capabilities (4 bytes)
        data.append(contentsOf: withUnsafeBytes(of: capabilities.littleEndian) { Array($0) })
        
        // ClientName (64 bytes, fixed, null-padded)
        var nameBytes = Data(count: 64)
        let nameData = clientName.data(using: .utf8) ?? Data()
        let copyLength = min(nameData.count, 63)  // Leave room for null terminator
        nameBytes.replaceSubrange(0..<copyLength, with: nameData.prefix(copyLength))
        data.append(nameBytes)
        
        return data
    }
}

// MARK: - Device Info

/// USB device information for protocol messages
/// Must match VUSB_DEVICE_INFO in vusb_protocol.h (208 bytes total)
struct VusbDeviceInfo: Identifiable, Hashable {
    let id: Int32
    let vendorId: UInt16
    let productId: UInt16
    let deviceClass: UInt8
    let deviceSubclass: UInt8
    let deviceProtocol: UInt8
    let speed: VusbSpeed
    let numConfigurations: UInt8
    let numInterfaces: UInt8
    let manufacturer: String
    let product: String
    let serialNumber: String
    var deviceDescriptor: Data
    var configDescriptor: Data
    
    init(id: Int32, vendorId: UInt16, productId: UInt16, deviceClass: UInt8 = 0,
         deviceSubclass: UInt8 = 0, deviceProtocol: UInt8 = 0, speed: VusbSpeed = .unknown,
         numConfigurations: UInt8 = 1, numInterfaces: UInt8 = 1,
         manufacturer: String = "", product: String = "", serialNumber: String = "",
         deviceDescriptor: Data = Data(), configDescriptor: Data = Data()) {
        self.id = id
        self.vendorId = vendorId
        self.productId = productId
        self.deviceClass = deviceClass
        self.deviceSubclass = deviceSubclass
        self.deviceProtocol = deviceProtocol
        self.speed = speed
        self.numConfigurations = numConfigurations
        self.numInterfaces = numInterfaces
        self.manufacturer = manufacturer
        self.product = product
        self.serialNumber = serialNumber
        self.deviceDescriptor = deviceDescriptor
        self.configDescriptor = configDescriptor
    }
    
    /// Serialize to protocol format matching VUSB_DEVICE_INFO (208 bytes)
    func toData() -> Data {
        var data = Data(capacity: 208)
        
        // DeviceId (4 bytes)
        data.append(contentsOf: withUnsafeBytes(of: UInt32(bitPattern: id).littleEndian) { Array($0) })
        
        // VendorId, ProductId (2 bytes each)
        data.append(contentsOf: withUnsafeBytes(of: vendorId.littleEndian) { Array($0) })
        data.append(contentsOf: withUnsafeBytes(of: productId.littleEndian) { Array($0) })
        
        // DeviceClass, DeviceSubClass, DeviceProtocol, Speed (1 byte each)
        data.append(deviceClass)
        data.append(deviceSubclass)
        data.append(deviceProtocol)
        data.append(speed.rawValue)
        
        // NumConfigurations, NumInterfaces, Reserved[2] (4 bytes total)
        data.append(numConfigurations)
        data.append(numInterfaces)
        data.append(contentsOf: [0, 0]) // Reserved padding
        
        // Fixed-size strings (64 bytes each, null-padded)
        data.append(fixedString(manufacturer, length: 64))
        data.append(fixedString(product, length: 64))
        data.append(fixedString(serialNumber, length: 64))
        
        return data
    }
    
    /// Convert string to fixed-length null-padded data
    private func fixedString(_ string: String, length: Int) -> Data {
        var data = Data(count: length)
        let stringData = string.data(using: .utf8) ?? Data()
        let copyLength = min(stringData.count, length - 1) // Leave room for null terminator
        data.replaceSubrange(0..<copyLength, with: stringData.prefix(copyLength))
        return data
    }
    
    /// Parse from protocol data (VUSB_DEVICE_INFO format - 208 bytes)
    static func fromData(_ data: Data) -> VusbDeviceInfo? {
        guard data.count >= 208 else { return nil }
        
        var offset = 0
        
        let deviceId = data.subdata(in: offset..<offset+4).withUnsafeBytes { $0.load(as: Int32.self).littleEndian }
        offset += 4
        
        let vendorId = data.subdata(in: offset..<offset+2).withUnsafeBytes { $0.load(as: UInt16.self).littleEndian }
        offset += 2
        
        let productId = data.subdata(in: offset..<offset+2).withUnsafeBytes { $0.load(as: UInt16.self).littleEndian }
        offset += 2
        
        let deviceClass = data[offset]
        offset += 1
        
        let deviceSubclass = data[offset]
        offset += 1
        
        let deviceProtocol = data[offset]
        offset += 1
        
        let speedByte = data[offset]
        let speed = VusbSpeed(rawValue: speedByte) ?? .unknown
        offset += 1
        
        let numConfigurations = data[offset]
        offset += 1
        
        let numInterfaces = data[offset]
        offset += 1
        
        offset += 2 // Skip Reserved[2]
        
        // Parse fixed-size strings (64 bytes each, null-terminated)
        func readFixedString(length: Int) -> String {
            let stringData = data.subdata(in: offset..<offset+length)
            offset += length
            // Find null terminator
            if let nullIndex = stringData.firstIndex(of: 0) {
                return String(data: stringData.prefix(upTo: nullIndex), encoding: .utf8) ?? ""
            }
            return String(data: stringData, encoding: .utf8) ?? ""
        }
        
        let manufacturer = readFixedString(length: 64)
        let product = readFixedString(length: 64)
        let serialNumber = readFixedString(length: 64)
        
        return VusbDeviceInfo(
            id: deviceId,
            vendorId: vendorId,
            productId: productId,
            deviceClass: deviceClass,
            deviceSubclass: deviceSubclass,
            deviceProtocol: deviceProtocol,
            speed: speed,
            numConfigurations: numConfigurations,
            numInterfaces: numInterfaces,
            manufacturer: manufacturer,
            product: product,
            serialNumber: serialNumber
        )
    }
    
    func hash(into hasher: inout Hasher) {
        hasher.combine(id)
        hasher.combine(vendorId)
        hasher.combine(productId)
    }
    
    static func == (lhs: VusbDeviceInfo, rhs: VusbDeviceInfo) -> Bool {
        return lhs.id == rhs.id && lhs.vendorId == rhs.vendorId && lhs.productId == rhs.productId
    }
    
    /// Format vendor/product ID as hex string
    var vendorProductString: String {
        return String(format: "%04X:%04X", vendorId, productId)
    }
    
    /// Display name (product name or vendor:product)
    var displayName: String {
        if !product.isEmpty {
            return product
        }
        return "USB Device \(vendorProductString)"
    }
}

// MARK: - URB Messages

/// URB submit request
struct UrbSubmit {
    let urbId: Int32
    let deviceId: Int32
    let function: UInt16
    let endpoint: UInt8
    let direction: UInt8
    let transferFlags: UInt32
    let bufferLength: UInt32
    let setupPacket: Data  // 8 bytes
    let data: Data
    
    static func fromData(_ data: Data) -> UrbSubmit? {
        guard data.count >= 28 else { return nil }  // Minimum size without variable data
        
        var offset = 0
        
        let urbId = data.subdata(in: offset..<offset+4).withUnsafeBytes { $0.load(as: Int32.self).littleEndian }
        offset += 4
        
        let deviceId = data.subdata(in: offset..<offset+4).withUnsafeBytes { $0.load(as: Int32.self).littleEndian }
        offset += 4
        
        let function = data.subdata(in: offset..<offset+2).withUnsafeBytes { $0.load(as: UInt16.self).littleEndian }
        offset += 2
        
        let endpoint = data[offset]
        offset += 1
        
        let direction = data[offset]
        offset += 1
        
        let transferFlags = data.subdata(in: offset..<offset+4).withUnsafeBytes { $0.load(as: UInt32.self).littleEndian }
        offset += 4
        
        let bufferLength = data.subdata(in: offset..<offset+4).withUnsafeBytes { $0.load(as: UInt32.self).littleEndian }
        offset += 4
        
        let setupPacket = data.subdata(in: offset..<offset+8)
        offset += 8
        
        let transferData = offset < data.count ? data.subdata(in: offset..<data.count) : Data()
        
        return UrbSubmit(
            urbId: urbId,
            deviceId: deviceId,
            function: function,
            endpoint: endpoint,
            direction: direction,
            transferFlags: transferFlags,
            bufferLength: bufferLength,
            setupPacket: setupPacket,
            data: transferData
        )
    }
}

/// URB completion response
struct UrbComplete {
    let urbId: Int32
    let status: Int32
    let actualLength: Int32
    let data: Data
    
    func toData() -> Data {
        var result = Data()
        result.append(contentsOf: withUnsafeBytes(of: urbId.littleEndian) { Array($0) })
        result.append(contentsOf: withUnsafeBytes(of: status.littleEndian) { Array($0) })
        result.append(contentsOf: withUnsafeBytes(of: actualLength.littleEndian) { Array($0) })
        result.append(data)
        return result
    }
}
