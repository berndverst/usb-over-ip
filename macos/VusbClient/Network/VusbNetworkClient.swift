//
//  VusbNetworkClient.swift
//  VusbClient
//
//  Network client for VUSB protocol communication
//

import Foundation
import Network
import Combine
import os.log

/// Connection state for the VUSB client
enum ConnectionState: Equatable {
    case disconnected
    case connecting
    case connected(serverAddress: String)
    case error(message: String)
    
    var isConnected: Bool {
        if case .connected = self { return true }
        return false
    }
}

/// Network client for VUSB protocol
@MainActor
class VusbNetworkClient: ObservableObject {
    
    // MARK: - Constants
    
    private static let connectTimeout: TimeInterval = 10.0
    private static let readTimeout: TimeInterval = 30.0
    private static let keepAliveInterval: TimeInterval = 10.0
    
    private let logger = Logger(subsystem: "com.vusb.client", category: "Network")
    
    // MARK: - Published Properties
    
    @Published private(set) var connectionState: ConnectionState = .disconnected
    @Published private(set) var lastError: String?
    
    // MARK: - Private Properties
    
    private var connection: NWConnection?
    private var sequenceNumber: UInt32 = 0
    private var isRunning = false
    
    private var keepAliveTimer: Timer?
    private var pendingRequests: [UInt32: CheckedContinuation<(VusbHeader, Data), Error>] = [:]
    
    // Callbacks
    var onMessageReceived: ((VusbHeader, Data) -> Void)?
    var onUrbSubmit: ((UrbSubmit) -> UrbComplete?)?
    var onDisconnected: (() -> Void)?
    
    // MARK: - Connection Management
    
    /// Connect to VUSB server
    func connect(serverAddress: String, port: UInt16 = VUSB_DEFAULT_PORT, clientName: String = "macOSClient") async -> Bool {
        guard !isRunning else {
            logger.warning("Already connected or connecting")
            return false
        }
        
        connectionState = .connecting
        
        let host = NWEndpoint.Host(serverAddress)
        let port = NWEndpoint.Port(rawValue: port)!
        
        let parameters = NWParameters.tcp
        parameters.allowLocalEndpointReuse = true
        
        connection = NWConnection(host: host, port: port, using: parameters)
        
        return await withCheckedContinuation { continuation in
            connection?.stateUpdateHandler = { [weak self] state in
                guard let self = self else { return }
                
                Task { @MainActor in
                    switch state {
                    case .ready:
                        self.logger.info("Connected to \(serverAddress):\(port.rawValue)")
                        self.isRunning = true
                        
                        // Send connect message
                        do {
                            let connectMsg = ConnectMessage(clientName: clientName)
                            try await self.sendMessage(command: .connect, payload: connectMsg.toData())
                            
                            // Wait for connect response (server echoes CONNECT command with status)
                            let (header, _) = try await self.receiveMessage()
                            
                            if header.commandType == .connect {
                                self.connectionState = .connected(serverAddress: serverAddress)
                                self.startReceiveLoop()
                                self.startKeepAlive()
                                continuation.resume(returning: true)
                            } else {
                                throw NSError(domain: "VusbClient", code: -1, 
                                            userInfo: [NSLocalizedDescriptionKey: "Expected CONNECT response"])
                            }
                        } catch {
                            self.logger.error("Connection handshake failed: \(error.localizedDescription)")
                            self.connectionState = .error(message: error.localizedDescription)
                            self.disconnect()
                            continuation.resume(returning: false)
                        }
                        
                    case .failed(let error):
                        self.logger.error("Connection failed: \(error.localizedDescription)")
                        self.connectionState = .error(message: error.localizedDescription)
                        self.isRunning = false
                        continuation.resume(returning: false)
                        
                    case .cancelled:
                        self.logger.info("Connection cancelled")
                        self.connectionState = .disconnected
                        self.isRunning = false
                        
                    case .waiting(let error):
                        self.logger.warning("Connection waiting: \(error.localizedDescription)")
                        
                    default:
                        break
                    }
                }
            }
            
            connection?.start(queue: .global(qos: .userInitiated))
        }
    }
    
    /// Disconnect from server
    func disconnect() {
        guard isRunning else { return }
        
        logger.info("Disconnecting...")
        isRunning = false
        
        // Stop keep-alive timer
        keepAliveTimer?.invalidate()
        keepAliveTimer = nil
        
        // Send disconnect message (best effort)
        Task {
            try? await sendMessage(command: .disconnect, payload: Data())
        }
        
        // Cancel connection
        connection?.cancel()
        connection = nil
        
        // Clear pending requests
        for (_, continuation) in pendingRequests {
            continuation.resume(throwing: NSError(domain: "VusbClient", code: -2,
                                                  userInfo: [NSLocalizedDescriptionKey: "Disconnected"]))
        }
        pendingRequests.removeAll()
        
        connectionState = .disconnected
        onDisconnected?()
        
        logger.info("Disconnected")
    }
    
    // MARK: - Message Sending
    
    /// Send a protocol message
    func sendMessage(command: VusbCommand, payload: Data) async throws {
        guard let connection = connection, isRunning else {
            throw NSError(domain: "VusbClient", code: -3,
                         userInfo: [NSLocalizedDescriptionKey: "Not connected"])
        }
        
        sequenceNumber += 1
        let header = VusbHeader(command: command, length: UInt32(payload.count), sequence: sequenceNumber)
        
        var messageData = header.toData()
        messageData.append(payload)
        
        logger.debug("Sending message: cmd=0x\(String(format: "%04X", command.rawValue)), len=\(payload.count), seq=\(self.sequenceNumber)")
        
        return try await withCheckedThrowingContinuation { continuation in
            connection.send(content: messageData, completion: .contentProcessed { error in
                if let error = error {
                    continuation.resume(throwing: error)
                } else {
                    continuation.resume()
                }
            })
        }
    }
    
    /// Send message and wait for response
    func sendMessageWithResponse(command: VusbCommand, payload: Data) async throws -> (VusbHeader, Data) {
        try await sendMessage(command: command, payload: payload)
        return try await receiveMessage()
    }
    
    // MARK: - Message Receiving
    
    /// Receive a single message
    private func receiveMessage() async throws -> (VusbHeader, Data) {
        guard let connection = connection else {
            throw NSError(domain: "VusbClient", code: -3,
                         userInfo: [NSLocalizedDescriptionKey: "Not connected"])
        }
        
        // Receive header
        let headerData = try await receiveExact(connection: connection, length: VUSB_HEADER_SIZE)
        
        guard let header = VusbHeader.fromData(headerData) else {
            throw NSError(domain: "VusbClient", code: -4,
                         userInfo: [NSLocalizedDescriptionKey: "Invalid header"])
        }
        
        // Validate magic
        guard header.magic == VUSB_PROTOCOL_MAGIC else {
            throw NSError(domain: "VusbClient", code: -5,
                         userInfo: [NSLocalizedDescriptionKey: "Invalid magic number"])
        }
        
        // Receive payload
        var payload = Data()
        if header.length > 0 {
            payload = try await receiveExact(connection: connection, length: Int(header.length))
        }
        
        logger.debug("Received message: cmd=0x\(String(format: "%04X", header.command)), len=\(header.length), seq=\(header.sequence)")
        
        return (header, payload)
    }
    
    /// Receive exact number of bytes
    private func receiveExact(connection: NWConnection, length: Int) async throws -> Data {
        return try await withCheckedThrowingContinuation { continuation in
            connection.receive(minimumIncompleteLength: length, maximumLength: length) { content, _, isComplete, error in
                if let error = error {
                    continuation.resume(throwing: error)
                } else if let data = content {
                    continuation.resume(returning: data)
                } else if isComplete {
                    continuation.resume(throwing: NSError(domain: "VusbClient", code: -6,
                                                         userInfo: [NSLocalizedDescriptionKey: "Connection closed"]))
                } else {
                    continuation.resume(returning: Data())
                }
            }
        }
    }
    
    // MARK: - Background Tasks
    
    /// Start the receive loop for incoming messages
    private func startReceiveLoop() {
        Task.detached { [weak self] in
            while let self = self, await self.isRunning {
                do {
                    let (header, payload) = try await self.receiveMessage()
                    await self.handleMessage(header: header, payload: payload)
                } catch {
                    await MainActor.run {
                        if self.isRunning {
                            self.logger.error("Receive error: \(error.localizedDescription)")
                            self.disconnect()
                        }
                    }
                    break
                }
            }
        }
    }
    
    /// Handle received message
    @MainActor
    private func handleMessage(header: VusbHeader, payload: Data) {
        switch header.commandType {
        case .ping:
            // Respond with pong
            Task {
                try? await sendMessage(command: .pong, payload: Data())
            }
            
        case .pong:
            // Response to our ping - connection is alive
            logger.debug("Received pong from server")
            break
            
        case .urbSubmit:
            // Handle URB request from server
            if let urb = UrbSubmit.fromData(payload) {
                if let response = onUrbSubmit?(urb) {
                    Task {
                        try? await sendMessage(command: .urbComplete, payload: response.toData())
                    }
                }
            }
            
        case .error:
            // Handle error message
            if let errorMessage = String(data: payload, encoding: .utf8) {
                logger.error("Server error: \(errorMessage)")
                lastError = errorMessage
            }
            
        default:
            // Pass to general handler
            onMessageReceived?(header, payload)
        }
    }
    
    /// Start keep-alive timer
    private func startKeepAlive() {
        keepAliveTimer = Timer.scheduledTimer(withTimeInterval: Self.keepAliveInterval, repeats: true) { [weak self] _ in
            guard let self = self else { return }
            Task {
                try? await self.sendMessage(command: .ping, payload: Data())
            }
        }
    }
    
    // MARK: - Device Operations
    
    /// Attach a device to the server
    /// Sends VUSB_DEVICE_INFO (208 bytes) + descriptorLength (4 bytes) + descriptors
    func attachDevice(_ device: VusbDeviceInfo) async throws {
        var payload = device.toData()
        
        // Combine device and config descriptors
        var descriptors = Data()
        descriptors.append(device.deviceDescriptor)
        descriptors.append(device.configDescriptor)
        
        // Append descriptor length (4 bytes) and descriptor data
        payload.append(contentsOf: withUnsafeBytes(of: UInt32(descriptors.count).littleEndian) { Array($0) })
        payload.append(descriptors)
        
        try await sendMessage(command: .deviceAttach, payload: payload)
        logger.info("Device attached: \(device.displayName)")
    }
    
    /// Detach a device from the server
    func detachDevice(deviceId: Int32) async throws {
        var data = Data()
        data.append(contentsOf: withUnsafeBytes(of: deviceId.littleEndian) { Array($0) })
        try await sendMessage(command: .deviceDetach, payload: data)
        logger.info("Device detached: \(deviceId)")
    }
    
    /// Send URB completion
    func sendUrbComplete(_ completion: UrbComplete) async throws {
        try await sendMessage(command: .urbComplete, payload: completion.toData())
    }
}

// MARK: - Async extension for isRunning check

extension VusbNetworkClient {
    nonisolated var isRunning: Bool {
        get async {
            await MainActor.run { self.isRunning }
        }
    }
    
    private func setIsRunning(_ value: Bool) {
        self.isRunning = value
    }
}
