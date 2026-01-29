//
//  VusbClientApp.swift
//  VusbClient
//
//  Main entry point for the VUSB Client macOS app
//

import SwiftUI

@main
struct VusbClientApp: App {
    
    // MARK: - State Objects
    
    @StateObject private var networkClient = VusbNetworkClient()
    @StateObject private var usbManager = UsbDeviceManager()
    @StateObject private var settings = AppSettings()
    @StateObject private var logger = ActivityLogger()
    
    // MARK: - App Scene
    
    var body: some Scene {
        WindowGroup {
            ContentView()
                .environmentObject(networkClient)
                .environmentObject(usbManager)
                .environmentObject(settings)
                .environmentObject(logger)
                .preferredColorScheme(settings.theme.colorScheme)
                .frame(minWidth: 700, minHeight: 500)
                .onAppear {
                    setupCallbacks()
                }
        }
        .windowStyle(.automatic)
        .windowToolbarStyle(.unified)
        .commands {
            // App commands
            CommandGroup(replacing: .appInfo) {
                Button("About VUSB Client") {
                    NSApplication.shared.orderFrontStandardAboutPanel(
                        options: [
                            NSApplication.AboutPanelOptionKey.applicationName: "VUSB Client",
                            NSApplication.AboutPanelOptionKey.applicationVersion: "1.0.0",
                            NSApplication.AboutPanelOptionKey.credits: NSAttributedString(
                                string: "Virtual USB forwarding client for macOS",
                                attributes: [.font: NSFont.systemFont(ofSize: 11)]
                            )
                        ]
                    )
                }
            }
            
            // Connection commands
            CommandMenu("Connection") {
                Button("Connect") {
                    Task {
                        await connect()
                    }
                }
                .keyboardShortcut("n", modifiers: [.command, .shift])
                .disabled(networkClient.connectionState.isConnected)
                
                Button("Disconnect") {
                    networkClient.disconnect()
                }
                .keyboardShortcut("d", modifiers: [.command, .shift])
                .disabled(!networkClient.connectionState.isConnected)
                
                Divider()
                
                Button("Refresh Devices") {
                    usbManager.refreshDeviceList()
                }
                .keyboardShortcut("r", modifiers: .command)
            }
            
            // View commands
            CommandGroup(after: .sidebar) {
                Button("Show Activity Log") {
                    // This would need a way to switch tabs
                }
                .keyboardShortcut("l", modifiers: [.command, .option])
            }
        }
        
        // Settings window
        Settings {
            SettingsView()
                .environmentObject(settings)
        }
        
        // Menu bar extra
        MenuBarExtra("VUSB", systemImage: menuBarIcon) {
            MenuBarView()
                .environmentObject(networkClient)
                .environmentObject(usbManager)
                .environmentObject(settings)
        }
    }
    
    // MARK: - Menu Bar Icon
    
    private var menuBarIcon: String {
        switch networkClient.connectionState {
        case .connected:
            return "cable.connector.horizontal"
        case .connecting:
            return "antenna.radiowaves.left.and.right"
        case .error:
            return "exclamationmark.triangle"
        case .disconnected:
            return "cable.connector"
        }
    }
    
    // MARK: - Setup
    
    private func setupCallbacks() {
        // Setup network client callbacks
        networkClient.onDisconnected = { [weak logger] in
            Task { @MainActor in
                logger?.info("Disconnected from server", source: "Connection")
            }
        }
        
        networkClient.onMessageReceived = { [weak logger] header, payload in
            Task { @MainActor in
                logger?.debug("Received message: cmd=0x\(String(format: "%04X", header.command))", source: "Network")
            }
        }
        
        // Setup URB handler
        networkClient.onUrbSubmit = { [weak logger] urb in
            Task { @MainActor in
                logger?.debug("URB submit: id=\(urb.urbId), device=\(urb.deviceId)", source: "URB")
            }
            
            // In a full implementation, this would forward to the actual USB device
            // For now, return a basic response
            return UrbComplete(
                urbId: urb.urbId,
                status: UsbdStatus.success,
                actualLength: 0,
                data: Data()
            )
        }
        
        // Log startup
        logger.info("VUSB Client started", source: "App")
        logger.info("Found \(usbManager.availableDevices.count) USB devices", source: "USB")
    }
    
    private func connect() async {
        let success = await networkClient.connect(
            serverAddress: settings.serverAddress,
            port: settings.serverPort,
            clientName: "macOSClient"
        )
        
        if success {
            logger.info("Connected to \(settings.serverAddress):\(settings.serverPort)", source: "Connection")
        } else {
            logger.error("Failed to connect to server", source: "Connection")
        }
    }
}

// MARK: - Menu Bar View

struct MenuBarView: View {
    
    @EnvironmentObject var networkClient: VusbNetworkClient
    @EnvironmentObject var usbManager: UsbDeviceManager
    @EnvironmentObject var settings: AppSettings
    
    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            // Status header
            HStack {
                Circle()
                    .fill(statusColor)
                    .frame(width: 8, height: 8)
                
                Text(statusText)
                    .font(.headline)
                
                Spacer()
            }
            .padding(.horizontal)
            .padding(.vertical, 8)
            
            Divider()
            
            // Connected server
            if case .connected(let address) = networkClient.connectionState {
                HStack {
                    Image(systemName: "server.rack")
                    Text(address)
                    Spacer()
                }
                .font(.caption)
                .foregroundColor(.secondary)
                .padding(.horizontal)
                .padding(.vertical, 4)
                
                Divider()
            }
            
            // Device count
            HStack {
                Image(systemName: "cable.connector.horizontal")
                Text("\(usbManager.availableDevices.count) devices")
                Spacer()
                Text("\(usbManager.attachedDevices.count) attached")
                    .foregroundColor(.secondary)
            }
            .font(.caption)
            .padding(.horizontal)
            .padding(.vertical, 4)
            
            Divider()
            
            // Actions
            Button(action: toggleConnection) {
                HStack {
                    Image(systemName: networkClient.connectionState.isConnected ? "xmark.circle" : "link")
                    Text(networkClient.connectionState.isConnected ? "Disconnect" : "Connect")
                }
            }
            .buttonStyle(.plain)
            .padding(.horizontal)
            .padding(.vertical, 6)
            
            Button(action: { usbManager.refreshDeviceList() }) {
                HStack {
                    Image(systemName: "arrow.clockwise")
                    Text("Refresh Devices")
                }
            }
            .buttonStyle(.plain)
            .padding(.horizontal)
            .padding(.vertical, 6)
            
            Divider()
            
            // Open main window
            Button(action: openMainWindow) {
                HStack {
                    Image(systemName: "macwindow")
                    Text("Open VUSB Client")
                }
            }
            .buttonStyle(.plain)
            .padding(.horizontal)
            .padding(.vertical, 6)
            
            Divider()
            
            // Quit
            Button(action: { NSApplication.shared.terminate(nil) }) {
                HStack {
                    Image(systemName: "power")
                    Text("Quit")
                }
            }
            .buttonStyle(.plain)
            .padding(.horizontal)
            .padding(.vertical, 6)
        }
        .frame(width: 250)
    }
    
    private var statusColor: Color {
        switch networkClient.connectionState {
        case .disconnected: return .gray
        case .connecting: return .orange
        case .connected: return .green
        case .error: return .red
        }
    }
    
    private var statusText: String {
        switch networkClient.connectionState {
        case .disconnected: return "Disconnected"
        case .connecting: return "Connecting..."
        case .connected: return "Connected"
        case .error: return "Error"
        }
    }
    
    private func toggleConnection() {
        if networkClient.connectionState.isConnected {
            networkClient.disconnect()
        } else {
            Task {
                await networkClient.connect(
                    serverAddress: settings.serverAddress,
                    port: settings.serverPort,
                    clientName: "macOSClient"
                )
            }
        }
    }
    
    private func openMainWindow() {
        NSApplication.shared.activate(ignoringOtherApps: true)
        if let window = NSApplication.shared.windows.first {
            window.makeKeyAndOrderFront(nil)
        }
    }
}
