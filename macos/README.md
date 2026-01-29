# VUSB Client for macOS

A native macOS client application for forwarding USB devices to a remote Windows server over the network.

## Features

- **Native SwiftUI Interface**: Modern macOS app with support for both light and dark modes
- **USB Device Discovery**: Automatic detection of connected USB devices via IOKit
- **Device Forwarding**: Forward USB devices to remote Windows VUSB server
- **Menu Bar Integration**: Quick access from the menu bar
- **Activity Logging**: Real-time activity and debug logging
- **Customizable Settings**: Configure server connection, auto-connect, and more

## Requirements

- macOS 13.0 (Ventura) or later
- Xcode 14.0 or later (for building)
- VUSB Server running on Windows

## Building

### Using Xcode

1. Open `VusbClient.xcodeproj` in Xcode
2. Select your development team in Signing & Capabilities
3. Build and run (⌘R)

### Using Command Line

```bash
cd macos
xcodebuild -project VusbClient.xcodeproj -scheme VusbClient -configuration Release build
```

## Project Structure

```
macos/
├── VusbClient.xcodeproj/       # Xcode project
└── VusbClient/
    ├── VusbClientApp.swift     # Main app entry point
    ├── Info.plist              # App configuration
    ├── VusbClient.entitlements # App entitlements
    ├── Assets.xcassets/        # App icons and colors
    ├── Protocol/
    │   └── VusbProtocol.swift  # Protocol definitions
    ├── Network/
    │   └── VusbNetworkClient.swift # Network communication
    ├── USB/
    │   └── UsbDeviceManager.swift  # USB device management
    ├── Models/
    │   └── Models.swift        # Data models and settings
    └── Views/
        ├── ContentView.swift   # Main content view
        ├── DeviceListView.swift # Device list
        ├── DeviceRowView.swift  # Device row component
        └── SettingsView.swift   # Settings panel
```

## Usage

### Connecting to a Server

1. Launch VUSB Client
2. Click the "Connect" button in the toolbar or use ⌘⇧N
3. Enter the server address and port (default: 7575)
4. Click "Connect"

### Attaching USB Devices

1. Connect to a VUSB server
2. Connected USB devices will appear in the device list
3. Click the "+" button next to a device to attach it
4. The device will be forwarded to the Windows server

### Settings

Access settings via the menu (⌘,) to configure:

- **Server Address**: Default server to connect to
- **Auto-connect**: Automatically connect on launch
- **Auto-attach**: Automatically attach new devices
- **Keep-alive Interval**: Connection health check interval
- **Theme**: Light, Dark, or System

### Menu Bar

The app includes a menu bar icon for quick access:
- View connection status
- Connect/disconnect quickly
- See device counts
- Open main window

## Protocol Compatibility

The macOS client implements the VUSB protocol (v1.0) compatible with:
- Windows VUSB Server
- Android VUSB Client

### Protocol Features

- TCP connection on port 7575 (configurable)
- Little-endian binary protocol
- Keep-alive mechanism
- URB (USB Request Block) forwarding

## Entitlements

The app requires the following entitlements:

- `com.apple.security.network.client`: Network client access
- `com.apple.security.network.server`: Network server access (for responses)
- `com.apple.security.device.usb`: USB device access

## Known Limitations

1. **USB Communication**: Full USB communication requires additional IOKit USB interface implementation. The current version provides device enumeration and basic attach/detach.

2. **App Sandbox**: The app runs without sandbox to access USB devices. A sandboxed version would require a helper tool.

3. **Code Signing**: For distribution, the app needs proper code signing with USB entitlements.

## Troubleshooting

### Cannot see USB devices

- Ensure the app has USB access permission in System Settings > Privacy & Security
- Try running with elevated privileges for certain devices

### Connection fails

- Verify the server address and port
- Check firewall settings on both machines
- Ensure the VUSB server is running on Windows

### Device won't attach

- Ensure you're connected to the server
- Check if the device is in use by another application
- Review the Activity Log for error messages

## License

See the main project LICENSE file for licensing information.

## Contributing

Contributions are welcome! Please see the main project DEVELOPER.md for guidelines.
