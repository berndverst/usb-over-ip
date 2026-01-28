# Virtual USB Device Simulator

A Windows driver and server application that simulates USB devices over a network. This project enables USB devices physically connected to a remote machine to appear as locally connected devices on a Windows host.

## Table of Contents

- [Overview](#overview)
- [Architecture](#architecture)
- [Understanding USB Request Blocks (URBs)](#understanding-usb-request-blocks-urbs)
- [Protocol Specification](#protocol-specification)
- [Protocol Flow](#protocol-flow)
- [Components](#components)
- [Building](#building)
- [Installation](#installation)
- [Usage](#usage)
- [Security Considerations](#security-considerations)
- [License](#license)

---

## Overview

The Virtual USB Device Simulator creates a bridge between USB devices on a remote network device (client) and Windows applications on a host machine (server). The system consists of:

1. **A kernel-mode virtual USB host controller driver** that presents virtual USB devices to Windows
2. **A server application** that manages connections and routes USB traffic
3. **A client application** that captures real USB devices and forwards their data over the network

This enables scenarios such as:
- Accessing USB devices across network boundaries
- USB device sharing between machines
- Remote USB debugging and testing
- Virtualization of USB devices for development

---

## Architecture

### High-Level System Architecture

```
┌─────────────────────────────────────────────────────────────────────────────────┐
│                         REMOTE MACHINE (Client)                                  │
│                                                                                  │
│  ┌────────────────┐     ┌─────────────────────┐     ┌────────────────────────┐ │
│  │   Real USB     │     │   vusb_capture.c    │     │  vusb_client_enhanced  │ │
│  │   Hardware     │────▶│   WinUSB Capture    │────▶│  Network Client        │ │
│  │   Devices      │     │   Module            │     │                        │ │
│  └────────────────┘     └─────────────────────┘     └───────────┬────────────┘ │
│         │                        │                               │              │
│         │                        │                               │              │
│  ┌──────▼───────┐        ┌───────▼───────┐              ┌────────▼────────┐    │
│  │ USB Device   │        │ vusb_client   │              │ TCP/IP Socket   │    │
│  │ Descriptors  │        │ _urb.c        │              │ Connection      │    │
│  │ & Endpoints  │        │ URB Handler   │              │ Port 7575       │    │
│  └──────────────┘        └───────────────┘              └────────┬────────┘    │
└──────────────────────────────────────────────────────────────────┼─────────────┘
                                                                   │
                                               ════════════════════════════════════
                                                    Network (TCP/IP, LAN/WAN)
                                               ════════════════════════════════════
                                                                   │
┌──────────────────────────────────────────────────────────────────┼─────────────┐
│                          HOST MACHINE (Server)                   │              │
│                                                                  │              │
│  ┌────────────────────────────────────────────────────────────────▼──────────┐ │
│  │                        vusb_server.exe                                     │ │
│  │  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────────────┐    │ │
│  │  │ Client Manager  │  │ Device Registry │  │ vusb_server_urb.c       │    │ │
│  │  │ (Connections)   │  │ (Virtual Devs)  │  │ URB Forwarder           │    │ │
│  │  └─────────────────┘  └─────────────────┘  └─────────────────────────┘    │ │
│  └──────────────────────────────────────────────────────────────┬────────────┘ │
│                                                                  │              │
│                              IOCTL Interface                     │              │
│                                                                  │              │
│  ┌───────────────────────────────────────────────────────────────▼───────────┐ │
│  │                         vusb.sys (KMDF Driver)                             │ │
│  │  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────────────┐    │ │
│  │  │ vusb_driver.c   │  │ vusb_device.c   │  │ vusb_urb.c              │    │ │
│  │  │ Driver Core     │  │ Virtual Device  │  │ URB Parser/Builder      │    │ │
│  │  │ & PnP Manager   │  │ Management      │  │                         │    │ │
│  │  └─────────────────┘  └─────────────────┘  └─────────────────────────┘    │ │
│  │                                                                            │ │
│  │  ┌─────────────────┐  ┌─────────────────────────────────────────────┐     │ │
│  │  │ vusb_ioctl.c    │  │ Virtual USB Host Controller                 │     │ │
│  │  │ IOCTL Handler   │  │ (Presents devices to Windows USB Stack)     │     │ │
│  │  └─────────────────┘  └─────────────────────────────────────────────┘     │ │
│  └───────────────────────────────────────────────────────────────────────────┘ │
│                                              │                                  │
│                              ┌───────────────▼───────────────┐                 │
│                              │     Windows USB Stack         │                 │
│                              │  (usbhub.sys, usbccgp.sys)    │                 │
│                              └───────────────┬───────────────┘                 │
│                                              │                                  │
│                              ┌───────────────▼───────────────┐                 │
│                              │   Windows Applications        │                 │
│                              │   (See virtual USB devices    │                 │
│                              │    as if locally connected)   │                 │
│                              └───────────────────────────────┘                 │
└────────────────────────────────────────────────────────────────────────────────┘
```

### Component Interaction Diagram

```
┌─────────────┐    USB     ┌─────────────┐    WinUSB    ┌─────────────┐
│  Real USB   │◄─────────▶│   WinUSB    │◄────────────▶│   Capture   │
│  Device     │  Hardware  │   Driver    │     API      │   Module    │
└─────────────┘            └─────────────┘              └──────┬──────┘
                                                               │
                                                        ┌──────▼──────┐
                                                        │   Client    │
                                                        │  URB Proc.  │
                                                        └──────┬──────┘
                                                               │
┌─────────────┐    IOCTL   ┌─────────────┐    TCP/IP   ┌──────▼──────┐
│    VUSB     │◄─────────▶│    VUSB     │◄────────────▶│    VUSB     │
│   Driver    │            │   Server    │    Network   │   Client    │
└──────┬──────┘            └─────────────┘              └─────────────┘
       │
┌──────▼──────┐            ┌─────────────┐              ┌─────────────┐
│  Virtual    │────────────▶│  Windows    │─────────────▶│  User App   │
│  USB HC     │  USB Stack │  USB Hub    │   Device     │  (e.g.      │
└─────────────┘            └─────────────┘   Access     │  Notepad)   │
                                                        └─────────────┘
```

---

## Understanding USB Request Blocks (URBs)

### What is a URB?

A **USB Request Block (URB)** is the fundamental data structure used by Windows to communicate with USB devices. When an application wants to interact with a USB device (read data, write data, send commands), Windows creates a URB that describes the requested operation.

Think of a URB as a "work order" for the USB subsystem:
- It specifies **what operation** to perform (read, write, control transfer)
- It specifies **where** to send/receive data (endpoint address)
- It contains **the data** being transferred (or a buffer to receive data)
- It receives **the result** of the operation (success, error, bytes transferred)

### URB Structure

```
┌────────────────────────────────────────────────────────────────┐
│                        URB (USB Request Block)                  │
├────────────────────────────────────────────────────────────────┤
│  Header                                                         │
│  ┌──────────────────┬──────────────────┬────────────────────┐  │
│  │ Length (2 bytes) │ Function (2 bytes)│ Status (4 bytes)  │  │
│  │ Size of URB      │ URB_FUNCTION_xxx  │ USBD_STATUS_xxx   │  │
│  └──────────────────┴──────────────────┴────────────────────┘  │
├────────────────────────────────────────────────────────────────┤
│  Function-Specific Data (varies by URB type)                   │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │ Pipe Handle    - Which endpoint to use                   │  │
│  │ Transfer Flags - Direction, options                      │  │
│  │ Transfer Buffer- Data to send or buffer to receive       │  │
│  │ Buffer Length  - Size of data                            │  │
│  │ Setup Packet   - For control transfers (8 bytes)         │  │
│  └──────────────────────────────────────────────────────────┘  │
└────────────────────────────────────────────────────────────────┘
```

### URB Types (Functions)

| Function Code | Name | Description |
|--------------|------|-------------|
| `0x0008` | `URB_FUNCTION_CONTROL_TRANSFER` | Control transfers (setup packets) |
| `0x0009` | `URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER` | Bulk or interrupt data transfer |
| `0x000A` | `URB_FUNCTION_ISOCH_TRANSFER` | Isochronous transfer (streaming) |
| `0x000B` | `URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE` | Get device descriptor |
| `0x000D` | `URB_FUNCTION_SELECT_CONFIGURATION` | Set device configuration |
| `0x0017` | `URB_FUNCTION_SELECT_INTERFACE` | Select alternate interface |
| `0x001E` | `URB_FUNCTION_SYNC_RESET_PIPE` | Reset an endpoint pipe |
| `0x001F` | `URB_FUNCTION_SYNC_CLEAR_STALL` | Clear endpoint stall condition |

### USB Transfer Types

USB defines four transfer types, each suited for different purposes:

#### 1. Control Transfers
```
Host                                    Device
  │                                       │
  │  ─────── SETUP (8 bytes) ──────────▶  │  Request info
  │                                       │
  │  ◀─────── DATA (optional) ─────────   │  Response data
  │                                       │
  │  ─────── STATUS ───────────────────▶  │  Acknowledge
  │                                       │
```
- **Purpose**: Device configuration, commands, status queries
- **Characteristics**: Guaranteed delivery, low bandwidth, bidirectional
- **Examples**: Get device descriptor, set address, set configuration

#### 2. Bulk Transfers
```
Host                                    Device
  │                                       │
  │  ─────── DATA packet ──────────────▶  │
  │  ◀─────── ACK ─────────────────────   │
  │  ─────── DATA packet ──────────────▶  │
  │  ◀─────── ACK ─────────────────────   │
  │         ... continues ...             │
```
- **Purpose**: Large data transfers
- **Characteristics**: Guaranteed delivery, high bandwidth, no timing guarantee
- **Examples**: File transfers, printer data, mass storage

#### 3. Interrupt Transfers
```
Host                                    Device
  │                                       │
  │  ─────── IN token ─────────────────▶  │  (polling)
  │  ◀─────── DATA or NAK ─────────────   │
  │          (repeat at interval)         │
```
- **Purpose**: Small, periodic data with latency requirements
- **Characteristics**: Guaranteed bandwidth, bounded latency, small packets
- **Examples**: Keyboard keystrokes, mouse movements

#### 4. Isochronous Transfers
```
Host                                    Device
  │                                       │
  │  ─────── DATA ─────────────────────▶  │  (no ACK)
  │  ─────── DATA ─────────────────────▶  │
  │  ─────── DATA ─────────────────────▶  │
  │         (continuous stream)           │
```
- **Purpose**: Real-time streaming data
- **Characteristics**: Guaranteed bandwidth, no retransmission, time-critical
- **Examples**: Audio streaming, video capture

### URB Lifecycle in Virtual USB

```
┌──────────────────────────────────────────────────────────────────────────────┐
│                           URB LIFECYCLE                                       │
└──────────────────────────────────────────────────────────────────────────────┘

  Windows App                                                    Real USB Device
      │                                                                │
      │ 1. App calls ReadFile/WriteFile/DeviceIoControl               │
      ▼                                                                │
┌─────────────┐                                                        │
│ Windows     │  2. Creates URB for                                    │
│ USB Stack   │     the operation                                      │
└──────┬──────┘                                                        │
       │                                                               │
       │ 3. URB sent to host controller                                │
       ▼                                                               │
┌─────────────┐                                                        │
│ vusb.sys    │  4. vusb_urb.c parses URB                             │
│ Driver      │     extracts transfer info                             │
└──────┬──────┘                                                        │
       │                                                               │
       │ 5. IOCTL_VUSB_GET_PENDING_URB                                │
       ▼                                                               │
┌─────────────┐                                                        │
│ vusb_server │  6. Routes URB to correct                             │
│             │     client via device ID                               │
└──────┬──────┘                                                        │
       │                                                               │
       │ 7. VUSB_CMD_URB_SUBMIT over TCP                              │
       ▼                                                               │
┌─────────────┐                                                        │
│ vusb_client │  8. vusb_client_urb.c                                 │
│             │     routes to transfer handler                         │
└──────┬──────┘                                                        │
       │                                                               │
       │ 9. vusb_capture.c performs                                    │
       │    transfer via WinUSB API                                    │
       ▼                                                               │
┌─────────────┐                                                        │
│ WinUSB      │  10. Actual USB                                        │
│ Driver      │ ──────────────────────────────────────────────────────▶│
└─────────────┘      hardware transfer                                 │
       │                                                               │
       │ 11. Response data received                                    │
       │◀──────────────────────────────────────────────────────────────│
       │                                                               │
       │ 12. VUSB_CMD_URB_COMPLETE sent back through chain             │
       │     Client → Server → Driver → USB Stack → App                │
       ▼                                                               │
  Windows App                                                          │
      │                                                                │
      │ 13. App receives data                                          │
      ▼                                                                │
   [Complete]                                                          │
```

---

## Protocol Specification

### VUSB Protocol Header

All network messages use a common header format:

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
├─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┤
│                         Magic (0x56555342)                     │  "VUSB"
├─────────────────────────────────┬─────────────────────────────┤
│           Version (1.0)         │          Command            │
├─────────────────────────────────┴─────────────────────────────┤
│                        Payload Length                          │
├───────────────────────────────────────────────────────────────┤
│                       Sequence Number                          │
├───────────────────────────────────────────────────────────────┤
│                                                                │
│                      Payload (variable)                        │
│                                                                │
└───────────────────────────────────────────────────────────────┘
```

| Field | Offset | Size | Description |
|-------|--------|------|-------------|
| Magic | 0 | 4 bytes | Protocol identifier `0x56555342` ("VUSB") |
| Version | 4 | 2 bytes | Protocol version (major.minor) |
| Command | 6 | 2 bytes | Command type (see table below) |
| Length | 8 | 4 bytes | Payload length in bytes |
| Sequence | 12 | 4 bytes | Sequence number for request/response matching |
| Payload | 16 | Variable | Command-specific data |

### Protocol Commands

| Code | Command | Direction | Description |
|------|---------|-----------|-------------|
| `0x0001` | `VUSB_CMD_CONNECT` | Client → Server | Client connection request |
| `0x0002` | `VUSB_CMD_CONNECT_ACK` | Server → Client | Connection acknowledgment |
| `0x0003` | `VUSB_CMD_DISCONNECT` | Bidirectional | Graceful disconnection |
| `0x0004` | `VUSB_CMD_ATTACH` | Client → Server | Attach a USB device |
| `0x0005` | `VUSB_CMD_DETACH` | Client → Server | Detach a USB device |
| `0x0006` | `VUSB_CMD_URB_SUBMIT` | Server → Client | Forward URB to device |
| `0x0007` | `VUSB_CMD_URB_COMPLETE` | Client → Server | URB completion response |
| `0x0008` | `VUSB_CMD_RESET` | Server → Client | Reset device |
| `0x0009` | `VUSB_CMD_KEEPALIVE` | Bidirectional | Connection keepalive |
| `0x000A` | `VUSB_CMD_ERROR` | Bidirectional | Error notification |

### Device Attach Payload

```
┌───────────────────────────────────────────────────────────────┐
│                    ATTACH Payload Structure                    │
├───────────────────────────────────────────────────────────────┤
│  Device ID (4 bytes)     - Unique device identifier           │
│  Vendor ID (2 bytes)     - USB VID                            │
│  Product ID (2 bytes)    - USB PID                            │
│  Device Class (1 byte)   - USB device class                   │
│  Device Subclass (1 byte)                                     │
│  Device Protocol (1 byte)                                     │
│  Speed (1 byte)          - USB_SPEED_xxx                      │
│  Descriptors (variable)  - Full device descriptor tree        │
└───────────────────────────────────────────────────────────────┘
```

### URB Submit Payload

```
┌───────────────────────────────────────────────────────────────┐
│                  URB_SUBMIT Payload Structure                  │
├───────────────────────────────────────────────────────────────┤
│  URB ID (4 bytes)        - Unique URB identifier              │
│  Device ID (4 bytes)     - Target device                      │
│  Function (2 bytes)      - URB_FUNCTION_xxx                   │
│  Endpoint (1 byte)       - Endpoint address (0x00-0xFF)       │
│  Direction (1 byte)      - 0=OUT, 1=IN                        │
│  Transfer Flags (4 bytes)                                     │
│  Buffer Length (4 bytes) - Expected/actual data size          │
│  Setup Packet (8 bytes)  - For control transfers only         │
│  Data (variable)         - Transfer data (for OUT transfers)  │
└───────────────────────────────────────────────────────────────┘
```

### URB Complete Payload

```
┌───────────────────────────────────────────────────────────────┐
│                 URB_COMPLETE Payload Structure                 │
├───────────────────────────────────────────────────────────────┤
│  URB ID (4 bytes)        - Matching URB identifier            │
│  Status (4 bytes)        - USBD_STATUS_xxx                    │
│  Actual Length (4 bytes) - Bytes actually transferred         │
│  Data (variable)         - Transfer data (for IN transfers)   │
└───────────────────────────────────────────────────────────────┘
```

---

## Protocol Flow

### Connection Establishment

```
    Client                                      Server
       │                                           │
       │  ════ TCP Connection (port 7575) ════▶   │
       │                                           │
       │  ──── VUSB_CMD_CONNECT ─────────────────▶ │
       │       {client_name, version}              │
       │                                           │
       │  ◀──── VUSB_CMD_CONNECT_ACK ──────────── │
       │       {server_version, session_id}        │
       │                                           │
       │  ════════ Connection Ready ═══════════   │
```

### Device Attachment

```
    Client                    Server                     Driver
       │                         │                          │
       │  ── VUSB_CMD_ATTACH ──▶ │                          │
       │     {device_info,       │                          │
       │      descriptors}       │                          │
       │                         │                          │
       │                         │  ── IOCTL_VUSB_PLUG ───▶ │
       │                         │     {device_info}        │
       │                         │                          │
       │                         │                     [Creates Virtual
       │                         │                      USB Device]
       │                         │                          │
       │                         │  ◀─── STATUS_SUCCESS ─── │
       │                         │                          │
       │  ◀── Attach Success ─── │                          │
       │                         │                          │
                            [Device now visible in Windows Device Manager]
```

### USB Data Transfer (Complete Flow)

```
  Application      USB Stack       Driver        Server        Client       Real Device
       │               │             │              │              │              │
       │ ReadFile()    │             │              │              │              │
       │──────────────▶│             │              │              │              │
       │               │             │              │              │              │
       │               │ URB_BULK_   │              │              │              │
       │               │ TRANSFER    │              │              │              │
       │               │────────────▶│              │              │              │
       │               │             │              │              │              │
       │               │             │ IOCTL_GET    │              │              │
       │               │             │ _PENDING_URB │              │              │
       │               │             │◀─────────────│              │              │
       │               │             │──────────────▶              │              │
       │               │             │ {urb_info}   │              │              │
       │               │             │              │              │              │
       │               │             │              │ CMD_URB_     │              │
       │               │             │              │ SUBMIT       │              │
       │               │             │              │─────────────▶│              │
       │               │             │              │              │              │
       │               │             │              │              │ WinUsb_      │
       │               │             │              │              │ ReadPipe()   │
       │               │             │              │              │─────────────▶│
       │               │             │              │              │              │
       │               │             │              │              │◀─── data ────│
       │               │             │              │              │              │
       │               │             │              │ CMD_URB_     │              │
       │               │             │              │ COMPLETE     │              │
       │               │             │              │◀─────────────│              │
       │               │             │              │ {status,data}│              │
       │               │             │              │              │              │
       │               │             │ IOCTL_       │              │              │
       │               │             │ COMPLETE_URB │              │              │
       │               │             │◀─────────────│              │              │
       │               │             │              │              │              │
       │               │◀────────────│              │              │              │
       │               │  complete   │              │              │              │
       │               │             │              │              │              │
       │◀──────────────│             │              │              │              │
       │    data       │             │              │              │              │
       │               │             │              │              │              │
```

### Device Detachment

```
    Client                    Server                     Driver
       │                         │                          │
       │  ── VUSB_CMD_DETACH ──▶ │                          │
       │     {device_id}         │                          │
       │                         │                          │
       │                         │  ── IOCTL_VUSB_UNPLUG ─▶ │
       │                         │     {device_id}          │
       │                         │                          │
       │                         │                     [Removes Virtual
       │                         │                      USB Device]
       │                         │                          │
       │                         │  ◀─── STATUS_SUCCESS ─── │
       │                         │                          │
       │  ◀── Detach Success ─── │                          │
       │                         │                          │
                          [Device removed from Windows Device Manager]
```

### Error Handling Flow

```
    Client                    Server                     Driver
       │                         │                          │
       │                         │  ◀── URB Submit ──────── │
       │                         │                          │
       │  ◀── CMD_URB_SUBMIT ─── │                          │
       │                         │                          │
       │  [USB Transfer Fails]   │                          │
       │                         │                          │
       │  ── CMD_URB_COMPLETE ─▶ │                          │
       │     {USBD_STATUS_STALL} │                          │
       │                         │                          │
       │                         │  ── IOCTL_COMPLETE ────▶ │
       │                         │     {error_status}       │
       │                         │                          │
       │                         │                     [Reports error
       │                         │                      to USB Stack]
       │                         │                          │
                                              [App receives error]
```

---

## Components

### 1. Virtual USB Driver (`driver/`)

| File | Purpose |
|------|---------|
| `vusb_driver.c/h` | Main driver entry, PnP, power management |
| `vusb_device.c` | Virtual device creation and management |
| `vusb_ioctl.c` | User-mode IOCTL communication handler |
| `vusb_urb.c/h` | URB parsing, building, and completion |
| `vusb.inf` | Driver installation information |

**Key Features:**
- KMDF-based for reliability and compatibility
- Creates virtual USB host controller
- Routes USB requests through IOCTL interface
- Supports hot plug/unplug of virtual devices

### 2. Server Application (`server/`)

| File | Purpose |
|------|---------|
| `vusb_server.c/h` | Main server, network handling |
| `vusb_server_urb.c/h` | URB forwarding between driver and clients |

**Key Features:**
- Multi-client support
- Device-to-client routing
- Asynchronous URB handling
- Connection management

### 3. Client Application (`client/`)

| File | Purpose |
|------|---------|
| `vusb_client.c/h` | Basic client with simulated devices |
| `vusb_client_enhanced.c` | Full client with real USB support |
| `vusb_capture.c/h` | Real USB device enumeration and capture |
| `vusb_client_urb.c/h` | URB processing for real devices |

**Key Features:**
- Real USB device enumeration via WinUSB
- Device descriptor capture
- USB transfer forwarding
- Multiple device support

### 4. Protocol Library (`protocol/`)

| File | Purpose |
|------|---------|
| `vusb_protocol.h` | Network protocol definitions |
| `vusb_ioctl.h` | Driver IOCTL interface definitions |

### 5. Tools (`tools/`)

| File | Purpose |
|------|---------|
| `vusb_install.c` | Driver installation utility |
| `vusb_test.c` | Driver and protocol testing |

---

## Building

### Prerequisites

- **Windows Driver Kit (WDK) 10** or later
- **Visual Studio 2019/2022** with C++ and WDK components
- **CMake 3.16** or later
- **Windows SDK 10.0.19041.0** or later

### Build User-Mode Components

```powershell
# Create build directory
mkdir build
cd build

# Configure with CMake
cmake ..

# Build all targets
cmake --build . --config Release
```

### Build Targets

| Target | Description | Output |
|--------|-------------|--------|
| `vusb_server` | Network server | `vusb_server.exe` |
| `vusb_client` | Basic client | `vusb_client.exe` |
| `vusb_client_capture` | Enhanced client with real USB | `vusb_client_capture.exe` |
| `vusb_test` | Test utility | `vusb_test.exe` |
| `vusb_install` | Installation utility | `vusb_install.exe` |

### Build Driver (Kernel-Mode)

```powershell
# Open Visual Studio solution
cd driver
start vusb.sln

# Or build from command line (requires VS Developer Command Prompt)
msbuild vusb.sln /p:Configuration=Release /p:Platform=x64
```

---

## Installation

### 1. Enable Test Signing (Development Only)

```powershell
# Run as Administrator
bcdedit /set testsigning on

# Reboot required
shutdown /r /t 0
```

### 2. Install the Driver

```powershell
# Using the installation utility
vusb_install.exe install driver\vusb.inf

# Or manually with pnputil
pnputil /add-driver driver\vusb.inf /install
```

### 3. Verify Installation

```powershell
# Check if driver is loaded
sc query vusb

# View in Device Manager
devmgmt.msc
# Look under "Universal Serial Bus controllers"
```

---

## Usage

### Start the Server (Host Machine)

```powershell
# Basic usage
vusb_server.exe

# Custom port
vusb_server.exe --port 8080

# Verbose output
vusb_server.exe --verbose
```

### Start the Client (Remote Machine)

```powershell
# Basic client with simulated device
vusb_client.exe --server 192.168.1.100 --port 7575

# Enhanced client with real USB capture
vusb_client_capture.exe --server 192.168.1.100

# List available USB devices
vusb_client_capture.exe --list

# Attach specific device by VID:PID
vusb_client_capture.exe --server 192.168.1.100 --device 1234:5678
```

### Example Session

```
# On Server (192.168.1.100)
> vusb_server.exe --verbose
[INFO] Virtual USB Server starting on port 7575
[INFO] Waiting for connections...
[INFO] Client connected from 192.168.1.50
[INFO] Device attached: VID=046D PID=C534 (Logitech USB Receiver)

# On Client (192.168.1.50)
> vusb_client_capture.exe --server 192.168.1.100
[INFO] Connecting to 192.168.1.100:7575
[INFO] Connected!
[INFO] Found USB device: Logitech USB Receiver (046D:C534)
[INFO] Attaching device...
[INFO] Device attached successfully

# On Server, the device now appears in Device Manager
# Applications can access it as a normal USB device
```

---

## Security Considerations

⚠️ **Warning**: This project is designed for development and testing. For production deployment:

| Risk | Mitigation |
|------|------------|
| **Unencrypted traffic** | Implement TLS/SSL encryption |
| **No authentication** | Add client authentication (certificates, tokens) |
| **Network exposure** | Use VPN or firewall rules |
| **Kernel driver** | Code signing with EV certificate |
| **USB attacks** | Input validation, rate limiting |

### Recommended Production Setup

```
┌──────────┐      VPN/TLS      ┌──────────┐
│  Client  │◄─────────────────▶│  Server  │
└──────────┘    Encrypted      └──────────┘
                Authenticated
```

---

## Troubleshooting

| Issue | Solution |
|-------|----------|
| Driver won't load | Enable test signing, check WDK version |
| Connection refused | Check firewall, verify port 7575 |
| Device not appearing | Check Device Manager for errors |
| URB failures | Enable verbose logging, check USB device |
| Client crashes | Run as Administrator for WinUSB access |

---

## License

MIT License - See [LICENSE](LICENSE) file for details.

---

## Contributing

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Submit a pull request

See [DEVELOPER.md](DEVELOPER.md) for development guidelines.
