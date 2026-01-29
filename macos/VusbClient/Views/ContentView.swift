//
//  ContentView.swift
//  VusbClient
//
//  Main content view for the VUSB Client app
//

import SwiftUI

struct ContentView: View {
    
    @EnvironmentObject var networkClient: VusbNetworkClient
    @EnvironmentObject var usbManager: UsbDeviceManager
    @EnvironmentObject var settings: AppSettings
    @EnvironmentObject var logger: ActivityLogger
    
    @State private var selectedTab: Tab = .devices
    @State private var showingSettings = false
    @State private var showingConnectSheet = false
    
    enum Tab: String, CaseIterable {
        case devices = "Devices"
        case activity = "Activity"
    }
    
    var body: some View {
        NavigationSplitView {
            // Sidebar
            VStack(spacing: 0) {
                // Connection Status
                connectionStatusView
                    .padding()
                
                Divider()
                
                // Tab Selection
                List(Tab.allCases, id: \.self, selection: $selectedTab) { tab in
                    Label(tab.rawValue, systemImage: tabIcon(for: tab))
                        .tag(tab)
                }
                .listStyle(.sidebar)
            }
            .frame(minWidth: 200)
        } detail: {
            // Main Content
            switch selectedTab {
            case .devices:
                DeviceListView()
            case .activity:
                ActivityLogView()
            }
        }
        .toolbar {
            ToolbarItemGroup(placement: .primaryAction) {
                // Connect/Disconnect Button
                Button(action: toggleConnection) {
                    if networkClient.connectionState.isConnected {
                        Label("Disconnect", systemImage: "xmark.circle")
                    } else {
                        Label("Connect", systemImage: "link")
                    }
                }
                .help(networkClient.connectionState.isConnected ? "Disconnect from server" : "Connect to server")
                
                // Refresh Button
                Button(action: refreshDevices) {
                    Label("Refresh", systemImage: "arrow.clockwise")
                }
                .help("Refresh device list")
                .keyboardShortcut("r", modifiers: .command)
                
                // Settings Button
                Button(action: { showingSettings = true }) {
                    Label("Settings", systemImage: "gear")
                }
                .help("Open settings")
                .keyboardShortcut(",", modifiers: .command)
            }
        }
        .sheet(isPresented: $showingConnectSheet) {
            ConnectSheet()
        }
        .sheet(isPresented: $showingSettings) {
            SettingsView()
        }
        .onAppear {
            // Auto-connect if enabled
            if settings.autoConnect {
                Task {
                    await connect()
                }
            }
        }
        .onReceive(NotificationCenter.default.publisher(for: .usbDeviceListChanged)) { _ in
            usbManager.refreshDeviceList()
        }
    }
    
    // MARK: - Connection Status View
    
    private var connectionStatusView: some View {
        VStack(spacing: 8) {
            // Status indicator
            HStack {
                Circle()
                    .fill(statusColor)
                    .frame(width: 10, height: 10)
                
                Text(statusText)
                    .font(.headline)
                
                Spacer()
            }
            
            // Server address (if connected)
            if case .connected(let address) = networkClient.connectionState {
                HStack {
                    Image(systemName: "server.rack")
                        .foregroundColor(.secondary)
                    Text(address)
                        .font(.caption)
                        .foregroundColor(.secondary)
                    Spacer()
                }
            }
            
            // Error message (if any)
            if case .error(let message) = networkClient.connectionState {
                Text(message)
                    .font(.caption)
                    .foregroundColor(.red)
                    .lineLimit(2)
            }
        }
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
    
    private func tabIcon(for tab: Tab) -> String {
        switch tab {
        case .devices: return "cable.connector.horizontal"
        case .activity: return "list.bullet.rectangle"
        }
    }
    
    // MARK: - Actions
    
    private func toggleConnection() {
        if networkClient.connectionState.isConnected {
            networkClient.disconnect()
            logger.info("Disconnected from server", source: "Connection")
        } else {
            showingConnectSheet = true
        }
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
    
    private func refreshDevices() {
        usbManager.refreshDeviceList()
        logger.info("Device list refreshed", source: "USB")
    }
}

// MARK: - Connect Sheet

struct ConnectSheet: View {
    
    @EnvironmentObject var networkClient: VusbNetworkClient
    @EnvironmentObject var settings: AppSettings
    @EnvironmentObject var logger: ActivityLogger
    
    @Environment(\.dismiss) var dismiss
    
    @State private var serverAddress: String = ""
    @State private var serverPort: String = ""
    @State private var isConnecting = false
    
    var body: some View {
        VStack(spacing: 20) {
            Text("Connect to Server")
                .font(.title2)
                .fontWeight(.semibold)
            
            Form {
                TextField("Server Address", text: $serverAddress)
                    .textFieldStyle(.roundedBorder)
                
                TextField("Port", text: $serverPort)
                    .textFieldStyle(.roundedBorder)
            }
            .formStyle(.grouped)
            
            HStack {
                Button("Cancel") {
                    dismiss()
                }
                .keyboardShortcut(.escape)
                
                Spacer()
                
                Button("Connect") {
                    Task {
                        await connect()
                    }
                }
                .keyboardShortcut(.return)
                .disabled(serverAddress.isEmpty || isConnecting)
                .buttonStyle(.borderedProminent)
            }
        }
        .padding()
        .frame(width: 350, height: 200)
        .onAppear {
            serverAddress = settings.serverAddress
            serverPort = String(settings.serverPort)
        }
    }
    
    private func connect() async {
        isConnecting = true
        
        // Save settings
        settings.serverAddress = serverAddress
        settings.serverPort = UInt16(serverPort) ?? VUSB_DEFAULT_PORT
        
        let success = await networkClient.connect(
            serverAddress: settings.serverAddress,
            port: settings.serverPort,
            clientName: "macOSClient"
        )
        
        isConnecting = false
        
        if success {
            logger.info("Connected to \(settings.serverAddress):\(settings.serverPort)", source: "Connection")
            dismiss()
        } else {
            logger.error("Failed to connect to server", source: "Connection")
        }
    }
}

// MARK: - Activity Log View

struct ActivityLogView: View {
    
    @EnvironmentObject var logger: ActivityLogger
    
    var body: some View {
        VStack(spacing: 0) {
            // Toolbar
            HStack {
                Text("Activity Log")
                    .font(.headline)
                
                Spacer()
                
                Button(action: { logger.clear() }) {
                    Label("Clear", systemImage: "trash")
                }
                .buttonStyle(.borderless)
            }
            .padding()
            
            Divider()
            
            // Log entries
            if logger.entries.isEmpty {
                ContentUnavailableView(
                    "No Activity",
                    systemImage: "list.bullet.rectangle",
                    description: Text("Activity will appear here")
                )
            } else {
                List(logger.entries) { entry in
                    LogEntryRow(entry: entry)
                }
                .listStyle(.plain)
            }
        }
    }
}

struct LogEntryRow: View {
    let entry: LogEntry
    
    var body: some View {
        HStack(alignment: .top, spacing: 8) {
            Image(systemName: entry.level.icon)
                .foregroundColor(entry.level.color)
                .frame(width: 20)
            
            VStack(alignment: .leading, spacing: 2) {
                Text(entry.message)
                    .font(.body)
                
                HStack {
                    Text(entry.formattedTimestamp)
                    Text("•")
                    Text(entry.source)
                }
                .font(.caption)
                .foregroundColor(.secondary)
            }
        }
        .padding(.vertical, 4)
    }
}

// MARK: - Preview

#Preview {
    ContentView()
        .environmentObject(VusbNetworkClient())
        .environmentObject(UsbDeviceManager())
        .environmentObject(AppSettings())
        .environmentObject(ActivityLogger())
}
