//
//  DeviceRowView.swift
//  VusbClient
//
//  Row view for individual USB devices
//

import SwiftUI

struct DeviceRowView: View {
    
    let device: MacUsbDevice
    
    @EnvironmentObject var networkClient: VusbNetworkClient
    @EnvironmentObject var usbManager: UsbDeviceManager
    @EnvironmentObject var logger: ActivityLogger
    
    @State private var isHovering = false
    
    var isAttached: Bool {
        usbManager.isDeviceAttached(device)
    }
    
    var deviceState: DeviceForwardingState {
        usbManager.getDeviceState(device)
    }
    
    var body: some View {
        HStack(spacing: 12) {
            // Device icon with status indicator
            ZStack(alignment: .bottomTrailing) {
                Image(systemName: usbDeviceClassIcon(for: device.deviceClass))
                    .font(.title2)
                    .foregroundColor(iconColor)
                    .frame(width: 32, height: 32)
                
                // Status indicator
                if isAttached {
                    Circle()
                        .fill(statusIndicatorColor)
                        .frame(width: 10, height: 10)
                        .overlay(
                            Circle()
                                .stroke(Color.white, lineWidth: 1.5)
                        )
                }
            }
            
            // Device info
            VStack(alignment: .leading, spacing: 2) {
                Text(device.displayName)
                    .font(.body)
                    .fontWeight(.medium)
                    .lineLimit(1)
                
                HStack(spacing: 8) {
                    Text(device.vendorProductString)
                        .font(.caption)
                        .foregroundColor(.secondary)
                    
                    if !device.manufacturer.isEmpty {
                        Text("•")
                            .foregroundColor(.secondary)
                        Text(device.manufacturer)
                            .font(.caption)
                            .foregroundColor(.secondary)
                            .lineLimit(1)
                    }
                }
            }
            
            Spacer()
            
            // Speed badge
            speedBadge
            
            // Attach/Detach button
            attachButton
        }
        .padding(.vertical, 6)
        .contentShape(Rectangle())
        .onHover { hovering in
            withAnimation(.easeInOut(duration: 0.15)) {
                isHovering = hovering
            }
        }
    }
    
    // MARK: - Subviews
    
    private var iconColor: Color {
        if case .error = deviceState {
            return .red
        }
        return isAttached ? .accentColor : .secondary
    }
    
    private var statusIndicatorColor: Color {
        switch deviceState {
        case .idle:
            return .gray
        case .attached:
            return .green
        case .forwarding:
            return .blue
        case .error:
            return .red
        }
    }
    
    private var speedBadge: some View {
        Text(speedText)
            .font(.caption2)
            .fontWeight(.medium)
            .padding(.horizontal, 6)
            .padding(.vertical, 2)
            .background(speedColor.opacity(0.15))
            .foregroundColor(speedColor)
            .cornerRadius(4)
    }
    
    private var speedText: String {
        switch device.speed {
        case .low: return "LS"
        case .full: return "FS"
        case .high: return "HS"
        case .super: return "SS"
        case .superPlus: return "SS+"
        case .unknown: return "?"
        }
    }
    
    private var speedColor: Color {
        switch device.speed {
        case .low, .full: return .orange
        case .high: return .green
        case .super, .superPlus: return .blue
        case .unknown: return .gray
        }
    }
    
    private var attachButton: some View {
        Button(action: toggleAttach) {
            Image(systemName: isAttached ? "minus.circle.fill" : "plus.circle")
                .font(.title3)
                .foregroundColor(isAttached ? .orange : .accentColor)
        }
        .buttonStyle(.plain)
        .disabled(!networkClient.connectionState.isConnected && !isAttached)
        .opacity(isHovering || isAttached ? 1.0 : 0.5)
        .help(isAttached ? "Detach device from server" : "Attach device to server")
    }
    
    // MARK: - Actions
    
    private func toggleAttach() {
        if isAttached {
            detachDevice()
        } else {
            attachDevice()
        }
    }
    
    private func attachDevice() {
        guard networkClient.connectionState.isConnected else {
            logger.warning("Cannot attach device: Not connected to server", source: "USB")
            return
        }
        
        Task {
            do {
                var deviceInfo = device.toVusbDeviceInfo()
                if let descriptor = usbManager.getDeviceDescriptor(device) {
                    deviceInfo.deviceDescriptor = descriptor
                }
                if let config = usbManager.getConfigDescriptor(device) {
                    deviceInfo.configDescriptor = config
                }
                
                try await networkClient.attachDevice(deviceInfo)
                usbManager.attachDevice(device)
                logger.info("Device attached: \(device.displayName)", source: "USB")
            } catch {
                logger.error("Failed to attach device: \(error.localizedDescription)", source: "USB")
                usbManager.setDeviceError(device, message: error.localizedDescription)
            }
        }
    }
    
    private func detachDevice() {
        Task {
            do {
                try await networkClient.detachDevice(deviceId: Int32(device.id))
                usbManager.detachDevice(device)
                logger.info("Device detached: \(device.displayName)", source: "USB")
            } catch {
                logger.error("Failed to detach device: \(error.localizedDescription)", source: "USB")
            }
        }
    }
}

// MARK: - Preview

#Preview {
    List {
        DeviceRowView(device: MacUsbDevice(
            id: 1,
            vendorId: 0x046D,
            productId: 0xC52B,
            deviceClass: 0,
            deviceSubclass: 0,
            deviceProtocol: 0,
            speed: .high,
            manufacturer: "Logitech",
            product: "USB Receiver",
            serialNumber: "123456",
            locationId: 0x12345678,
            service: 0
        ))
    }
    .environmentObject(VusbNetworkClient())
    .environmentObject(UsbDeviceManager())
    .environmentObject(ActivityLogger())
}
