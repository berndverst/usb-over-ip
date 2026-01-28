# VUSB Android Client

Android client application for the Virtual USB Device Simulator.

## Features

- **USB Device Capture**: Enumerates and captures USB devices connected to the Android device
- **Network Forwarding**: Forwards USB traffic to the VUSB server over TCP/IP
- **Mobile & TV Support**: Works on both Android phones/tablets and Android TV (NVIDIA Shield, etc.)
- **Background Service**: Runs as a foreground service for persistent operation
- **Auto-start**: Can automatically start on device boot
- **Permission Handling**: Manages USB permissions automatically

## Requirements

- Android 5.0 (API 21) or higher
- USB Host support (most Android devices with USB-A or USB-C port)
- Network connectivity to VUSB server

## Building

### Prerequisites

- Android Studio Arctic Fox or later
- Android SDK 34
- Kotlin 1.9.x

### Build Steps

```bash
cd android

# Debug build
./gradlew assembleDebug

# Release build
./gradlew assembleRelease
```

The APK will be in `app/build/outputs/apk/`

## Installation

### Standard Installation

```bash
adb install app/build/outputs/apk/debug/app-debug.apk
```

### System App Installation (for privileged USB access)

For some devices (especially Android TV), you may want to install as a system app for automatic USB permission:

```bash
# Requires root access
adb root
adb remount
adb push app/build/outputs/apk/release/app-release.apk /system/priv-app/VUSBClient/VUSBClient.apk
adb shell chmod 644 /system/priv-app/VUSBClient/VUSBClient.apk
adb reboot
```

### NVIDIA Shield TV

For NVIDIA Shield TV, you can sideload the app:

1. Enable Developer Options on your Shield
2. Enable "Apps from Unknown Sources"
3. Use `adb install` or a sideload app

## Usage

### Mobile App

1. Open the VUSB Client app
2. Enter the server address and port
3. Tap "Connect"
4. Grant USB permissions when prompted
5. Tap "Attach" on devices you want to forward

### Android TV

1. Open VUSB Client from the TV launcher
2. Navigate using the D-pad
3. Select "Settings" to configure server address
4. Select "Connect to Server"
5. Navigate to devices and select to attach

### Auto-start on Boot

1. Go to Settings in the app
2. Enable "Auto-start on Boot"
3. Configure server address
4. Enable "Auto-attach Devices"

The service will start automatically when the device boots.

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                        Android Device                            │
│                                                                  │
│  ┌──────────────────┐     ┌──────────────────────────────────┐ │
│  │   USB Device     │────▶│      UsbDeviceManager            │ │
│  │   (Hardware)     │     │  - Enumeration via USB Host API  │ │
│  └──────────────────┘     │  - Permission management         │ │
│                           │  - WinUSB-like control           │ │
│                           └──────────────┬───────────────────┘ │
│                                          │                      │
│                           ┌──────────────▼───────────────────┐ │
│                           │         UrbHandler               │ │
│                           │  - Process URB requests          │ │
│                           │  - Route to USB transfers        │ │
│                           └──────────────┬───────────────────┘ │
│                                          │                      │
│  ┌──────────────────┐     ┌──────────────▼───────────────────┐ │
│  │   UI (Mobile)    │     │    UsbForwardingService          │ │
│  │   MainActivity   │◄───▶│  - Foreground service            │ │
│  │                  │     │  - Manages connections           │ │
│  ├──────────────────┤     │  - Coordinates components        │ │
│  │   UI (TV)        │     └──────────────┬───────────────────┘ │
│  │   TvActivity     │                    │                      │
│  └──────────────────┘     ┌──────────────▼───────────────────┐ │
│                           │      VusbNetworkClient           │ │
│                           │  - TCP/IP connection             │ │
│                           │  - VUSB protocol handling        │ │
│                           └──────────────┬───────────────────┘ │
└──────────────────────────────────────────┼──────────────────────┘
                                           │
                                    Network (TCP)
                                           │
                                           ▼
                                    ┌─────────────┐
                                    │ VUSB Server │
                                    └─────────────┘
```

## Protocol Compatibility

The Android client implements the same VUSB protocol as the Windows client:

- Magic: `0x56555342` ("VUSB")
- Default port: 7575
- Commands: CONNECT, ATTACH, DETACH, URB_SUBMIT, URB_COMPLETE, etc.

## Permissions

The app requires the following permissions:

| Permission | Purpose |
|------------|---------|
| `INTERNET` | Network communication with server |
| `USB_HOST` | Access USB devices |
| `FOREGROUND_SERVICE` | Run as foreground service |
| `RECEIVE_BOOT_COMPLETED` | Auto-start on boot |
| `WAKE_LOCK` | Keep device awake during transfers |
| `POST_NOTIFICATIONS` | Show service notification |

## Troubleshooting

### No USB devices found

- Ensure your device supports USB Host mode
- Try a powered USB hub if device doesn't provide enough power
- Check USB cable (some cables are charge-only)

### Connection refused

- Verify server address and port
- Check that server is running
- Ensure both devices are on the same network
- Check firewall settings on server

### Permission denied

- Grant USB permission when prompted
- For system-level access, install as system app
- On some devices, you may need to enable USB debugging

### Device not appearing on server

- Check that device is attached in the app
- Verify network connectivity
- Look at server logs for errors

## Known Limitations

- Some USB devices may not work due to Android USB Host API limitations
- Isochronous transfers (audio/video streaming) have limited support
- Some Android devices restrict USB Host functionality

## License

MIT License - See LICENSE file in project root.
