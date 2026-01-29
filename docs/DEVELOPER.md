# Developer Guide

## Prerequisites

### For Server/Client (User-Mode)
- Visual Studio 2019 or later with C++ workload
- CMake 3.16 or later
- Windows 10 SDK

### For Driver (Kernel-Mode)
- Windows Driver Kit (WDK) 10 or later
- Visual Studio 2019/2022 with WDK integration
- Windows 10 SDK matching WDK version

## Building

### Building User-Mode Components

```powershell
# Using the build script
.\build.bat

# Or manually with CMake
mkdir build
cd build
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release
```

### Building the Driver

1. Open `driver\vusb.sln` in Visual Studio
2. Select **Release | x64** configuration
3. Build the solution

The driver files will be in `driver\Release\`:
- `vusb.sys` - The driver binary
- `vusb.inf` - Installation file
- `vusb.cat` - Catalog file (after signing)

## Driver Signing

For testing, enable test signing mode:

```powershell
# Enable test signing (requires admin, reboot)
bcdedit /set testsigning on
```

For production, you need an EV code signing certificate.

## Installation

### Installing the Driver

```powershell
# As administrator
pnputil /add-driver driver\vusb.inf /install

# Or use the install utility
tools\vusb_install.exe install driver\vusb.inf
```

### Verifying Installation

```powershell
# Check driver status
sc query VirtualUSB

# Or use Device Manager - look under "System devices"
```

## Testing

### Test Without Network

```powershell
# Run the test utility
tools\vusb_test.exe
```

This tests the driver directly using IOCTLs.

### Test With Network

Terminal 1 (Server):
```powershell
vusb_server.exe --port 7575
```

Terminal 2 (Client):
```powershell
vusb_client.exe --server 127.0.0.1 --port 7575
# Then use 'attach 1234 5678' to attach a test device
```

## Architecture Deep Dive

### Protocol Flow

1. **Client connects** to server (TCP)
2. **Client attaches device** - sends device descriptors
3. **Server creates virtual device** via driver IOCTL
4. **Windows enumerates device** - requests descriptors via URBs
5. **Server forwards URBs** to client over network
6. **Client responds** with descriptor data from real device
7. **URB completes** and Windows sees the device

### URB Handling Flow

```
Windows App → USB Stack → Virtual USB Driver → Server → Network → Client → Real USB Device
                                                ←                   ←
                                               Response              Response
```

### Interrupt Endpoint Polling

For game controllers and other HID devices, the mobile clients implement continuous interrupt endpoint polling:

1. **When a device attaches**, the client scans for interrupt IN endpoints
2. **A background coroutine/task** continuously reads from each interrupt endpoint
3. **Data changes are detected** to reduce unnecessary network traffic
4. **URB completions are sent proactively** to the server without waiting for URB_SUBMIT

The interrupt URB format uses a special URB ID to distinguish from normal URB responses:
- Bit 31 = 1 (interrupt flag)
- Bits 16-23 = Device ID
- Bits 0-7 = Endpoint address

This approach provides low-latency input for:
- Game controllers (button states, joystick positions)
- Mice (movement data)
- Keyboards (key events)
- Other HID devices

Files involved:
- Android: `InterruptPoller.kt` - Polls using Android USB Host API
- macOS: `InterruptPoller.swift` - Polls using IOKit USB interfaces

### Key Data Structures

- `VUSB_HEADER` - Protocol message header (all messages)
- `VUSB_DEVICE_INFO` - Device identification and properties
- `VUSB_URB_SUBMIT` - USB Request Block submission
- `VUSB_URB_COMPLETE` - URB completion with data

## Extending the Project

### Adding New IOCTL Commands

1. Define the IOCTL code in `protocol/vusb_ioctl.h`
2. Add handler function declaration in `driver/vusb_driver.h`
3. Implement handler in `driver/vusb_ioctl.c`
4. Add case to switch in `VusbEvtIoDeviceControl`

### Adding New Protocol Commands

1. Add command enum in `protocol/vusb_protocol.h`
2. Define request/response structures
3. Add handler in server (`VusbServerProcessMessage`)
4. Add sender in client if needed

## Debugging

### Driver Debugging

Use WinDbg with kernel debugging:

```
# Enable kernel debugging
bcdedit /debug on
bcdedit /dbgsettings net hostip:<debugger_ip> port:50000
```

In WinDbg:
```
!devnode 0 1 ROOT\VirtualUSB
!drvobj \Driver\VirtualUSB 2
```

### Server/Client Debugging

Use Visual Studio debugger or add logging:

```c
// Enable verbose logging
#define VUSB_DEBUG 1
```

### Viewing Driver Debug Output

Use DebugView or WinDbg to see `KdPrint` output.

## Common Issues

### Driver Won't Load
- Check test signing is enabled
- Verify driver is properly signed
- Check Event Viewer for errors

### Device Not Appearing
- Check driver is running (`sc query VirtualUSB`)
- Verify plugin IOCTL succeeded
- Check Device Manager for errors

### Network Connection Failed
- Check firewall settings
- Verify server is running
- Check port is not in use

## Security Considerations

This project is for development/testing. For production:

1. **Encrypt network traffic** - Add TLS support
2. **Authenticate clients** - Add certificate-based auth
3. **Validate input** - Sanitize all IOCTL/network input
4. **Code signing** - Use proper EV certificates
5. **Access control** - Restrict driver access
