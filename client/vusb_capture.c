/**
 * USB Device Capture Module Implementation
 * 
 * Enumerates and captures real USB devices using WinUSB/SetupAPI.
 */

#include <windows.h>
#include <winusb.h>
#include <setupapi.h>
#include <usbiodef.h>
#include <devpkey.h>
#include <cfgmgr32.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vusb_capture.h"

#pragma comment(lib, "winusb.lib")
#pragma comment(lib, "setupapi.lib")

/* Internal helper functions */
static int ParseDevicePath(const wchar_t* path, uint16_t* vid, uint16_t* pid);
static int ReadDeviceDescriptor(PUSB_CAPTURED_DEVICE device);
static int ReadConfigDescriptor(PUSB_CAPTURED_DEVICE device);
static int BuildDescriptorBuffer(PUSB_CAPTURED_DEVICE device);
static uint8_t GetDeviceSpeed(HANDLE deviceHandle);

/**
 * UsbCaptureInit - Initialize capture context
 */
int UsbCaptureInit(PUSB_CAPTURE_CONTEXT ctx)
{
    if (!ctx) return -1;

    memset(ctx, 0, sizeof(USB_CAPTURE_CONTEXT));
    InitializeCriticalSection(&ctx->Lock);
    ctx->NextLocalId = 1;
    ctx->Initialized = TRUE;

    printf("[Capture] Initialized\n");
    return 0;
}

/**
 * UsbCaptureCleanup - Cleanup capture context
 */
void UsbCaptureCleanup(PUSB_CAPTURE_CONTEXT ctx)
{
    if (!ctx || !ctx->Initialized) return;

    EnterCriticalSection(&ctx->Lock);

    /* Close all devices */
    for (int i = 0; i < MAX_USB_DEVICES; i++) {
        if (ctx->Devices[i].Active) {
            UsbCaptureCloseDevice(&ctx->Devices[i]);
        }
    }

    LeaveCriticalSection(&ctx->Lock);
    DeleteCriticalSection(&ctx->Lock);

    ctx->Initialized = FALSE;
    printf("[Capture] Cleanup complete\n");
}

/**
 * UsbCaptureEnumerateDevices - Enumerate all USB devices
 */
int UsbCaptureEnumerateDevices(PUSB_CAPTURE_CONTEXT ctx)
{
    HDEVINFO deviceInfoSet;
    SP_DEVICE_INTERFACE_DATA interfaceData;
    PSP_DEVICE_INTERFACE_DETAIL_DATA_W detailData = NULL;
    DWORD requiredSize;
    DWORD index;
    int deviceCount = 0;

    if (!ctx || !ctx->Initialized) return -1;

    EnterCriticalSection(&ctx->Lock);

    /* Get device info set for all USB devices */
    deviceInfoSet = SetupDiGetClassDevsW(
        &GUID_DEVINTERFACE_USB_DEVICE,
        NULL,
        NULL,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE
    );

    if (deviceInfoSet == INVALID_HANDLE_VALUE) {
        printf("[Capture] Failed to get device list: %lu\n", GetLastError());
        LeaveCriticalSection(&ctx->Lock);
        return -1;
    }

    /* Enumerate devices */
    interfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
    for (index = 0; 
         SetupDiEnumDeviceInterfaces(deviceInfoSet, NULL, &GUID_DEVINTERFACE_USB_DEVICE, 
                                      index, &interfaceData);
         index++) {
        
        /* Get required size */
        SetupDiGetDeviceInterfaceDetailW(deviceInfoSet, &interfaceData, NULL, 0, 
                                          &requiredSize, NULL);
        
        detailData = (PSP_DEVICE_INTERFACE_DETAIL_DATA_W)malloc(requiredSize);
        if (!detailData) continue;
        
        detailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        
        /* Get device interface detail */
        if (SetupDiGetDeviceInterfaceDetailW(deviceInfoSet, &interfaceData, detailData,
                                              requiredSize, NULL, NULL)) {
            uint16_t vid, pid;
            
            /* Parse VID/PID from device path */
            if (ParseDevicePath(detailData->DevicePath, &vid, &pid) == 0) {
                /* Check if device already exists */
                PUSB_CAPTURED_DEVICE existing = UsbCaptureFindDeviceByVidPid(ctx, vid, pid);
                
                if (!existing && ctx->DeviceCount < MAX_USB_DEVICES) {
                    /* Find free slot */
                    for (int i = 0; i < MAX_USB_DEVICES; i++) {
                        if (!ctx->Devices[i].Active) {
                            PUSB_CAPTURED_DEVICE device = &ctx->Devices[i];
                            
                            memset(device, 0, sizeof(USB_CAPTURED_DEVICE));
                            device->LocalId = ctx->NextLocalId++;
                            device->Active = TRUE;
                            wcscpy_s(device->DevicePath, MAX_PATH, detailData->DevicePath);
                            device->DeviceInfo.VendorId = vid;
                            device->DeviceInfo.ProductId = pid;
                            
                            ctx->DeviceCount++;
                            deviceCount++;
                            
                            printf("[Capture] Found device: VID=%04X PID=%04X\n", vid, pid);
                            break;
                        }
                    }
                }
            }
        }
        
        free(detailData);
        detailData = NULL;
    }

    SetupDiDestroyDeviceInfoList(deviceInfoSet);
    LeaveCriticalSection(&ctx->Lock);

    printf("[Capture] Enumeration complete: %d devices found\n", deviceCount);
    return deviceCount;
}

/**
 * UsbCaptureRefreshDevices - Refresh device list and open new devices
 */
int UsbCaptureRefreshDevices(PUSB_CAPTURE_CONTEXT ctx)
{
    int count;

    count = UsbCaptureEnumerateDevices(ctx);
    
    /* Try to open and read descriptors for each device */
    EnterCriticalSection(&ctx->Lock);
    
    for (int i = 0; i < MAX_USB_DEVICES; i++) {
        PUSB_CAPTURED_DEVICE device = &ctx->Devices[i];
        
        if (device->Active && !device->Opened) {
            if (UsbCaptureOpenDevice(device) == 0) {
                UsbCaptureGetDescriptors(device);
                UsbCapturePrintDeviceInfo(device);
            }
        }
    }
    
    LeaveCriticalSection(&ctx->Lock);
    return count;
}

/**
 * UsbCaptureFindDevice - Find device by local ID
 */
PUSB_CAPTURED_DEVICE UsbCaptureFindDevice(PUSB_CAPTURE_CONTEXT ctx, uint32_t localId)
{
    if (!ctx) return NULL;

    for (int i = 0; i < MAX_USB_DEVICES; i++) {
        if (ctx->Devices[i].Active && ctx->Devices[i].LocalId == localId) {
            return &ctx->Devices[i];
        }
    }
    return NULL;
}

/**
 * UsbCaptureFindDeviceByVidPid - Find device by VID/PID
 */
PUSB_CAPTURED_DEVICE UsbCaptureFindDeviceByVidPid(PUSB_CAPTURE_CONTEXT ctx, 
                                                   uint16_t vid, uint16_t pid)
{
    if (!ctx) return NULL;

    for (int i = 0; i < MAX_USB_DEVICES; i++) {
        if (ctx->Devices[i].Active &&
            ctx->Devices[i].DeviceInfo.VendorId == vid &&
            ctx->Devices[i].DeviceInfo.ProductId == pid) {
            return &ctx->Devices[i];
        }
    }
    return NULL;
}

/**
 * UsbCaptureOpenDevice - Open a USB device for access
 */
int UsbCaptureOpenDevice(PUSB_CAPTURED_DEVICE device)
{
    BOOL result;

    if (!device || device->Opened) return -1;

    /* Open device handle */
    device->DeviceHandle = CreateFileW(
        device->DevicePath,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED,
        NULL
    );

    if (device->DeviceHandle == INVALID_HANDLE_VALUE) {
        printf("[Capture] Failed to open device: %lu\n", GetLastError());
        return -1;
    }

    /* Initialize WinUSB */
    result = WinUsb_Initialize(device->DeviceHandle, &device->WinUsbHandle);
    if (!result) {
        printf("[Capture] WinUsb_Initialize failed: %lu\n", GetLastError());
        CloseHandle(device->DeviceHandle);
        device->DeviceHandle = INVALID_HANDLE_VALUE;
        return -1;
    }

    device->Opened = TRUE;
    printf("[Capture] Opened device: VID=%04X PID=%04X\n", 
           device->DeviceInfo.VendorId, device->DeviceInfo.ProductId);

    return 0;
}

/**
 * UsbCaptureCloseDevice - Close a USB device
 */
void UsbCaptureCloseDevice(PUSB_CAPTURED_DEVICE device)
{
    if (!device) return;

    /* Close interface handles */
    for (int i = 0; i < MAX_USB_INTERFACES; i++) {
        if (device->InterfaceHandles[i]) {
            WinUsb_Free(device->InterfaceHandles[i]);
            device->InterfaceHandles[i] = NULL;
        }
    }

    /* Close WinUSB handle */
    if (device->WinUsbHandle) {
        WinUsb_Free(device->WinUsbHandle);
        device->WinUsbHandle = NULL;
    }

    /* Close device handle */
    if (device->DeviceHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(device->DeviceHandle);
        device->DeviceHandle = INVALID_HANDLE_VALUE;
    }

    device->Opened = FALSE;
}

/**
 * UsbCaptureGetDescriptors - Read all device descriptors
 */
int UsbCaptureGetDescriptors(PUSB_CAPTURED_DEVICE device)
{
    if (!device || !device->Opened) return -1;

    /* Read device descriptor */
    if (ReadDeviceDescriptor(device) != 0) {
        printf("[Capture] Failed to read device descriptor\n");
        return -1;
    }

    /* Read configuration descriptor */
    if (ReadConfigDescriptor(device) != 0) {
        printf("[Capture] Failed to read config descriptor\n");
        return -1;
    }

    /* Read string descriptors */
    wchar_t stringBuffer[256];
    
    if (device->DeviceDescriptor.iManufacturer) {
        if (UsbCaptureGetStringDescriptor(device, device->DeviceDescriptor.iManufacturer,
                                           0x0409, stringBuffer, sizeof(stringBuffer)) == 0) {
            WideCharToMultiByte(CP_UTF8, 0, stringBuffer, -1, 
                               device->DeviceInfo.Manufacturer, 
                               sizeof(device->DeviceInfo.Manufacturer), NULL, NULL);
        }
    }
    
    if (device->DeviceDescriptor.iProduct) {
        if (UsbCaptureGetStringDescriptor(device, device->DeviceDescriptor.iProduct,
                                           0x0409, stringBuffer, sizeof(stringBuffer)) == 0) {
            WideCharToMultiByte(CP_UTF8, 0, stringBuffer, -1, 
                               device->DeviceInfo.Product, 
                               sizeof(device->DeviceInfo.Product), NULL, NULL);
        }
    }
    
    if (device->DeviceDescriptor.iSerialNumber) {
        if (UsbCaptureGetStringDescriptor(device, device->DeviceDescriptor.iSerialNumber,
                                           0x0409, stringBuffer, sizeof(stringBuffer)) == 0) {
            WideCharToMultiByte(CP_UTF8, 0, stringBuffer, -1, 
                               device->DeviceInfo.SerialNumber, 
                               sizeof(device->DeviceInfo.SerialNumber), NULL, NULL);
        }
    }

    /* Build complete descriptor buffer for sending to server */
    BuildDescriptorBuffer(device);

    return 0;
}

/**
 * UsbCaptureGetStringDescriptor - Read a string descriptor
 */
int UsbCaptureGetStringDescriptor(PUSB_CAPTURED_DEVICE device, uint8_t index,
                                   uint16_t langId, wchar_t* buffer, uint32_t bufferSize)
{
    USB_STRING_DESCRIPTOR stringDesc;
    ULONG transferred;
    BOOL result;

    if (!device || !device->Opened || !buffer) return -1;

    /* Get string descriptor */
    result = WinUsb_GetDescriptor(
        device->WinUsbHandle,
        USB_STRING_DESCRIPTOR_TYPE,
        index,
        langId,
        (PUCHAR)&stringDesc,
        sizeof(stringDesc),
        &transferred
    );

    if (!result || transferred < 2) {
        return -1;
    }

    /* Read full string */
    uint8_t fullBuffer[256];
    result = WinUsb_GetDescriptor(
        device->WinUsbHandle,
        USB_STRING_DESCRIPTOR_TYPE,
        index,
        langId,
        fullBuffer,
        stringDesc.bLength,
        &transferred
    );

    if (!result) {
        return -1;
    }

    /* Copy string (skip header) */
    PUSB_STRING_DESCRIPTOR fullDesc = (PUSB_STRING_DESCRIPTOR)fullBuffer;
    int charCount = (fullDesc->bLength - 2) / 2;
    if (charCount > 0 && (uint32_t)charCount < bufferSize / sizeof(wchar_t)) {
        memcpy(buffer, fullDesc->bString, charCount * sizeof(wchar_t));
        buffer[charCount] = L'\0';
    }

    return 0;
}

/**
 * UsbCaptureControlTransfer - Perform a control transfer
 */
int UsbCaptureControlTransfer(
    PUSB_CAPTURED_DEVICE device,
    PVUSB_SETUP_PACKET setupPacket,
    uint8_t* data,
    uint32_t dataLength,
    uint32_t* actualLength,
    uint32_t timeout)
{
    WINUSB_SETUP_PACKET winUsbSetup;
    ULONG transferred = 0;
    BOOL result;

    if (!device || !device->Opened || !setupPacket) return -1;

    /* Convert setup packet */
    winUsbSetup.RequestType = setupPacket->bmRequestType;
    winUsbSetup.Request = setupPacket->bRequest;
    winUsbSetup.Value = setupPacket->wValue;
    winUsbSetup.Index = setupPacket->wIndex;
    winUsbSetup.Length = setupPacket->wLength;

    /* Set timeout */
    if (timeout > 0) {
        WinUsb_SetPipePolicy(device->WinUsbHandle, 0, PIPE_TRANSFER_TIMEOUT,
                            sizeof(timeout), &timeout);
    }

    /* Perform transfer */
    result = WinUsb_ControlTransfer(
        device->WinUsbHandle,
        winUsbSetup,
        data,
        dataLength,
        &transferred,
        NULL
    );

    if (actualLength) {
        *actualLength = transferred;
    }

    if (!result) {
        DWORD error = GetLastError();
        printf("[Capture] Control transfer failed: %lu\n", error);
        device->TransferErrors++;
        return (int)error;
    }

    device->TransfersCompleted++;
    if (setupPacket->bmRequestType & 0x80) {
        device->BytesIn += transferred;
    } else {
        device->BytesOut += transferred;
    }

    return 0;
}

/**
 * UsbCaptureBulkTransfer - Perform a bulk transfer
 */
int UsbCaptureBulkTransfer(
    PUSB_CAPTURED_DEVICE device,
    uint8_t endpoint,
    uint8_t* data,
    uint32_t dataLength,
    uint32_t* actualLength,
    uint32_t timeout)
{
    ULONG transferred = 0;
    BOOL result;

    if (!device || !device->Opened) return -1;

    /* Set timeout */
    if (timeout > 0) {
        WinUsb_SetPipePolicy(device->WinUsbHandle, endpoint, PIPE_TRANSFER_TIMEOUT,
                            sizeof(timeout), &timeout);
    }

    /* Perform transfer */
    if (endpoint & 0x80) {
        /* IN transfer (device to host) */
        result = WinUsb_ReadPipe(device->WinUsbHandle, endpoint, data, dataLength,
                                  &transferred, NULL);
    } else {
        /* OUT transfer (host to device) */
        result = WinUsb_WritePipe(device->WinUsbHandle, endpoint, data, dataLength,
                                   &transferred, NULL);
    }

    if (actualLength) {
        *actualLength = transferred;
    }

    if (!result) {
        DWORD error = GetLastError();
        device->TransferErrors++;
        return (int)error;
    }

    device->TransfersCompleted++;
    if (endpoint & 0x80) {
        device->BytesIn += transferred;
    } else {
        device->BytesOut += transferred;
    }

    return 0;
}

/**
 * UsbCaptureInterruptTransfer - Perform an interrupt transfer
 */
int UsbCaptureInterruptTransfer(
    PUSB_CAPTURED_DEVICE device,
    uint8_t endpoint,
    uint8_t* data,
    uint32_t dataLength,
    uint32_t* actualLength,
    uint32_t timeout)
{
    /* Interrupt transfers use same API as bulk */
    return UsbCaptureBulkTransfer(device, endpoint, data, dataLength, actualLength, timeout);
}

/**
 * UsbCaptureAsyncBulkTransfer - Start an asynchronous bulk transfer
 */
int UsbCaptureAsyncBulkTransfer(
    PUSB_CAPTURED_DEVICE device,
    uint8_t endpoint,
    uint8_t* data,
    uint32_t dataLength,
    PUSB_ASYNC_TRANSFER transfer)
{
    BOOL result;

    if (!device || !device->Opened || !transfer) return -1;

    memset(&transfer->Overlapped, 0, sizeof(OVERLAPPED));
    transfer->Overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    transfer->Device = device;
    transfer->Endpoint = endpoint;
    transfer->Buffer = data;
    transfer->BufferLength = dataLength;

    if (endpoint & 0x80) {
        result = WinUsb_ReadPipe(device->WinUsbHandle, endpoint, data, dataLength,
                                  NULL, &transfer->Overlapped);
    } else {
        result = WinUsb_WritePipe(device->WinUsbHandle, endpoint, data, dataLength,
                                   NULL, &transfer->Overlapped);
    }

    if (!result && GetLastError() != ERROR_IO_PENDING) {
        CloseHandle(transfer->Overlapped.hEvent);
        return -1;
    }

    return 0;
}

/**
 * UsbCaptureCancelTransfer - Cancel an asynchronous transfer
 */
int UsbCaptureCancelTransfer(PUSB_ASYNC_TRANSFER transfer)
{
    if (!transfer || !transfer->Device) return -1;

    WinUsb_AbortPipe(transfer->Device->WinUsbHandle, transfer->Endpoint);
    
    if (transfer->Overlapped.hEvent) {
        CloseHandle(transfer->Overlapped.hEvent);
        transfer->Overlapped.hEvent = NULL;
    }

    return 0;
}

/* ============ Internal Helper Functions ============ */

/**
 * ParseDevicePath - Extract VID/PID from device path
 */
static int ParseDevicePath(const wchar_t* path, uint16_t* vid, uint16_t* pid)
{
    const wchar_t* vidStr = wcsstr(path, L"vid_");
    if (!vidStr) vidStr = wcsstr(path, L"VID_");
    
    const wchar_t* pidStr = wcsstr(path, L"pid_");
    if (!pidStr) pidStr = wcsstr(path, L"PID_");

    if (vidStr && pidStr) {
        *vid = (uint16_t)wcstoul(vidStr + 4, NULL, 16);
        *pid = (uint16_t)wcstoul(pidStr + 4, NULL, 16);
        return 0;
    }
    return -1;
}

/**
 * ReadDeviceDescriptor - Read USB device descriptor
 */
static int ReadDeviceDescriptor(PUSB_CAPTURED_DEVICE device)
{
    ULONG transferred;
    BOOL result;

    result = WinUsb_GetDescriptor(
        device->WinUsbHandle,
        USB_DEVICE_DESCRIPTOR_TYPE,
        0,
        0,
        (PUCHAR)&device->DeviceDescriptor,
        sizeof(USB_DEVICE_DESCRIPTOR),
        &transferred
    );

    if (!result || transferred != sizeof(USB_DEVICE_DESCRIPTOR)) {
        return -1;
    }

    /* Update device info */
    device->DeviceInfo.DeviceId = device->LocalId;
    device->DeviceInfo.VendorId = device->DeviceDescriptor.idVendor;
    device->DeviceInfo.ProductId = device->DeviceDescriptor.idProduct;
    device->DeviceInfo.DeviceClass = device->DeviceDescriptor.bDeviceClass;
    device->DeviceInfo.DeviceSubClass = device->DeviceDescriptor.bDeviceSubClass;
    device->DeviceInfo.DeviceProtocol = device->DeviceDescriptor.bDeviceProtocol;
    device->DeviceInfo.NumConfigurations = device->DeviceDescriptor.bNumConfigurations;
    
    /* Get speed */
    device->DeviceInfo.Speed = GetDeviceSpeed(device->DeviceHandle);

    return 0;
}

/**
 * ReadConfigDescriptor - Read USB configuration descriptor
 */
static int ReadConfigDescriptor(PUSB_CAPTURED_DEVICE device)
{
    USB_CONFIGURATION_DESCRIPTOR configDesc;
    uint8_t* fullConfig = NULL;
    ULONG transferred;
    BOOL result;

    /* First, get just the config descriptor header */
    result = WinUsb_GetDescriptor(
        device->WinUsbHandle,
        USB_CONFIGURATION_DESCRIPTOR_TYPE,
        0,
        0,
        (PUCHAR)&configDesc,
        sizeof(configDesc),
        &transferred
    );

    if (!result) {
        return -1;
    }

    /* Allocate buffer for full configuration */
    fullConfig = (uint8_t*)malloc(configDesc.wTotalLength);
    if (!fullConfig) {
        return -1;
    }

    /* Read full configuration */
    result = WinUsb_GetDescriptor(
        device->WinUsbHandle,
        USB_CONFIGURATION_DESCRIPTOR_TYPE,
        0,
        0,
        fullConfig,
        configDesc.wTotalLength,
        &transferred
    );

    if (!result) {
        free(fullConfig);
        return -1;
    }

    /* Parse interfaces and endpoints */
    device->NumInterfaces = configDesc.bNumInterfaces;
    device->DeviceInfo.NumInterfaces = configDesc.bNumInterfaces;

    /* Parse the configuration descriptor tree */
    uint8_t* ptr = fullConfig + sizeof(USB_CONFIGURATION_DESCRIPTOR);
    uint8_t* end = fullConfig + configDesc.wTotalLength;
    int currentInterface = -1;
    int currentEndpoint = 0;

    while (ptr < end) {
        uint8_t length = ptr[0];
        uint8_t type = ptr[1];

        if (length == 0) break;

        if (type == USB_INTERFACE_DESCRIPTOR_TYPE && length >= sizeof(USB_INTERFACE_DESCRIPTOR)) {
            PUSB_INTERFACE_DESCRIPTOR ifaceDesc = (PUSB_INTERFACE_DESCRIPTOR)ptr;
            
            if (ifaceDesc->bInterfaceNumber < MAX_USB_INTERFACES) {
                currentInterface = ifaceDesc->bInterfaceNumber;
                PUSB_INTERFACE_INFO iface = &device->Interfaces[currentInterface];
                
                iface->InterfaceNumber = ifaceDesc->bInterfaceNumber;
                iface->AlternateSetting = ifaceDesc->bAlternateSetting;
                iface->InterfaceClass = ifaceDesc->bInterfaceClass;
                iface->InterfaceSubClass = ifaceDesc->bInterfaceSubClass;
                iface->InterfaceProtocol = ifaceDesc->bInterfaceProtocol;
                iface->NumEndpoints = ifaceDesc->bNumEndpoints;
                currentEndpoint = 0;
            }
        }
        else if (type == USB_ENDPOINT_DESCRIPTOR_TYPE && length >= sizeof(USB_ENDPOINT_DESCRIPTOR)) {
            PUSB_ENDPOINT_DESCRIPTOR epDesc = (PUSB_ENDPOINT_DESCRIPTOR)ptr;
            
            if (currentInterface >= 0 && currentEndpoint < MAX_USB_ENDPOINTS) {
                PUSB_INTERFACE_INFO iface = &device->Interfaces[currentInterface];
                PUSB_ENDPOINT_INFO ep = &iface->Endpoints[currentEndpoint];
                
                ep->Address = epDesc->bEndpointAddress;
                ep->Attributes = epDesc->bmAttributes;
                ep->MaxPacketSize = epDesc->wMaxPacketSize;
                ep->Interval = epDesc->bInterval;
                currentEndpoint++;
            }
        }

        ptr += length;
    }

    free(fullConfig);
    return 0;
}

/**
 * BuildDescriptorBuffer - Build complete descriptor buffer for network transmission
 */
static int BuildDescriptorBuffer(PUSB_CAPTURED_DEVICE device)
{
    uint8_t* ptr = device->Descriptors;
    uint8_t configBuffer[4096];
    ULONG transferred;
    BOOL result;

    /* Copy device descriptor */
    memcpy(ptr, &device->DeviceDescriptor, sizeof(USB_DEVICE_DESCRIPTOR));
    ptr += sizeof(USB_DEVICE_DESCRIPTOR);

    /* Get and copy configuration descriptor */
    USB_CONFIGURATION_DESCRIPTOR configDesc;
    result = WinUsb_GetDescriptor(device->WinUsbHandle, USB_CONFIGURATION_DESCRIPTOR_TYPE,
                                   0, 0, (PUCHAR)&configDesc, sizeof(configDesc), &transferred);
    if (result) {
        result = WinUsb_GetDescriptor(device->WinUsbHandle, USB_CONFIGURATION_DESCRIPTOR_TYPE,
                                       0, 0, configBuffer, configDesc.wTotalLength, &transferred);
        if (result) {
            memcpy(ptr, configBuffer, configDesc.wTotalLength);
            ptr += configDesc.wTotalLength;
        }
    }

    device->DescriptorLength = (uint32_t)(ptr - device->Descriptors);
    return 0;
}

/**
 * GetDeviceSpeed - Get USB device speed
 */
static uint8_t GetDeviceSpeed(HANDLE deviceHandle)
{
    USB_NODE_CONNECTION_INFORMATION_EX connInfo;
    DWORD bytesReturned;

    /* This requires querying the hub, which is complex.
       For now, default to high speed. */
    return VUSB_SPEED_HIGH;
}

/**
 * UsbCaptureGetSpeedString - Get human-readable speed string
 */
const char* UsbCaptureGetSpeedString(uint8_t speed)
{
    switch (speed) {
        case VUSB_SPEED_LOW:        return "Low (1.5 Mbps)";
        case VUSB_SPEED_FULL:       return "Full (12 Mbps)";
        case VUSB_SPEED_HIGH:       return "High (480 Mbps)";
        case VUSB_SPEED_SUPER:      return "Super (5 Gbps)";
        case VUSB_SPEED_SUPER_PLUS: return "Super+ (10 Gbps)";
        default:                    return "Unknown";
    }
}

/**
 * UsbCaptureGetClassString - Get human-readable class string
 */
const char* UsbCaptureGetClassString(uint8_t deviceClass)
{
    switch (deviceClass) {
        case 0x00: return "Composite";
        case 0x01: return "Audio";
        case 0x02: return "CDC";
        case 0x03: return "HID";
        case 0x05: return "Physical";
        case 0x06: return "Image";
        case 0x07: return "Printer";
        case 0x08: return "Mass Storage";
        case 0x09: return "Hub";
        case 0x0A: return "CDC-Data";
        case 0x0B: return "Smart Card";
        case 0x0D: return "Content Security";
        case 0x0E: return "Video";
        case 0x0F: return "Healthcare";
        case 0x10: return "Audio/Video";
        case 0xDC: return "Diagnostic";
        case 0xE0: return "Wireless";
        case 0xEF: return "Miscellaneous";
        case 0xFE: return "Application Specific";
        case 0xFF: return "Vendor Specific";
        default:   return "Unknown";
    }
}

/**
 * UsbCapturePrintDeviceInfo - Print device information
 */
void UsbCapturePrintDeviceInfo(PUSB_CAPTURED_DEVICE device)
{
    if (!device) return;

    printf("\n=== USB Device Information ===\n");
    printf("Local ID:     %u\n", device->LocalId);
    printf("VID:PID:      %04X:%04X\n", device->DeviceInfo.VendorId, device->DeviceInfo.ProductId);
    printf("Class:        %s (0x%02X)\n", UsbCaptureGetClassString(device->DeviceInfo.DeviceClass),
           device->DeviceInfo.DeviceClass);
    printf("Speed:        %s\n", UsbCaptureGetSpeedString(device->DeviceInfo.Speed));
    printf("Manufacturer: %s\n", device->DeviceInfo.Manufacturer);
    printf("Product:      %s\n", device->DeviceInfo.Product);
    printf("Serial:       %s\n", device->DeviceInfo.SerialNumber);
    printf("Interfaces:   %u\n", device->NumInterfaces);
    
    for (int i = 0; i < device->NumInterfaces && i < MAX_USB_INTERFACES; i++) {
        PUSB_INTERFACE_INFO iface = &device->Interfaces[i];
        printf("  Interface %d: Class=%s (%02X:%02X:%02X), %d endpoints\n",
               iface->InterfaceNumber,
               UsbCaptureGetClassString(iface->InterfaceClass),
               iface->InterfaceClass, iface->InterfaceSubClass, iface->InterfaceProtocol,
               iface->NumEndpoints);
        
        for (int j = 0; j < iface->NumEndpoints && j < MAX_USB_ENDPOINTS; j++) {
            PUSB_ENDPOINT_INFO ep = &iface->Endpoints[j];
            const char* type;
            switch (ep->Attributes & 0x03) {
                case 0: type = "Control"; break;
                case 1: type = "Isochronous"; break;
                case 2: type = "Bulk"; break;
                case 3: type = "Interrupt"; break;
                default: type = "Unknown";
            }
            printf("    EP 0x%02X: %s %s, MaxPacket=%u\n",
                   ep->Address,
                   (ep->Address & 0x80) ? "IN " : "OUT",
                   type,
                   ep->MaxPacketSize);
        }
    }
    printf("Descriptor size: %u bytes\n", device->DescriptorLength);
    printf("==============================\n\n");
}
