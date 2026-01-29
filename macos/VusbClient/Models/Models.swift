//
//  Models.swift
//  VusbClient
//
//  App-wide models and settings
//

import Foundation
import SwiftUI

// MARK: - App Settings

/// Application settings stored in UserDefaults
class AppSettings: ObservableObject {
    
    private let defaults = UserDefaults.standard
    
    // Keys
    private enum Keys {
        static let serverAddress = "serverAddress"
        static let serverPort = "serverPort"
        static let autoConnect = "autoConnect"
        static let autoAttachDevices = "autoAttachDevices"
        static let keepAliveInterval = "keepAliveInterval"
        static let showNotifications = "showNotifications"
        static let launchAtLogin = "launchAtLogin"
        static let theme = "theme"
    }
    
    // MARK: - Published Properties
    
    @Published var serverAddress: String {
        didSet { defaults.set(serverAddress, forKey: Keys.serverAddress) }
    }
    
    @Published var serverPort: UInt16 {
        didSet { defaults.set(Int(serverPort), forKey: Keys.serverPort) }
    }
    
    @Published var autoConnect: Bool {
        didSet { defaults.set(autoConnect, forKey: Keys.autoConnect) }
    }
    
    @Published var autoAttachDevices: Bool {
        didSet { defaults.set(autoAttachDevices, forKey: Keys.autoAttachDevices) }
    }
    
    @Published var keepAliveInterval: TimeInterval {
        didSet { defaults.set(keepAliveInterval, forKey: Keys.keepAliveInterval) }
    }
    
    @Published var showNotifications: Bool {
        didSet { defaults.set(showNotifications, forKey: Keys.showNotifications) }
    }
    
    @Published var launchAtLogin: Bool {
        didSet { defaults.set(launchAtLogin, forKey: Keys.launchAtLogin) }
    }
    
    @Published var theme: AppTheme {
        didSet { defaults.set(theme.rawValue, forKey: Keys.theme) }
    }
    
    // MARK: - Initialization
    
    init() {
        self.serverAddress = defaults.string(forKey: Keys.serverAddress) ?? "192.168.1.100"
        self.serverPort = UInt16(defaults.integer(forKey: Keys.serverPort))
        if self.serverPort == 0 { self.serverPort = VUSB_DEFAULT_PORT }
        self.autoConnect = defaults.bool(forKey: Keys.autoConnect)
        self.autoAttachDevices = defaults.bool(forKey: Keys.autoAttachDevices)
        self.keepAliveInterval = defaults.double(forKey: Keys.keepAliveInterval)
        if self.keepAliveInterval == 0 { self.keepAliveInterval = 10.0 }
        self.showNotifications = defaults.object(forKey: Keys.showNotifications) as? Bool ?? true
        self.launchAtLogin = defaults.bool(forKey: Keys.launchAtLogin)
        
        let themeRaw = defaults.string(forKey: Keys.theme) ?? AppTheme.system.rawValue
        self.theme = AppTheme(rawValue: themeRaw) ?? .system
    }
    
    // MARK: - Reset
    
    func resetToDefaults() {
        serverAddress = "192.168.1.100"
        serverPort = VUSB_DEFAULT_PORT
        autoConnect = false
        autoAttachDevices = false
        keepAliveInterval = 10.0
        showNotifications = true
        launchAtLogin = false
        theme = .system
    }
}

// MARK: - App Theme

enum AppTheme: String, CaseIterable, Identifiable {
    case system = "system"
    case light = "light"
    case dark = "dark"
    
    var id: String { rawValue }
    
    var displayName: String {
        switch self {
        case .system: return "System"
        case .light: return "Light"
        case .dark: return "Dark"
        }
    }
    
    var colorScheme: ColorScheme? {
        switch self {
        case .system: return nil
        case .light: return .light
        case .dark: return .dark
        }
    }
}

// MARK: - Connection Info

/// Information about the current connection
struct ConnectionInfo {
    let serverAddress: String
    let serverPort: UInt16
    let connectedAt: Date
    var attachedDeviceCount: Int
    
    var displayAddress: String {
        return "\(serverAddress):\(serverPort)"
    }
    
    var connectionDuration: TimeInterval {
        return Date().timeIntervalSince(connectedAt)
    }
    
    var formattedDuration: String {
        let duration = connectionDuration
        let hours = Int(duration) / 3600
        let minutes = (Int(duration) % 3600) / 60
        let seconds = Int(duration) % 60
        
        if hours > 0 {
            return String(format: "%d:%02d:%02d", hours, minutes, seconds)
        }
        return String(format: "%02d:%02d", minutes, seconds)
    }
}

// MARK: - Log Entry

/// Log entry for activity view
struct LogEntry: Identifiable {
    let id = UUID()
    let timestamp: Date
    let level: LogLevel
    let message: String
    let source: String
    
    enum LogLevel: String {
        case debug = "DEBUG"
        case info = "INFO"
        case warning = "WARN"
        case error = "ERROR"
        
        var color: Color {
            switch self {
            case .debug: return .secondary
            case .info: return .primary
            case .warning: return .orange
            case .error: return .red
            }
        }
        
        var icon: String {
            switch self {
            case .debug: return "ant"
            case .info: return "info.circle"
            case .warning: return "exclamationmark.triangle"
            case .error: return "xmark.circle"
            }
        }
    }
    
    var formattedTimestamp: String {
        let formatter = DateFormatter()
        formatter.dateFormat = "HH:mm:ss.SSS"
        return formatter.string(from: timestamp)
    }
}

// MARK: - Activity Logger

/// Activity logger for the app
@MainActor
class ActivityLogger: ObservableObject {
    
    @Published private(set) var entries: [LogEntry] = []
    
    private let maxEntries = 1000
    
    func log(_ message: String, level: LogEntry.LogLevel = .info, source: String = "App") {
        let entry = LogEntry(timestamp: Date(), level: level, message: message, source: source)
        entries.insert(entry, at: 0)
        
        // Trim if necessary
        if entries.count > maxEntries {
            entries = Array(entries.prefix(maxEntries))
        }
    }
    
    func debug(_ message: String, source: String = "App") {
        log(message, level: .debug, source: source)
    }
    
    func info(_ message: String, source: String = "App") {
        log(message, level: .info, source: source)
    }
    
    func warning(_ message: String, source: String = "App") {
        log(message, level: .warning, source: source)
    }
    
    func error(_ message: String, source: String = "App") {
        log(message, level: .error, source: source)
    }
    
    func clear() {
        entries.removeAll()
    }
}

// MARK: - USB Device Class Names

/// Get human-readable name for USB device class
func usbDeviceClassName(for deviceClass: UInt8) -> String {
    switch deviceClass {
    case 0x00: return "Composite"
    case 0x01: return "Audio"
    case 0x02: return "Communications"
    case 0x03: return "HID"
    case 0x05: return "Physical"
    case 0x06: return "Image"
    case 0x07: return "Printer"
    case 0x08: return "Mass Storage"
    case 0x09: return "Hub"
    case 0x0A: return "CDC-Data"
    case 0x0B: return "Smart Card"
    case 0x0D: return "Content Security"
    case 0x0E: return "Video"
    case 0x0F: return "Personal Healthcare"
    case 0x10: return "Audio/Video"
    case 0xDC: return "Diagnostic"
    case 0xE0: return "Wireless Controller"
    case 0xEF: return "Miscellaneous"
    case 0xFE: return "Application Specific"
    case 0xFF: return "Vendor Specific"
    default: return "Unknown"
    }
}

/// Get SF Symbol name for USB device class
func usbDeviceClassIcon(for deviceClass: UInt8) -> String {
    switch deviceClass {
    case 0x01: return "speaker.wave.2"
    case 0x02: return "antenna.radiowaves.left.and.right"
    case 0x03: return "keyboard"
    case 0x06: return "camera"
    case 0x07: return "printer"
    case 0x08: return "externaldrive"
    case 0x09: return "rectangle.connected.to.line.below"
    case 0x0E: return "video"
    case 0xE0: return "wifi"
    default: return "cable.connector.horizontal"
    }
}
