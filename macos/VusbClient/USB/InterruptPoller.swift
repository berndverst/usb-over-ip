//
//  InterruptPoller.swift
//  VusbClient
//
//  Interrupt Endpoint Polling for macOS
//  Continuously polls interrupt IN endpoints for game controllers and other HID devices
//

import Foundation
import IOKit
import IOKit.usb
import Combine
import os.log

/// Data received from an interrupt endpoint
struct InterruptData: Equatable {
    let deviceId: Int
    let endpointAddress: UInt8
    let data: Data
    let timestamp: Date
    
    static func == (lhs: InterruptData, rhs: InterruptData) -> Bool {
        return lhs.deviceId == rhs.deviceId &&
               lhs.endpointAddress == rhs.endpointAddress &&
               lhs.data == rhs.data
    }
}

/// Interrupt endpoint information
struct InterruptEndpoint {
    let address: UInt8
    let maxPacketSize: UInt16
    let interval: UInt8
    let pipeRef: UInt8
}

/// Interrupt Endpoint Poller
/// Polls interrupt IN endpoints for HID devices (game controllers, mice, keyboards)
class InterruptPoller {
    
    private let logger = Logger(subsystem: "com.vusb.client", category: "InterruptPoller")
    
    // Configuration
    private let defaultPollTimeoutMs: UInt32 = 50 // 50ms default timeout
    
    // State
    private var pollingTasks: [Int: Task<Void, Never>] = [:]
    private var polledEndpoints: [Int: [InterruptEndpoint]] = [:]
    private var deviceInterfaces: [Int: UnsafeMutablePointer<UnsafeMutablePointer<IOUSBInterfaceInterface>?>] = [:]
    
    // Publisher for interrupt data
    private let interruptDataSubject = PassthroughSubject<InterruptData, Never>()
    var interruptDataPublisher: AnyPublisher<InterruptData, Never> {
        interruptDataSubject.eraseToAnyPublisher()
    }
    
    // Track last data to detect changes (reduces network traffic)
    private var lastData: [String: Data] = [:]
    private let lastDataLock = NSLock()
    
    /// Start polling interrupt endpoints for a device
    /// - Parameters:
    ///   - deviceId: The device ID
    ///   - service: The IOKit service for the device
    /// - Returns: Number of interrupt endpoints being polled
    func startPolling(deviceId: Int, service: io_service_t) -> Int {
        // Check if already polling
        if pollingTasks[deviceId] != nil {
            logger.warning("Already polling device: \(deviceId)")
            return polledEndpoints[deviceId]?.count ?? 0
        }
        
        // Get device interface
        guard let deviceInterface = getDeviceInterface(service: service) else {
            logger.error("Failed to get device interface for device: \(deviceId)")
            return 0
        }
        
        // Open device
        let openResult = deviceInterface.pointee?.pointee.USBDeviceOpen(deviceInterface)
        guard openResult == kIOReturnSuccess || openResult == kIOReturnExclusiveAccess else {
            logger.error("Failed to open USB device: \(String(describing: openResult))")
            return 0
        }
        
        // Find all interface interfaces with interrupt endpoints
        let endpoints = findInterruptEndpoints(deviceInterface: deviceInterface, deviceId: deviceId)
        
        if endpoints.isEmpty {
            logger.debug("No interrupt IN endpoints found for device: \(deviceId)")
            deviceInterface.pointee?.pointee.USBDeviceClose(deviceInterface)
            return 0
        }
        
        logger.info("Found \(endpoints.count) interrupt IN endpoint(s) for device \(deviceId)")
        endpoints.forEach { ep in
            logger.debug("  - Endpoint 0x\(String(ep.address, radix: 16)), interval=\(ep.interval)ms, maxPacket=\(ep.maxPacketSize)")
        }
        
        polledEndpoints[deviceId] = endpoints
        
        // Start polling task
        let task = Task { [weak self] in
            await self?.pollDevice(deviceId: deviceId, deviceInterface: deviceInterface, endpoints: endpoints)
        }
        pollingTasks[deviceId] = task
        
        return endpoints.count
    }
    
    /// Stop polling for a device
    func stopPolling(deviceId: Int) {
        pollingTasks[deviceId]?.cancel()
        pollingTasks.removeValue(forKey: deviceId)
        polledEndpoints.removeValue(forKey: deviceId)
        
        // Close and cleanup device interface
        if let interface = deviceInterfaces.removeValue(forKey: deviceId) {
            interface.pointee?.pointee.USBInterfaceClose(interface)
            _ = interface.pointee?.pointee.Release(interface)
        }
        
        // Clear last data cache
        lastDataLock.lock()
        lastData = lastData.filter { !$0.key.hasPrefix("\(deviceId)-") }
        lastDataLock.unlock()
        
        logger.debug("Stopped polling device: \(deviceId)")
    }
    
    /// Stop all polling
    func stopAll() {
        let deviceIds = Array(pollingTasks.keys)
        deviceIds.forEach { stopPolling(deviceId: $0) }
        logger.info("Stopped all interrupt polling")
    }
    
    /// Check if a device is being polled
    func isPolling(deviceId: Int) -> Bool {
        return pollingTasks[deviceId] != nil
    }
    
    /// Get list of polled endpoints for a device
    func getPolledEndpoints(deviceId: Int) -> [InterruptEndpoint] {
        return polledEndpoints[deviceId] ?? []
    }
    
    // MARK: - Private Methods
    
    /// Get IOUSBDeviceInterface for a service
    private func getDeviceInterface(service: io_service_t) -> UnsafeMutablePointer<UnsafeMutablePointer<IOUSBDeviceInterface>?>? {
        var score: Int32 = 0
        var pluginInterface: UnsafeMutablePointer<UnsafeMutablePointer<IOCFPlugInInterface>?>?
        
        let pluginResult = IOCreatePlugInInterfaceForService(
            service,
            kIOUSBDeviceUserClientTypeID,
            kIOCFPlugInInterfaceID,
            &pluginInterface,
            &score
        )
        
        guard pluginResult == kIOReturnSuccess,
              let plugin = pluginInterface else {
            logger.error("Failed to create plugin interface: \(pluginResult)")
            return nil
        }
        
        defer {
            _ = plugin.pointee?.pointee.Release(plugin)
        }
        
        var deviceInterface: UnsafeMutableRawPointer?
        
        let queryResult = withUnsafeMutablePointer(to: &deviceInterface) { ptr in
            plugin.pointee?.pointee.QueryInterface(
                plugin,
                CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID),
                ptr
            )
        }
        
        guard queryResult == S_OK,
              let interface = deviceInterface else {
            logger.error("Failed to query device interface")
            return nil
        }
        
        return interface.assumingMemoryBound(to: UnsafeMutablePointer<IOUSBDeviceInterface>?.self)
    }
    
    /// Find all interrupt IN endpoints on a device
    private func findInterruptEndpoints(
        deviceInterface: UnsafeMutablePointer<UnsafeMutablePointer<IOUSBDeviceInterface>?>,
        deviceId: Int
    ) -> [InterruptEndpoint] {
        var endpoints: [InterruptEndpoint] = []
        
        // Iterate through interfaces to find interrupt endpoints
        var request = IOUSBFindInterfaceRequest(
            bInterfaceClass: UInt16(kIOUSBFindInterfaceDontCare),
            bInterfaceSubClass: UInt16(kIOUSBFindInterfaceDontCare),
            bInterfaceProtocol: UInt16(kIOUSBFindInterfaceDontCare),
            bAlternateSetting: UInt16(kIOUSBFindInterfaceDontCare)
        )
        
        var interfaceIterator: io_iterator_t = 0
        let findResult = deviceInterface.pointee?.pointee.CreateInterfaceIterator(deviceInterface, &request, &interfaceIterator)
        
        guard findResult == kIOReturnSuccess else {
            logger.error("Failed to create interface iterator: \(String(describing: findResult))")
            return endpoints
        }
        
        defer { IOObjectRelease(interfaceIterator) }
        
        var interfaceService = IOIteratorNext(interfaceIterator)
        while interfaceService != 0 {
            defer {
                IOObjectRelease(interfaceService)
                interfaceService = IOIteratorNext(interfaceIterator)
            }
            
            // Get interface interface
            guard let interfaceInterface = getInterfaceInterface(service: interfaceService) else {
                continue
            }
            
            // Open interface
            let openResult = interfaceInterface.pointee?.pointee.USBInterfaceOpen(interfaceInterface)
            if openResult != kIOReturnSuccess && openResult != kIOReturnExclusiveAccess {
                _ = interfaceInterface.pointee?.pointee.Release(interfaceInterface)
                continue
            }
            
            // Get number of endpoints
            var numEndpoints: UInt8 = 0
            interfaceInterface.pointee?.pointee.GetNumEndpoints(interfaceInterface, &numEndpoints)
            
            // Check each endpoint
            for pipeRef in 1...numEndpoints {
                var direction: UInt8 = 0
                var number: UInt8 = 0
                var transferType: UInt8 = 0
                var maxPacketSize: UInt16 = 0
                var interval: UInt8 = 0
                
                let pipeResult = interfaceInterface.pointee?.pointee.GetPipeProperties(
                    interfaceInterface,
                    pipeRef,
                    &direction,
                    &number,
                    &transferType,
                    &maxPacketSize,
                    &interval
                )
                
                if pipeResult == kIOReturnSuccess {
                    // Check for interrupt IN endpoint (direction 1 = IN, transfer type 3 = interrupt)
                    if direction == kUSBIn && transferType == kUSBInterrupt {
                        let address = UInt8(0x80 | number) // Set IN direction bit
                        endpoints.append(InterruptEndpoint(
                            address: address,
                            maxPacketSize: maxPacketSize,
                            interval: interval,
                            pipeRef: pipeRef
                        ))
                        
                        // Store interface for this device
                        deviceInterfaces[deviceId] = interfaceInterface
                    }
                }
            }
            
            // If no endpoints found on this interface, close and release it
            if endpoints.isEmpty {
                interfaceInterface.pointee?.pointee.USBInterfaceClose(interfaceInterface)
                _ = interfaceInterface.pointee?.pointee.Release(interfaceInterface)
            }
        }
        
        return endpoints
    }
    
    /// Get IOUSBInterfaceInterface for a service
    private func getInterfaceInterface(service: io_service_t) -> UnsafeMutablePointer<UnsafeMutablePointer<IOUSBInterfaceInterface>?>? {
        var score: Int32 = 0
        var pluginInterface: UnsafeMutablePointer<UnsafeMutablePointer<IOCFPlugInInterface>?>?
        
        let pluginResult = IOCreatePlugInInterfaceForService(
            service,
            kIOUSBInterfaceUserClientTypeID,
            kIOCFPlugInInterfaceID,
            &pluginInterface,
            &score
        )
        
        guard pluginResult == kIOReturnSuccess,
              let plugin = pluginInterface else {
            return nil
        }
        
        defer {
            _ = plugin.pointee?.pointee.Release(plugin)
        }
        
        var interfaceInterface: UnsafeMutableRawPointer?
        
        let queryResult = withUnsafeMutablePointer(to: &interfaceInterface) { ptr in
            plugin.pointee?.pointee.QueryInterface(
                plugin,
                CFUUIDGetUUIDBytes(kIOUSBInterfaceInterfaceID),
                ptr
            )
        }
        
        guard queryResult == S_OK,
              let interface = interfaceInterface else {
            return nil
        }
        
        return interface.assumingMemoryBound(to: UnsafeMutablePointer<IOUSBInterfaceInterface>?.self)
    }
    
    /// Main polling loop for a device
    private func pollDevice(
        deviceId: Int,
        deviceInterface: UnsafeMutablePointer<UnsafeMutablePointer<IOUSBDeviceInterface>?>,
        endpoints: [InterruptEndpoint]
    ) async {
        guard let interfaceInterface = deviceInterfaces[deviceId] else {
            logger.error("No interface interface for device: \(deviceId)")
            return
        }
        
        logger.info("Starting interrupt polling for device: \(deviceId)")
        
        do {
            while !Task.isCancelled {
                for endpoint in endpoints {
                    if Task.isCancelled { break }
                    
                    // Allocate buffer
                    var buffer = [UInt8](repeating: 0, count: Int(endpoint.maxPacketSize))
                    var bytesRead: UInt32 = UInt32(endpoint.maxPacketSize)
                    
                    // Read from pipe (this is a blocking call with timeout)
                    let readResult = interfaceInterface.pointee?.pointee.ReadPipeTO(
                        interfaceInterface,
                        endpoint.pipeRef,
                        &buffer,
                        &bytesRead,
                        defaultPollTimeoutMs,
                        defaultPollTimeoutMs
                    )
                    
                    if readResult == kIOReturnSuccess && bytesRead > 0 {
                        let data = Data(buffer.prefix(Int(bytesRead)))
                        
                        // Check if data changed (reduces network traffic)
                        let cacheKey = "\(deviceId)-\(endpoint.address)"
                        var shouldEmit = true
                        
                        lastDataLock.lock()
                        if let lastBytes = lastData[cacheKey], lastBytes == data {
                            shouldEmit = false
                        } else {
                            lastData[cacheKey] = data
                        }
                        lastDataLock.unlock()
                        
                        if shouldEmit {
                            let interruptData = InterruptData(
                                deviceId: deviceId,
                                endpointAddress: endpoint.address,
                                data: data,
                                timestamp: Date()
                            )
                            interruptDataSubject.send(interruptData)
                        }
                    }
                    // kIOReturnTimeout is normal (no data available)
                    // Other errors should be logged
                    else if readResult != kIOReturnTimeout && readResult != kIOReturnAborted {
                        logger.debug("Read error on endpoint 0x\(String(endpoint.address, radix: 16)): \(String(describing: readResult))")
                    }
                }
                
                // Yield to other tasks
                await Task.yield()
            }
        } catch {
            if !Task.isCancelled {
                logger.error("Error polling device \(deviceId): \(error.localizedDescription)")
            }
        }
        
        logger.debug("Polling stopped for device: \(deviceId)")
        
        // Cleanup
        deviceInterface.pointee?.pointee.USBDeviceClose(deviceInterface)
    }
}
