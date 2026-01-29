//
//  UsbDeviceManager.swift
//  VusbClient
//
//  USB Device Manager using IOKit for macOS
//

import Foundation
import IOKit
import IOKit.usb
import Combine
import os.log

/// USB Device information from IOKit
struct MacUsbDevice: Identifiable, Hashable {
    let id: Int
    let vendorId: UInt16
    let productId: UInt16
    let deviceClass: UInt8
    let deviceSubclass: UInt8
    let deviceProtocol: UInt8
    let speed: VusbSpeed
    let manufacturer: String
    let product: String
    let serialNumber: String
    let locationId: UInt32
    let service: io_service_t
    
    var vendorProductString: String {
        return String(format: "%04X:%04X", vendorId, productId)
    }
    
    var displayName: String {
        if !product.isEmpty {
            return product
        }
        if !manufacturer.isEmpty {
            return "\(manufacturer) Device"
        }
        return "USB Device \(vendorProductString)"
    }
    
    func hash(into hasher: inout Hasher) {
        hasher.combine(vendorId)
        hasher.combine(productId)
        hasher.combine(locationId)
    }
    
    static func == (lhs: MacUsbDevice, rhs: MacUsbDevice) -> Bool {
        return lhs.vendorId == rhs.vendorId &&
               lhs.productId == rhs.productId &&
               lhs.locationId == rhs.locationId
    }
    
    /// Convert to VUSB protocol device info
    func toVusbDeviceInfo() -> VusbDeviceInfo {
        return VusbDeviceInfo(
            id: Int32(id),
            vendorId: vendorId,
            productId: productId,
            deviceClass: deviceClass,
            deviceSubclass: deviceSubclass,
            deviceProtocol: deviceProtocol,
            speed: speed,
            manufacturer: manufacturer,
            product: product,
            serialNumber: serialNumber
        )
    }
}

/// Device state for forwarding
enum DeviceForwardingState {
    case idle
    case attached
    case forwarding
    case error(String)
}

/// USB Device Manager for macOS
@MainActor
class UsbDeviceManager: ObservableObject {
    
    private let logger = Logger(subsystem: "com.vusb.client", category: "USB")
    
    // MARK: - Published Properties
    
    @Published private(set) var availableDevices: [MacUsbDevice] = []
    @Published private(set) var attachedDevices: Set<Int> = []
    @Published private(set) var deviceStates: [Int: DeviceForwardingState] = [:]
    
    // MARK: - Private Properties
    
    private var notificationPort: IONotificationPortRef?
    private var addedIterator: io_iterator_t = 0
    private var removedIterator: io_iterator_t = 0
    private var deviceIdCounter: Int = 1
    
    // Device handles for communication
    private var deviceInterfaces: [Int: IOUSBDeviceInterface] = [:]
    
    // MARK: - Initialization
    
    init() {
        setupNotifications()
        refreshDeviceList()
    }
    
    deinit {
        cleanup()
    }
    
    // MARK: - Device Enumeration
    
    /// Refresh the list of available USB devices
    func refreshDeviceList() {
        var devices: [MacUsbDevice] = []
        
        // Create matching dictionary for USB devices
        guard let matchingDict = IOServiceMatching(kIOUSBDeviceClassName) else {
            logger.error("Failed to create USB matching dictionary")
            return
        }
        
        var iterator: io_iterator_t = 0
        let result = IOServiceGetMatchingServices(kIOMainPortDefault, matchingDict, &iterator)
        
        guard result == KERN_SUCCESS else {
            logger.error("Failed to get USB services: \(result)")
            return
        }
        
        defer { IOObjectRelease(iterator) }
        
        var service = IOIteratorNext(iterator)
        while service != 0 {
            if let device = createDevice(from: service) {
                devices.append(device)
            }
            IOObjectRelease(service)
            service = IOIteratorNext(iterator)
        }
        
        logger.info("Found \(devices.count) USB devices")
        availableDevices = devices
    }
    
    /// Create MacUsbDevice from IOKit service
    private func createDevice(from service: io_service_t) -> MacUsbDevice? {
        // Get vendor ID
        guard let vendorId = getDeviceProperty(service, key: kUSBVendorID) as? Int else {
            return nil
        }
        
        // Get product ID
        guard let productId = getDeviceProperty(service, key: kUSBProductID) as? Int else {
            return nil
        }
        
        // Get other properties with defaults
        let deviceClass = (getDeviceProperty(service, key: kUSBDeviceClass) as? Int) ?? 0
        let deviceSubclass = (getDeviceProperty(service, key: kUSBDeviceSubClass) as? Int) ?? 0
        let deviceProtocol = (getDeviceProperty(service, key: kUSBDeviceProtocol) as? Int) ?? 0
        let speedValue = (getDeviceProperty(service, key: "Device Speed") as? Int) ?? 0
        let locationId = (getDeviceProperty(service, key: kUSBDevicePropertyLocationID) as? Int) ?? 0
        
        // Get string properties
        let manufacturer = (getDeviceProperty(service, key: "USB Vendor Name") as? String) ?? ""
        let product = (getDeviceProperty(service, key: "USB Product Name") as? String) ?? ""
        let serialNumber = (getDeviceProperty(service, key: kUSBSerialNumberString) as? String) ?? ""
        
        // Convert speed
        let speed: VusbSpeed
        switch speedValue {
        case 0: speed = .low
        case 1: speed = .full
        case 2: speed = .high
        case 3: speed = .super
        case 4: speed = .superPlus
        default: speed = .unknown
        }
        
        let deviceId = deviceIdCounter
        deviceIdCounter += 1
        
        return MacUsbDevice(
            id: deviceId,
            vendorId: UInt16(vendorId),
            productId: UInt16(productId),
            deviceClass: UInt8(deviceClass),
            deviceSubclass: UInt8(deviceSubclass),
            deviceProtocol: UInt8(deviceProtocol),
            speed: speed,
            manufacturer: manufacturer,
            product: product,
            serialNumber: serialNumber,
            locationId: UInt32(locationId),
            service: service
        )
    }
    
    /// Get device property from IOKit
    private func getDeviceProperty(_ service: io_service_t, key: String) -> Any? {
        let cfKey = key as CFString
        guard let value = IORegistryEntryCreateCFProperty(service, cfKey, kCFAllocatorDefault, 0) else {
            return nil
        }
        return value.takeRetainedValue()
    }
    
    // MARK: - Device Notifications
    
    /// Setup IOKit notifications for device attach/detach
    private func setupNotifications() {
        notificationPort = IONotificationPortCreate(kIOMainPortDefault)
        guard let notificationPort = notificationPort else {
            logger.error("Failed to create notification port")
            return
        }
        
        let runLoopSource = IONotificationPortGetRunLoopSource(notificationPort).takeUnretainedValue()
        CFRunLoopAddSource(CFRunLoopGetMain(), runLoopSource, .defaultMode)
        
        // Create matching dictionary
        guard let matchingDict = IOServiceMatching(kIOUSBDeviceClassName) else {
            logger.error("Failed to create matching dictionary")
            return
        }
        
        // We need to retain the dictionary for the second call
        let matchingDictCopy = matchingDict as NSDictionary
        
        // Device added notification
        let addCallback: IOServiceMatchingCallback = { (refcon, iterator) in
            // Drain the iterator
            var service = IOIteratorNext(iterator)
            while service != 0 {
                IOObjectRelease(service)
                service = IOIteratorNext(iterator)
            }
            
            // Refresh device list on main thread
            DispatchQueue.main.async {
                // Note: In a real implementation, we'd use refcon to get self
                // For now, post a notification
                NotificationCenter.default.post(name: .usbDeviceListChanged, object: nil)
            }
        }
        
        var result = IOServiceAddMatchingNotification(
            notificationPort,
            kIOFirstMatchNotification,
            matchingDict,
            addCallback,
            nil,
            &addedIterator
        )
        
        if result != KERN_SUCCESS {
            logger.error("Failed to add device added notification: \(result)")
        }
        
        // Drain initial iterator
        var service = IOIteratorNext(addedIterator)
        while service != 0 {
            IOObjectRelease(service)
            service = IOIteratorNext(addedIterator)
        }
        
        // Device removed notification
        let removeCallback: IOServiceMatchingCallback = { (refcon, iterator) in
            var service = IOIteratorNext(iterator)
            while service != 0 {
                IOObjectRelease(service)
                service = IOIteratorNext(iterator)
            }
            
            DispatchQueue.main.async {
                NotificationCenter.default.post(name: .usbDeviceListChanged, object: nil)
            }
        }
        
        result = IOServiceAddMatchingNotification(
            notificationPort,
            kIOTerminatedNotification,
            matchingDictCopy as CFDictionary,
            removeCallback,
            nil,
            &removedIterator
        )
        
        if result != KERN_SUCCESS {
            logger.error("Failed to add device removed notification: \(result)")
        }
        
        // Drain initial iterator
        service = IOIteratorNext(removedIterator)
        while service != 0 {
            IOObjectRelease(service)
            service = IOIteratorNext(removedIterator)
        }
    }
    
    // MARK: - Device Management
    
    /// Mark a device as attached (forwarding to server)
    func attachDevice(_ device: MacUsbDevice) {
        attachedDevices.insert(device.id)
        deviceStates[device.id] = .attached
        logger.info("Device attached locally: \(device.displayName)")
    }
    
    /// Mark a device as detached (stop forwarding)
    func detachDevice(_ device: MacUsbDevice) {
        attachedDevices.remove(device.id)
        deviceStates[device.id] = .idle
        logger.info("Device detached locally: \(device.displayName)")
    }
    
    /// Check if a device is attached
    func isDeviceAttached(_ device: MacUsbDevice) -> Bool {
        return attachedDevices.contains(device.id)
    }
    
    /// Get device state
    func getDeviceState(_ device: MacUsbDevice) -> DeviceForwardingState {
        return deviceStates[device.id] ?? .idle
    }
    
    /// Set device error state
    func setDeviceError(_ device: MacUsbDevice, message: String) {
        deviceStates[device.id] = .error(message)
    }
    
    // MARK: - USB Communication
    
    /// Get device descriptor
    func getDeviceDescriptor(_ device: MacUsbDevice) -> Data? {
        // In a full implementation, this would use IOUSBDeviceInterface to get the descriptor
        // For now, return a basic device descriptor structure
        var descriptor = Data(count: 18)
        descriptor[0] = 18  // bLength
        descriptor[1] = 1   // bDescriptorType (Device)
        descriptor[2] = 0x00  // bcdUSB low
        descriptor[3] = 0x02  // bcdUSB high (USB 2.0)
        descriptor[4] = device.deviceClass
        descriptor[5] = device.deviceSubclass
        descriptor[6] = device.deviceProtocol
        descriptor[7] = 64  // bMaxPacketSize0
        descriptor[8] = UInt8(device.vendorId & 0xFF)
        descriptor[9] = UInt8(device.vendorId >> 8)
        descriptor[10] = UInt8(device.productId & 0xFF)
        descriptor[11] = UInt8(device.productId >> 8)
        descriptor[12] = 0x00  // bcdDevice low
        descriptor[13] = 0x01  // bcdDevice high
        descriptor[14] = 1  // iManufacturer
        descriptor[15] = 2  // iProduct
        descriptor[16] = 3  // iSerialNumber
        descriptor[17] = 1  // bNumConfigurations
        return descriptor
    }
    
    /// Get configuration descriptor
    func getConfigDescriptor(_ device: MacUsbDevice) -> Data? {
        // In a full implementation, this would use IOUSBDeviceInterface
        // For now, return a minimal config descriptor
        var descriptor = Data(count: 9)
        descriptor[0] = 9   // bLength
        descriptor[1] = 2   // bDescriptorType (Configuration)
        descriptor[2] = 9   // wTotalLength low
        descriptor[3] = 0   // wTotalLength high
        descriptor[4] = 0   // bNumInterfaces
        descriptor[5] = 1   // bConfigurationValue
        descriptor[6] = 0   // iConfiguration
        descriptor[7] = 0x80  // bmAttributes (Bus-powered)
        descriptor[8] = 250  // bMaxPower (500mA)
        return descriptor
    }
    
    // MARK: - Cleanup
    
    /// Cleanup resources
    func cleanup() {
        if addedIterator != 0 {
            IOObjectRelease(addedIterator)
            addedIterator = 0
        }
        if removedIterator != 0 {
            IOObjectRelease(removedIterator)
            removedIterator = 0
        }
        if let notificationPort = notificationPort {
            IONotificationPortDestroy(notificationPort)
            self.notificationPort = nil
        }
        
        attachedDevices.removeAll()
        deviceStates.removeAll()
    }
}

// MARK: - Notification Names

extension Notification.Name {
    static let usbDeviceListChanged = Notification.Name("usbDeviceListChanged")
}

// MARK: - USB Constants (if not available from IOKit)

let kUSBVendorID = "idVendor"
let kUSBProductID = "idProduct"
let kUSBDeviceClass = "bDeviceClass"
let kUSBDeviceSubClass = "bDeviceSubClass"
let kUSBDeviceProtocol = "bDeviceProtocol"
let kUSBSerialNumberString = "USB Serial Number"
let kUSBDevicePropertyLocationID = "locationID"
