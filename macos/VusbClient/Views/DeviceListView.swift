//
//  DeviceListView.swift
//  VusbClient
//
//  List view for USB devices
//

import SwiftUI

struct DeviceListView: View {
    
    @EnvironmentObject var networkClient: VusbNetworkClient
    @EnvironmentObject var usbManager: UsbDeviceManager
    @EnvironmentObject var logger: ActivityLogger
    
    @State private var selectedDevice: MacUsbDevice?
    @State private var showingDeviceDetails = false
    
    var body: some View {
        VStack(spacing: 0) {
            // Header
            HStack {
                Text("USB Devices")
                    .font(.headline)
                
                Spacer()
                
                Text("\(usbManager.availableDevices.count) devices")
                    .font(.caption)
                    .foregroundColor(.secondary)
            }
            .padding()
            
            Divider()
            
            // Device list
            if usbManager.availableDevices.isEmpty {
                ContentUnavailableView(
                    "No USB Devices",
                    systemImage: "cable.connector.horizontal",
                    description: Text("Connect a USB device to see it here")
                )
            } else {
                List(usbManager.availableDevices, selection: $selectedDevice) { device in
                    DeviceRowView(device: device)
                        .tag(device)
                        .contextMenu {
                            deviceContextMenu(for: device)
                        }
                }
                .listStyle(.inset)
            }
        }
        .sheet(isPresented: $showingDeviceDetails) {
            if let device = selectedDevice {
                DeviceDetailSheet(device: device)
            }
        }
    }
    
    // MARK: - Context Menu
    
    @ViewBuilder
    private func deviceContextMenu(for device: MacUsbDevice) -> some View {
        if usbManager.isDeviceAttached(device) {
            Button(action: { detachDevice(device) }) {
                Label("Detach", systemImage: "minus.circle")
            }
        } else {
            Button(action: { attachDevice(device) }) {
                Label("Attach", systemImage: "plus.circle")
            }
            .disabled(!networkClient.connectionState.isConnected)
        }
        
        Divider()
        
        Button(action: { 
            selectedDevice = device
            showingDeviceDetails = true 
        }) {
            Label("Details", systemImage: "info.circle")
        }
    }
    
    // MARK: - Actions
    
    private func attachDevice(_ device: MacUsbDevice) {
        guard networkClient.connectionState.isConnected else {
            logger.warning("Cannot attach device: Not connected to server", source: "USB")
            return
        }
        
        Task {
            do {
                // Get descriptors
                var deviceInfo = device.toVusbDeviceInfo()
                if let descriptor = usbManager.getDeviceDescriptor(device) {
                    deviceInfo.deviceDescriptor = descriptor
                }
                if let config = usbManager.getConfigDescriptor(device) {
                    deviceInfo.configDescriptor = config
                }
                
                // Send attach command
                try await networkClient.attachDevice(deviceInfo)
                
                // Update local state
                usbManager.attachDevice(device)
                logger.info("Device attached: \(device.displayName)", source: "USB")
            } catch {
                logger.error("Failed to attach device: \(error.localizedDescription)", source: "USB")
                usbManager.setDeviceError(device, message: error.localizedDescription)
            }
        }
    }
    
    private func detachDevice(_ device: MacUsbDevice) {
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

// MARK: - Device Detail Sheet

struct DeviceDetailSheet: View {
    
    let device: MacUsbDevice
    
    @Environment(\.dismiss) var dismiss
    
    var body: some View {
        VStack(spacing: 0) {
            // Header
            HStack {
                Image(systemName: usbDeviceClassIcon(for: device.deviceClass))
                    .font(.largeTitle)
                    .foregroundColor(.accentColor)
                
                VStack(alignment: .leading) {
                    Text(device.displayName)
                        .font(.title2)
                        .fontWeight(.semibold)
                    
                    Text(device.vendorProductString)
                        .font(.caption)
                        .foregroundColor(.secondary)
                }
                
                Spacer()
            }
            .padding()
            
            Divider()
            
            // Details
            Form {
                Section("Identification") {
                    LabeledContent("Vendor ID", value: String(format: "0x%04X", device.vendorId))
                    LabeledContent("Product ID", value: String(format: "0x%04X", device.productId))
                    if !device.serialNumber.isEmpty {
                        LabeledContent("Serial Number", value: device.serialNumber)
                    }
                }
                
                Section("USB Properties") {
                    LabeledContent("Class", value: usbDeviceClassName(for: device.deviceClass))
                    LabeledContent("Subclass", value: String(format: "0x%02X", device.deviceSubclass))
                    LabeledContent("Protocol", value: String(format: "0x%02X", device.deviceProtocol))
                    LabeledContent("Speed", value: device.speed.description)
                }
                
                Section("System") {
                    LabeledContent("Location ID", value: String(format: "0x%08X", device.locationId))
                    if !device.manufacturer.isEmpty {
                        LabeledContent("Manufacturer", value: device.manufacturer)
                    }
                    if !device.product.isEmpty {
                        LabeledContent("Product", value: device.product)
                    }
                }
            }
            .formStyle(.grouped)
            
            // Close button
            HStack {
                Spacer()
                Button("Close") {
                    dismiss()
                }
                .keyboardShortcut(.escape)
                .buttonStyle(.borderedProminent)
            }
            .padding()
        }
        .frame(width: 450, height: 500)
    }
}

// MARK: - Preview

#Preview {
    DeviceListView()
        .environmentObject(VusbNetworkClient())
        .environmentObject(UsbDeviceManager())
        .environmentObject(ActivityLogger())
}
