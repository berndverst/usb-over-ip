//
//  SettingsView.swift
//  VusbClient
//
//  Settings view for the VUSB Client app
//

import SwiftUI

struct SettingsView: View {
    
    @EnvironmentObject var settings: AppSettings
    @Environment(\.dismiss) var dismiss
    
    @State private var selectedTab: SettingsTab = .connection
    
    enum SettingsTab: String, CaseIterable {
        case connection = "Connection"
        case behavior = "Behavior"
        case appearance = "Appearance"
        case about = "About"
        
        var icon: String {
            switch self {
            case .connection: return "network"
            case .behavior: return "gearshape"
            case .appearance: return "paintbrush"
            case .about: return "info.circle"
            }
        }
    }
    
    var body: some View {
        TabView(selection: $selectedTab) {
            connectionSettings
                .tabItem {
                    Label(SettingsTab.connection.rawValue, systemImage: SettingsTab.connection.icon)
                }
                .tag(SettingsTab.connection)
            
            behaviorSettings
                .tabItem {
                    Label(SettingsTab.behavior.rawValue, systemImage: SettingsTab.behavior.icon)
                }
                .tag(SettingsTab.behavior)
            
            appearanceSettings
                .tabItem {
                    Label(SettingsTab.appearance.rawValue, systemImage: SettingsTab.appearance.icon)
                }
                .tag(SettingsTab.appearance)
            
            aboutView
                .tabItem {
                    Label(SettingsTab.about.rawValue, systemImage: SettingsTab.about.icon)
                }
                .tag(SettingsTab.about)
        }
        .frame(width: 500, height: 400)
        .padding()
    }
    
    // MARK: - Connection Settings
    
    private var connectionSettings: some View {
        Form {
            Section {
                TextField("Server Address", text: $settings.serverAddress)
                    .textFieldStyle(.roundedBorder)
                
                HStack {
                    Text("Port")
                    Spacer()
                    TextField("Port", value: $settings.serverPort, format: .number)
                        .textFieldStyle(.roundedBorder)
                        .frame(width: 100)
                }
            } header: {
                Text("Server")
            }
            
            Section {
                Stepper(value: $settings.keepAliveInterval, in: 5...60, step: 5) {
                    HStack {
                        Text("Keep-alive Interval")
                        Spacer()
                        Text("\(Int(settings.keepAliveInterval))s")
                            .foregroundColor(.secondary)
                    }
                }
            } header: {
                Text("Network")
            }
        }
        .formStyle(.grouped)
    }
    
    // MARK: - Behavior Settings
    
    private var behaviorSettings: some View {
        Form {
            Section {
                Toggle("Auto-connect on launch", isOn: $settings.autoConnect)
                Toggle("Auto-attach new devices", isOn: $settings.autoAttachDevices)
            } header: {
                Text("Automation")
            }
            
            Section {
                Toggle("Show notifications", isOn: $settings.showNotifications)
                Toggle("Launch at login", isOn: $settings.launchAtLogin)
            } header: {
                Text("System")
            }
            
            Section {
                Button("Reset to Defaults") {
                    settings.resetToDefaults()
                }
                .foregroundColor(.red)
            }
        }
        .formStyle(.grouped)
    }
    
    // MARK: - Appearance Settings
    
    private var appearanceSettings: some View {
        Form {
            Section {
                Picker("Theme", selection: $settings.theme) {
                    ForEach(AppTheme.allCases) { theme in
                        Text(theme.displayName).tag(theme)
                    }
                }
                .pickerStyle(.segmented)
            } header: {
                Text("Theme")
            }
        }
        .formStyle(.grouped)
    }
    
    // MARK: - About View
    
    private var aboutView: some View {
        VStack(spacing: 20) {
            // App icon
            Image(systemName: "cable.connector.horizontal")
                .font(.system(size: 64))
                .foregroundColor(.accentColor)
            
            // App name and version
            VStack(spacing: 4) {
                Text("VUSB Client")
                    .font(.title)
                    .fontWeight(.bold)
                
                Text("Version 1.0.0")
                    .font(.subheadline)
                    .foregroundColor(.secondary)
            }
            
            // Description
            Text("Virtual USB client for macOS. Forward USB devices to remote Windows servers over the network.")
                .font(.body)
                .multilineTextAlignment(.center)
                .foregroundColor(.secondary)
                .padding(.horizontal)
            
            Spacer()
            
            // Links
            HStack(spacing: 20) {
                Link("GitHub", destination: URL(string: "https://github.com")!)
                Link("Documentation", destination: URL(string: "https://github.com")!)
            }
            .font(.caption)
            
            // Copyright
            Text("© 2026 VUSB Project")
                .font(.caption2)
                .foregroundColor(.secondary)
        }
        .padding()
    }
}

// MARK: - Preview

#Preview {
    SettingsView()
        .environmentObject(AppSettings())
}
