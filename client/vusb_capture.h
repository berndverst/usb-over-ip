/**
 * USB Device Capture Module
 * 
 * Uses WinUSB/SetupAPI to enumerate and capture real USB devices
 * for forwarding over the network.
 */

#ifndef VUSB_CAPTURE_H
#define VUSB_CAPTURE_H

#include <windows.h>
#include <winusb.h>
#include <setupapi.h>
#include <stdint.h>
#include "../protocol/vusb_protocol.h"

#pragma comment(lib, "winusb.lib")
#pragma comment(lib, "setupapi.lib")

#define MAX_USB_DEVICES         32
#define MAX_USB_INTERFACES      8
#define MAX_USB_ENDPOINTS       32
#define MAX_DESCRIPTOR_SIZE     4096

/* Endpoint information */
typedef struct _USB_ENDPOINT_INFO {
    uint8_t     Address;
    uint8_t     Attributes;     /* Transfer type */
    uint16_t    MaxPacketSize;
    uint8_t     Interval;
    USBD_PIPE_INFORMATION PipeInfo;
} USB_ENDPOINT_INFO, *PUSB_ENDPOINT_INFO;

/* Interface information */
typedef struct _USB_INTERFACE_INFO {
    uint8_t     InterfaceNumber;
    uint8_t     AlternateSetting;
    uint8_t     InterfaceClass;
    uint8_t     InterfaceSubClass;
    uint8_t     InterfaceProtocol;
    uint8_t     NumEndpoints;
    USB_ENDPOINT_INFO Endpoints[MAX_USB_ENDPOINTS];
} USB_INTERFACE_INFO, *PUSB_INTERFACE_INFO;

/* Captured USB device */
typedef struct _USB_CAPTURED_DEVICE {
    /* Identification */
    uint32_t            LocalId;
    uint32_t            RemoteId;       /* ID assigned by server */
    BOOL                Active;
    BOOL                Opened;
    
    /* Device path for opening */
    wchar_t             DevicePath[MAX_PATH];
    
    /* Handles */
    HANDLE              DeviceHandle;
    WINUSB_INTERFACE_HANDLE WinUsbHandle;
    WINUSB_INTERFACE_HANDLE InterfaceHandles[MAX_USB_INTERFACES];
    
    /* Device information */
    VUSB_DEVICE_INFO    DeviceInfo;
    USB_DEVICE_DESCRIPTOR DeviceDescriptor;
    
    /* Configuration */
    uint8_t             NumInterfaces;
    USB_INTERFACE_INFO  Interfaces[MAX_USB_INTERFACES];
    
    /* Raw descriptors */
    uint8_t             Descriptors[MAX_DESCRIPTOR_SIZE];
    uint32_t            DescriptorLength;
    
    /* Statistics */
    uint64_t            BytesIn;
    uint64_t            BytesOut;
    uint32_t            TransfersCompleted;
    uint32_t            TransferErrors;
} USB_CAPTURED_DEVICE, *PUSB_CAPTURED_DEVICE;

/* Capture context */
typedef struct _USB_CAPTURE_CONTEXT {
    BOOL                    Initialized;
    CRITICAL_SECTION        Lock;
    uint32_t                NextLocalId;
    uint32_t                DeviceCount;
    USB_CAPTURED_DEVICE     Devices[MAX_USB_DEVICES];
    
    /* Hotplug notification */
    HDEVNOTIFY              NotificationHandle;
    HWND                    NotificationWindow;
    
    /* Callback for device events */
    void (*OnDeviceArrival)(PUSB_CAPTURED_DEVICE device, void* context);
    void (*OnDeviceRemoval)(PUSB_CAPTURED_DEVICE device, void* context);
    void* CallbackContext;
} USB_CAPTURE_CONTEXT, *PUSB_CAPTURE_CONTEXT;

/* Initialization and cleanup */
int UsbCaptureInit(PUSB_CAPTURE_CONTEXT ctx);
void UsbCaptureCleanup(PUSB_CAPTURE_CONTEXT ctx);

/* Device enumeration */
int UsbCaptureEnumerateDevices(PUSB_CAPTURE_CONTEXT ctx);
int UsbCaptureRefreshDevices(PUSB_CAPTURE_CONTEXT ctx);

/* Device operations */
PUSB_CAPTURED_DEVICE UsbCaptureFindDevice(PUSB_CAPTURE_CONTEXT ctx, uint32_t localId);
PUSB_CAPTURED_DEVICE UsbCaptureFindDeviceByVidPid(PUSB_CAPTURE_CONTEXT ctx, 
                                                   uint16_t vid, uint16_t pid);
int UsbCaptureOpenDevice(PUSB_CAPTURED_DEVICE device);
void UsbCaptureCloseDevice(PUSB_CAPTURED_DEVICE device);

/* Descriptor retrieval */
int UsbCaptureGetDescriptors(PUSB_CAPTURED_DEVICE device);
int UsbCaptureGetStringDescriptor(PUSB_CAPTURED_DEVICE device, uint8_t index,
                                   uint16_t langId, wchar_t* buffer, uint32_t bufferSize);

/* USB transfers */
int UsbCaptureControlTransfer(
    PUSB_CAPTURED_DEVICE device,
    PVUSB_SETUP_PACKET setupPacket,
    uint8_t* data,
    uint32_t dataLength,
    uint32_t* actualLength,
    uint32_t timeout);

int UsbCaptureBulkTransfer(
    PUSB_CAPTURED_DEVICE device,
    uint8_t endpoint,
    uint8_t* data,
    uint32_t dataLength,
    uint32_t* actualLength,
    uint32_t timeout);

int UsbCaptureInterruptTransfer(
    PUSB_CAPTURED_DEVICE device,
    uint8_t endpoint,
    uint8_t* data,
    uint32_t dataLength,
    uint32_t* actualLength,
    uint32_t timeout);

/* Async transfer support */
typedef struct _USB_ASYNC_TRANSFER {
    OVERLAPPED          Overlapped;
    PUSB_CAPTURED_DEVICE Device;
    uint8_t             Endpoint;
    uint8_t*            Buffer;
    uint32_t            BufferLength;
    uint32_t            UrbId;
    void (*Callback)(struct _USB_ASYNC_TRANSFER* transfer, uint32_t status, 
                     uint32_t actualLength, void* context);
    void*               CallbackContext;
} USB_ASYNC_TRANSFER, *PUSB_ASYNC_TRANSFER;

int UsbCaptureAsyncBulkTransfer(
    PUSB_CAPTURED_DEVICE device,
    uint8_t endpoint,
    uint8_t* data,
    uint32_t dataLength,
    PUSB_ASYNC_TRANSFER transfer);

int UsbCaptureCancelTransfer(PUSB_ASYNC_TRANSFER transfer);

/* Utility functions */
const char* UsbCaptureGetSpeedString(uint8_t speed);
const char* UsbCaptureGetClassString(uint8_t deviceClass);
void UsbCapturePrintDeviceInfo(PUSB_CAPTURED_DEVICE device);

#endif /* VUSB_CAPTURE_H */
