# Virtual USB Device Simulator

A Windows driver and server application that simulates USB devices over a network.

## Architecture

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           Remote Device (Client)                             │
│  ┌──────────────┐    ┌──────────────────┐    ┌─────────────────────────┐   │
│  │  Real USB    │───▶│  USB Capture     │───▶│  Network Encoder/       │   │
│  │  Devices     │    │  Driver/Service  │    │  Client Application     │   │
│  └──────────────┘    └──────────────────┘    └───────────┬─────────────┘   │
└──────────────────────────────────────────────────────────┼─────────────────┘
                                                           │
                                                    Network (TCP/IP)
                                                           │
┌──────────────────────────────────────────────────────────┼─────────────────┐
│                        Host Device (Server)              │                  │
│  ┌─────────────────────────┐    ┌────────────────────────▼────────────┐   │
│  │  Virtual USB Driver     │◀──▶│  Server Application                  │   │
│  │  (KMDF - vusb.sys)      │    │  (vusb_server.exe)                   │   │
│  └───────────┬─────────────┘    └─────────────────────────────────────┘   │
│              │                                                             │
│  ┌───────────▼─────────────┐                                              │
│  │  Windows USB Stack      │                                              │
│  │  (Applications see      │                                              │
│  │   virtual USB devices)  │                                              │
│  └─────────────────────────┘                                              │
└───────────────────────────────────────────────────────────────────────────┘
```

## Components

### 1. Virtual USB Driver (`driver/`)
- Kernel-mode driver (KMDF) that creates virtual USB host controller
- Presents virtual USB devices to Windows
- Communicates with user-mode server via IOCTL

### 2. Server Application (`server/`)
- User-mode Windows service/application
- Handles network connections from remote clients
- Routes USB requests between driver and network clients

### 3. Protocol Library (`protocol/`)
- Common definitions for USB over network protocol
- Serialization/deserialization of USB requests and responses

### 4. Client Application (`client/`)
- Runs on remote device with real USB devices
- Captures USB device descriptors and traffic
- Sends encoded data to server over network

## Building

### Prerequisites
- Windows Driver Kit (WDK) 10 or later
- Visual Studio 2019 or later with C++ and WDK components
- CMake 3.16 or later

### Build Steps

```powershell
# Build the driver (requires WDK)
cd driver
msbuild vusb.sln /p:Configuration=Release /p:Platform=x64

# Build the server and client
cd ..
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

## Installation

### Driver Installation
1. Disable Secure Boot (for test signing)
2. Enable test signing: `bcdedit /set testsigning on`
3. Install the driver:
```powershell
pnputil /add-driver vusb.inf /install
```

### Server Setup
```powershell
# Run as administrator
vusb_server.exe --port 7575
```

## Protocol

The USB over network protocol uses a simple binary format:

| Field | Size | Description |
|-------|------|-------------|
| Magic | 4 bytes | Protocol identifier (0x56555342 "VUSB") |
| Version | 2 bytes | Protocol version |
| Command | 2 bytes | Command type |
| Length | 4 bytes | Payload length |
| Sequence | 4 bytes | Sequence number |
| Payload | Variable | Command-specific data |

## Security Considerations

⚠️ **Warning**: This is for development/testing purposes. For production use:
- Implement TLS encryption for network communication
- Add authentication mechanisms
- Consider using VPN for network transport

## License

MIT License - See LICENSE file for details
