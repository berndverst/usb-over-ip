# Virtual USB Userspace Server

A combined server and userspace USB device emulator that works without requiring a kernel driver installation.

## Overview

The userspace server provides an alternative to the kernel driver + server combination. It handles all network protocol communication with clients and provides device simulation capabilities, making it ideal for:

- **Development & Testing**: Test client applications without installing the kernel driver
- **Debugging**: Capture and analyze USB traffic for debugging purposes
- **Portability**: Easier to port to other platforms (Linux, macOS) in the future
- **Gadget Emulation**: Implement custom USB device emulation in userspace

## Limitations

Without a kernel driver, the userspace server **cannot** present USB devices to the Windows USB stack as real devices. This means:

- Applications expecting real USB devices won't see forwarded devices
- Device Manager won't show the virtual USB devices
- Standard USB APIs (WinUSB, libusb) can't access the virtual devices

For full system-level USB device presentation, use the kernel driver (`driver/vusb.sys`) with `vusb_server`.

## Building

The userspace server is built automatically with the CMake project:

```bash
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

The executable will be at `build/Release/vusb_userspace.exe`.

## Usage

```
vusb_userspace [options]

Options:
  --port <port>        Listen port (default: 7575)
  --max-clients <n>    Maximum clients (default: 32)
  --max-devices <n>    Maximum devices (default: 16)
  --simulation         Enable device simulation mode
  --verbose            Enable verbose logging
  --capture <file>     Capture USB traffic to file
  --help, -h           Show this help
```

### Examples

Basic usage (default port):
```bash
vusb_userspace.exe
```

With verbose logging:
```bash
vusb_userspace.exe --verbose
```

Capture USB traffic:
```bash
vusb_userspace.exe --capture traffic.bin
```

Custom port:
```bash
vusb_userspace.exe --port 8080
```

## Interactive Commands

While the server is running, you can use these keyboard shortcuts:

| Key | Action |
|-----|--------|
| h | Show help |
| s | Show statistics |
| d | List connected devices |
| c | List connected clients |
| q | Quit |

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    vusb_userspace.exe                       │
│                                                             │
│  ┌─────────────────┐     ┌─────────────────────────────┐   │
│  │   Network Layer │     │   Userspace USB Emulation   │   │
│  │                 │     │                             │   │
│  │ - Accept clients│     │ - Device management         │   │
│  │ - Protocol parse│────▶│ - URB processing            │   │
│  │ - Message route │     │ - Endpoint emulation        │   │
│  │                 │     │ - Gadget callbacks          │   │
│  └─────────────────┘     └─────────────────────────────┘   │
│                                     │                       │
│                                     ▼                       │
│                          ┌──────────────────┐              │
│                          │  Capture Module  │              │
│                          │  (optional)      │              │
│                          └──────────────────┘              │
└─────────────────────────────────────────────────────────────┘
              ▲
              │ TCP/IP
              ▼
     ┌─────────────────┐
     │  Remote Client  │
     │  (Android/iOS/  │
     │   Desktop)      │
     └─────────────────┘
```

## API for Custom Gadget Emulation

The userspace server can be extended with custom USB gadget emulation via callback functions:

```c
#include "vusb_userspace.h"

VUSB_US_GADGET_OPS myGadgetOps = {
    .HandleSetup = MySetupHandler,
    .HandleDataOut = MyDataOutHandler,
    .HandleDataIn = MyDataInHandler,
    .HandleReset = MyResetHandler,
    .HandleSetConfiguration = MyConfigHandler,
    .Context = myContext
};

VusbUsSetGadgetOps(&ctx, &myGadgetOps);
```

See `vusb_userspace.h` for the full API documentation.

## Comparison: Userspace vs Kernel Driver

| Feature | Userspace Server | Kernel Driver + Server |
|---------|-----------------|----------------------|
| Installation | No driver install | Requires driver signing/install |
| System integration | Application-level only | Full USB stack integration |
| Debugging | Easy, standard tools | Requires kernel debugging |
| Performance | Good | Better (kernel path) |
| Stability | Process crash = restart | BSOD risk if buggy |
| Portability | Easy to port | Platform specific |
| Use case | Development, testing | Production deployment |

## Protocol Compatibility

The userspace server is fully compatible with:
- Android client (`android/` directory)
- macOS client (`macos/` directory)
- Windows client (`client/` directory)

All use the same network protocol defined in `protocol/vusb_protocol.h`.

## Files

- `vusb_userspace.h` - API header
- `vusb_userspace.c` - Core implementation
- `vusb_userspace_main.c` - Main entry point with CLI

## Future Improvements

- [ ] Linux implementation using GadgetFS/FunctionFS
- [ ] macOS implementation  
- [ ] WebSocket transport for browser clients
- [ ] USB traffic replay from capture files
- [ ] REST API for device management
