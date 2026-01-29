/**
 * Virtual USB Userspace Driver Implementation
 * 
 * Provides a complete userspace implementation of virtual USB device
 * emulation and server functionality in a single module.
 */

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "vusb_userspace.h"
#include "../protocol/vusb_protocol.h"

#pragma comment(lib, "ws2_32.lib")

/* ============================================================
 * Internal Helper Functions
 * ============================================================ */

static uint64_t GetTimestampMs(void)
{
    LARGE_INTEGER freq, counter;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    return (uint64_t)(counter.QuadPart * 1000 / freq.QuadPart);
}

static void LogMessage(PVUSB_US_CONTEXT ctx, const char* fmt, ...)
{
    if (!ctx->Config.EnableLogging) return;
    
    va_list args;
    char buffer[512];
    
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    
    uint64_t elapsed = GetTimestampMs() - ctx->StartTime;
    printf("[%llu.%03llu] %s\n", elapsed / 1000, elapsed % 1000, buffer);
}

/* ============================================================
 * Device Management
 * ============================================================ */

static PVUSB_US_DEVICE FindFreeDeviceSlot(PVUSB_US_CONTEXT ctx)
{
    for (int i = 0; i < VUSB_US_MAX_DEVICES; i++) {
        if (!ctx->Devices[i].Active) {
            return &ctx->Devices[i];
        }
    }
    return NULL;
}

static void InitializeDevice(PVUSB_US_DEVICE device)
{
    memset(device, 0, sizeof(VUSB_US_DEVICE));
    InitializeCriticalSection(&device->UrbLock);
    
    /* Initialize endpoints */
    for (int i = 0; i < VUSB_US_MAX_ENDPOINTS; i++) {
        InitializeCriticalSection(&device->Endpoints[i].Lock);
        device->Endpoints[i].DataEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    }
}

static void CleanupDevice(PVUSB_US_DEVICE device)
{
    /* Cancel all pending URBs */
    EnterCriticalSection(&device->UrbLock);
    PVUSB_US_PENDING_URB urb = device->PendingUrbs;
    while (urb) {
        PVUSB_US_PENDING_URB next = urb->Next;
        if (urb->TransferBuffer) free(urb->TransferBuffer);
        if (urb->CompletionEvent) CloseHandle(urb->CompletionEvent);
        free(urb);
        urb = next;
    }
    device->PendingUrbs = NULL;
    device->PendingUrbCount = 0;
    LeaveCriticalSection(&device->UrbLock);
    
    /* Cleanup endpoints */
    for (int i = 0; i < VUSB_US_MAX_ENDPOINTS; i++) {
        if (device->Endpoints[i].Buffer) {
            free(device->Endpoints[i].Buffer);
            device->Endpoints[i].Buffer = NULL;
        }
        if (device->Endpoints[i].DataEvent) {
            CloseHandle(device->Endpoints[i].DataEvent);
        }
        DeleteCriticalSection(&device->Endpoints[i].Lock);
    }
    
    DeleteCriticalSection(&device->UrbLock);
    
    if (device->Descriptors) {
        free(device->Descriptors);
        device->Descriptors = NULL;
    }
    
    device->Active = FALSE;
}

int VusbUsCreateDevice(PVUSB_US_CONTEXT ctx, PVUSB_DEVICE_INFO deviceInfo,
                       uint8_t* descriptors, uint32_t descriptorLength,
                       uint32_t* deviceId)
{
    if (!ctx || !deviceInfo || !deviceId) return -1;
    
    EnterCriticalSection(&ctx->DeviceLock);
    
    PVUSB_US_DEVICE device = FindFreeDeviceSlot(ctx);
    if (!device) {
        LeaveCriticalSection(&ctx->DeviceLock);
        return -1;
    }
    
    InitializeDevice(device);
    
    device->Active = TRUE;
    device->DeviceId = ++ctx->NextDeviceId;
    device->State = VUSB_US_DEV_ATTACHED;
    memcpy(&device->DeviceInfo, deviceInfo, sizeof(VUSB_DEVICE_INFO));
    device->DeviceInfo.DeviceId = device->DeviceId;
    
    /* Copy descriptors */
    if (descriptors && descriptorLength > 0) {
        device->Descriptors = (uint8_t*)malloc(descriptorLength);
        if (device->Descriptors) {
            memcpy(device->Descriptors, descriptors, descriptorLength);
            device->DescriptorLength = descriptorLength;
        }
    }
    
    *deviceId = device->DeviceId;
    
    LeaveCriticalSection(&ctx->DeviceLock);
    
    LogMessage(ctx, "Device created: ID=%u VID=%04X PID=%04X (%s)",
               device->DeviceId, deviceInfo->VendorId, deviceInfo->ProductId,
               deviceInfo->Product);
    
    return 0;
}

int VusbUsDestroyDevice(PVUSB_US_CONTEXT ctx, uint32_t deviceId)
{
    if (!ctx) return -1;
    
    EnterCriticalSection(&ctx->DeviceLock);
    
    for (int i = 0; i < VUSB_US_MAX_DEVICES; i++) {
        if (ctx->Devices[i].Active && ctx->Devices[i].DeviceId == deviceId) {
            CleanupDevice(&ctx->Devices[i]);
            LeaveCriticalSection(&ctx->DeviceLock);
            LogMessage(ctx, "Device destroyed: ID=%u", deviceId);
            return 0;
        }
    }
    
    LeaveCriticalSection(&ctx->DeviceLock);
    return -1;
}

PVUSB_US_DEVICE VusbUsGetDevice(PVUSB_US_CONTEXT ctx, uint32_t deviceId)
{
    if (!ctx) return NULL;
    
    for (int i = 0; i < VUSB_US_MAX_DEVICES; i++) {
        if (ctx->Devices[i].Active && ctx->Devices[i].DeviceId == deviceId) {
            return &ctx->Devices[i];
        }
    }
    return NULL;
}

/* ============================================================
 * URB Processing
 * ============================================================ */

static PVUSB_US_PENDING_URB AllocateUrb(void)
{
    PVUSB_US_PENDING_URB urb = (PVUSB_US_PENDING_URB)calloc(1, sizeof(VUSB_US_PENDING_URB));
    if (urb) {
        urb->CompletionEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    }
    return urb;
}

static void FreeUrb(PVUSB_US_PENDING_URB urb)
{
    if (!urb) return;
    if (urb->TransferBuffer) free(urb->TransferBuffer);
    if (urb->CompletionEvent) CloseHandle(urb->CompletionEvent);
    free(urb);
}

int VusbUsSubmitUrb(PVUSB_US_CONTEXT ctx, uint32_t deviceId, 
                    PVUSB_US_PENDING_URB urb)
{
    if (!ctx || !urb) return -1;
    
    PVUSB_US_DEVICE device = VusbUsGetDevice(ctx, deviceId);
    if (!device) return -1;
    
    EnterCriticalSection(&device->UrbLock);
    
    urb->UrbId = ++device->NextUrbId;
    urb->SubmitTime = GetTimestampMs();
    urb->Completed = FALSE;
    
    /* Add to pending list */
    urb->Next = device->PendingUrbs;
    device->PendingUrbs = urb;
    device->PendingUrbCount++;
    device->UrbsSubmitted++;
    
    LeaveCriticalSection(&device->UrbLock);
    
    ctx->TotalUrbsProcessed++;
    
    return 0;
}

int VusbUsCompleteUrb(PVUSB_US_CONTEXT ctx, uint32_t deviceId, 
                      uint32_t urbId, uint32_t status,
                      uint8_t* data, uint32_t length)
{
    if (!ctx) return -1;
    
    PVUSB_US_DEVICE device = VusbUsGetDevice(ctx, deviceId);
    if (!device) return -1;
    
    EnterCriticalSection(&device->UrbLock);
    
    /* Find URB */
    PVUSB_US_PENDING_URB* pUrb = &device->PendingUrbs;
    while (*pUrb && (*pUrb)->UrbId != urbId) {
        pUrb = &(*pUrb)->Next;
    }
    
    if (!*pUrb) {
        LeaveCriticalSection(&device->UrbLock);
        return -1;
    }
    
    PVUSB_US_PENDING_URB urb = *pUrb;
    
    /* Complete the URB */
    urb->Status = status;
    urb->ActualLength = length;
    urb->Completed = TRUE;
    
    /* Copy data for IN transfers */
    if (data && length > 0 && urb->Direction == VUSB_DIR_IN) {
        if (urb->TransferBuffer && urb->TransferBufferLength >= length) {
            memcpy(urb->TransferBuffer, data, length);
        }
    }
    
    /* Update statistics */
    if (urb->Direction == VUSB_DIR_IN) {
        device->BytesIn += length;
    } else {
        device->BytesOut += urb->ActualLength;
    }
    device->UrbsCompleted++;
    
    /* Signal completion */
    if (urb->CompletionEvent) {
        SetEvent(urb->CompletionEvent);
    }
    
    /* Call completion callback */
    if (urb->CompletionCallback) {
        urb->CompletionCallback(urb, urb->CallbackContext);
    }
    
    /* Remove from pending list */
    *pUrb = urb->Next;
    device->PendingUrbCount--;
    
    LeaveCriticalSection(&device->UrbLock);
    
    ctx->TotalBytesTransferred += length;
    
    return 0;
}

int VusbUsCancelUrb(PVUSB_US_CONTEXT ctx, uint32_t deviceId, uint32_t urbId)
{
    return VusbUsCompleteUrb(ctx, deviceId, urbId, VUSB_STATUS_CANCELED, NULL, 0);
}

/* ============================================================
 * Standard USB Request Handling
 * ============================================================ */

static int HandleStandardRequest(PVUSB_US_CONTEXT ctx, PVUSB_US_DEVICE device,
                                 PVUSB_SETUP_PACKET setup, uint8_t* buffer,
                                 uint32_t* length)
{
    uint8_t requestType = setup->bmRequestType & 0x60; /* Request type bits */
    
    if (requestType != 0) {
        /* Not a standard request, let gadget handler deal with it */
        return -1;
    }
    
    switch (setup->bRequest) {
    case 0x00: /* GET_STATUS */
        buffer[0] = 0;
        buffer[1] = 0;
        *length = 2;
        return 0;
        
    case 0x01: /* CLEAR_FEATURE */
        *length = 0;
        return 0;
        
    case 0x03: /* SET_FEATURE */
        *length = 0;
        return 0;
        
    case 0x05: /* SET_ADDRESS */
        device->Address = (uint8_t)(setup->wValue & 0x7F);
        device->State = VUSB_US_DEV_ADDRESSED;
        *length = 0;
        LogMessage(ctx, "Device %u: SET_ADDRESS %u", device->DeviceId, device->Address);
        return 0;
        
    case 0x06: /* GET_DESCRIPTOR */
        {
            uint8_t descType = (setup->wValue >> 8) & 0xFF;
            uint8_t descIndex = setup->wValue & 0xFF;
            
            /* Search descriptors */
            if (device->Descriptors && device->DescriptorLength > 0) {
                uint32_t offset = 0;
                while (offset < device->DescriptorLength) {
                    uint8_t len = device->Descriptors[offset];
                    uint8_t type = device->Descriptors[offset + 1];
                    
                    if (len == 0) break;
                    
                    if (type == descType) {
                        if (descIndex == 0) {
                            uint32_t copyLen = len;
                            if (copyLen > setup->wLength) copyLen = setup->wLength;
                            memcpy(buffer, &device->Descriptors[offset], copyLen);
                            *length = copyLen;
                            return 0;
                        }
                        descIndex--;
                    }
                    offset += len;
                }
            }
            
            /* Descriptor not found */
            return -1;
        }
        
    case 0x08: /* GET_CONFIGURATION */
        buffer[0] = device->Configuration;
        *length = 1;
        return 0;
        
    case 0x09: /* SET_CONFIGURATION */
        device->Configuration = (uint8_t)(setup->wValue & 0xFF);
        if (device->Configuration > 0) {
            device->State = VUSB_US_DEV_CONFIGURED;
        }
        *length = 0;
        LogMessage(ctx, "Device %u: SET_CONFIGURATION %u", 
                   device->DeviceId, device->Configuration);
        
        /* Notify gadget */
        if (ctx->GadgetOps && ctx->GadgetOps->HandleSetConfiguration) {
            ctx->GadgetOps->HandleSetConfiguration(device, device->Configuration);
        }
        return 0;
        
    case 0x0A: /* GET_INTERFACE */
        buffer[0] = 0;
        *length = 1;
        return 0;
        
    case 0x0B: /* SET_INTERFACE */
        *length = 0;
        if (ctx->GadgetOps && ctx->GadgetOps->HandleSetInterface) {
            ctx->GadgetOps->HandleSetInterface(device, 
                                               (uint8_t)(setup->wIndex & 0xFF),
                                               (uint8_t)(setup->wValue & 0xFF));
        }
        return 0;
        
    default:
        return -1;
    }
}

/* ============================================================
 * Client Message Processing
 * ============================================================ */

static void SendResponse(PVUSB_US_CLIENT client, void* data, uint32_t length)
{
    send(client->Socket, (char*)data, length, 0);
}

static void HandleClientConnect(PVUSB_US_CONTEXT ctx, PVUSB_US_CLIENT client,
                                PVUSB_HEADER header, uint8_t* payload)
{
    VUSB_CONNECT_RESPONSE response;
    
    LogMessage(ctx, "Client %s connecting...", client->AddressString);
    
    /* Parse connect request */
    if (header->Length >= sizeof(VUSB_CONNECT_REQUEST) - sizeof(VUSB_HEADER)) {
        VUSB_CONNECT_REQUEST* req = (VUSB_CONNECT_REQUEST*)(payload - sizeof(VUSB_HEADER));
        client->ClientVersion = req->ClientVersion;
        client->Capabilities = req->Capabilities;
        strncpy(client->ClientName, req->ClientName, sizeof(client->ClientName) - 1);
    }
    
    client->Authenticated = TRUE;
    
    /* Build response */
    VusbInitHeader(&response.Header, VUSB_CMD_CONNECT,
                   sizeof(response) - sizeof(VUSB_HEADER), header->Sequence);
    response.Status = VUSB_STATUS_SUCCESS;
    response.ServerVersion = 0x00010000;
    response.Capabilities = 0;
    response.SessionId = client->SessionId;
    
    SendResponse(client, &response, sizeof(response));
    
    LogMessage(ctx, "Client %s connected (session %u, name: %s)",
               client->AddressString, client->SessionId, client->ClientName);
}

static void HandleDeviceAttach(PVUSB_US_CONTEXT ctx, PVUSB_US_CLIENT client,
                               PVUSB_HEADER header, uint8_t* payload, uint32_t payloadLen)
{
    VUSB_DEVICE_ATTACH_RESPONSE response;
    
    if (payloadLen < sizeof(VUSB_DEVICE_INFO) + sizeof(uint32_t)) {
        VusbInitHeader(&response.Header, VUSB_CMD_DEVICE_ATTACH,
                       sizeof(response) - sizeof(VUSB_HEADER), header->Sequence);
        response.Status = VUSB_STATUS_INVALID_PARAM;
        response.DeviceId = 0;
        SendResponse(client, &response, sizeof(response));
        return;
    }
    
    VUSB_DEVICE_INFO* deviceInfo = (VUSB_DEVICE_INFO*)payload;
    uint32_t descLen = *(uint32_t*)(payload + sizeof(VUSB_DEVICE_INFO));
    uint8_t* descriptors = payload + sizeof(VUSB_DEVICE_INFO) + sizeof(uint32_t);
    
    LogMessage(ctx, "Device attach: VID=%04X PID=%04X (%s - %s)",
               deviceInfo->VendorId, deviceInfo->ProductId,
               deviceInfo->Manufacturer, deviceInfo->Product);
    
    uint32_t deviceId = 0;
    int result = VusbUsCreateDevice(ctx, deviceInfo, descriptors, descLen, &deviceId);
    
    if (result == 0) {
        /* Track device ownership */
        PVUSB_US_DEVICE device = VusbUsGetDevice(ctx, deviceId);
        if (device) {
            device->OwnerClient = client;
            device->RemoteDeviceId = deviceInfo->DeviceId;
        }
        
        /* Track in client */
        if (client->DeviceCount < VUSB_US_MAX_DEVICES) {
            client->DeviceIds[client->DeviceCount++] = deviceId;
        }
    }
    
    VusbInitHeader(&response.Header, VUSB_CMD_DEVICE_ATTACH,
                   sizeof(response) - sizeof(VUSB_HEADER), header->Sequence);
    response.Status = (result == 0) ? VUSB_STATUS_SUCCESS : VUSB_STATUS_ERROR;
    response.DeviceId = deviceId;
    
    SendResponse(client, &response, sizeof(response));
}

static void HandleDeviceDetach(PVUSB_US_CONTEXT ctx, PVUSB_US_CLIENT client,
                               PVUSB_HEADER header, uint8_t* payload)
{
    VUSB_DEVICE_DETACH_REQUEST* req = (VUSB_DEVICE_DETACH_REQUEST*)(payload - sizeof(VUSB_HEADER));
    uint32_t deviceId = req->DeviceId;
    
    LogMessage(ctx, "Device detach: ID=%u", deviceId);
    
    /* Verify ownership */
    PVUSB_US_DEVICE device = VusbUsGetDevice(ctx, deviceId);
    if (device && device->OwnerClient == client) {
        VusbUsDestroyDevice(ctx, deviceId);
        
        /* Remove from client tracking */
        for (int i = 0; i < client->DeviceCount; i++) {
            if (client->DeviceIds[i] == deviceId) {
                client->DeviceIds[i] = client->DeviceIds[--client->DeviceCount];
                break;
            }
        }
    }
    
    /* Send status response */
    VUSB_HEADER resp;
    VusbInitHeader(&resp, VUSB_CMD_STATUS, 0, header->Sequence);
    SendResponse(client, &resp, sizeof(resp));
}

static void HandleUrbComplete(PVUSB_US_CONTEXT ctx, PVUSB_US_CLIENT client,
                              PVUSB_HEADER header, uint8_t* payload, uint32_t payloadLen)
{
    UNREFERENCED_PARAMETER(client);
    
    if (payloadLen < sizeof(VUSB_URB_COMPLETE) - sizeof(VUSB_HEADER)) {
        return;
    }
    
    VUSB_URB_COMPLETE* complete = (VUSB_URB_COMPLETE*)(payload - sizeof(VUSB_HEADER));
    
    uint8_t* data = NULL;
    if (complete->ActualLength > 0) {
        data = payload + sizeof(VUSB_URB_COMPLETE) - sizeof(VUSB_HEADER);
    }
    
    /* Find device by remote ID and complete URB */
    EnterCriticalSection(&ctx->DeviceLock);
    for (int i = 0; i < VUSB_US_MAX_DEVICES; i++) {
        PVUSB_US_DEVICE device = &ctx->Devices[i];
        if (device->Active && device->RemoteDeviceId == complete->DeviceId) {
            VusbUsCompleteUrb(ctx, device->DeviceId, complete->UrbId,
                              complete->Status, data, complete->ActualLength);
            break;
        }
    }
    LeaveCriticalSection(&ctx->DeviceLock);
    
    UNREFERENCED_PARAMETER(header);
}

static void HandleDeviceList(PVUSB_US_CONTEXT ctx, PVUSB_US_CLIENT client,
                             PVUSB_HEADER header)
{
    uint8_t buffer[sizeof(VUSB_DEVICE_LIST_RESPONSE) + 
                   VUSB_US_MAX_DEVICES * sizeof(VUSB_DEVICE_INFO)];
    VUSB_DEVICE_LIST_RESPONSE* response = (VUSB_DEVICE_LIST_RESPONSE*)buffer;
    VUSB_DEVICE_INFO* devices = (VUSB_DEVICE_INFO*)(buffer + sizeof(VUSB_DEVICE_LIST_RESPONSE));
    
    int count = 0;
    
    EnterCriticalSection(&ctx->DeviceLock);
    for (int i = 0; i < VUSB_US_MAX_DEVICES && count < VUSB_US_MAX_DEVICES; i++) {
        if (ctx->Devices[i].Active) {
            memcpy(&devices[count], &ctx->Devices[i].DeviceInfo, sizeof(VUSB_DEVICE_INFO));
            count++;
        }
    }
    LeaveCriticalSection(&ctx->DeviceLock);
    
    uint32_t payloadLen = sizeof(uint32_t) * 2 + count * sizeof(VUSB_DEVICE_INFO);
    VusbInitHeader(&response->Header, VUSB_CMD_DEVICE_LIST, payloadLen, header->Sequence);
    response->Status = VUSB_STATUS_SUCCESS;
    response->DeviceCount = count;
    
    SendResponse(client, buffer, sizeof(VUSB_HEADER) + payloadLen);
}

static void HandlePing(PVUSB_US_CLIENT client, PVUSB_HEADER header)
{
    VUSB_HEADER response;
    VusbInitHeader(&response, VUSB_CMD_PONG, 0, header->Sequence);
    SendResponse(client, &response, sizeof(response));
}

static void ProcessClientMessage(PVUSB_US_CONTEXT ctx, PVUSB_US_CLIENT client,
                                 PVUSB_HEADER header, uint8_t* payload, uint32_t payloadLen)
{
    switch (header->Command) {
    case VUSB_CMD_CONNECT:
        HandleClientConnect(ctx, client, header, payload);
        break;
        
    case VUSB_CMD_DISCONNECT:
        client->Connected = FALSE;
        break;
        
    case VUSB_CMD_PING:
        HandlePing(client, header);
        break;
        
    case VUSB_CMD_DEVICE_ATTACH:
        HandleDeviceAttach(ctx, client, header, payload, payloadLen);
        break;
        
    case VUSB_CMD_DEVICE_DETACH:
        HandleDeviceDetach(ctx, client, header, payload);
        break;
        
    case VUSB_CMD_URB_COMPLETE:
        HandleUrbComplete(ctx, client, header, payload, payloadLen);
        break;
        
    case VUSB_CMD_DEVICE_LIST:
        HandleDeviceList(ctx, client, header);
        break;
        
    default:
        LogMessage(ctx, "Unknown command: 0x%04X from %s", 
                   header->Command, client->AddressString);
        break;
    }
}

/* ============================================================
 * Client Thread
 * ============================================================ */

static DWORD WINAPI ClientThread(LPVOID param)
{
    PVUSB_US_CLIENT client = (PVUSB_US_CLIENT)param;
    PVUSB_US_CONTEXT ctx = client->Context;
    VUSB_HEADER header;
    uint8_t* buffer = NULL;
    int result;
    
    LogMessage(ctx, "Client thread started for session %u", client->SessionId);
    
    buffer = (uint8_t*)malloc(VUSB_MAX_PACKET_SIZE);
    if (!buffer) {
        goto cleanup;
    }
    
    while (client->Connected && ctx->Running) {
        /* Receive header */
        result = recv(client->Socket, (char*)&header, sizeof(header), MSG_WAITALL);
        if (result != sizeof(header)) {
            if (result == 0) {
                LogMessage(ctx, "Client %s closed connection", client->AddressString);
            }
            break;
        }
        
        /* Validate header */
        if (!VusbValidateHeader(&header)) {
            LogMessage(ctx, "Invalid protocol header from %s", client->AddressString);
            break;
        }
        
        /* Receive payload */
        if (header.Length > 0) {
            if (header.Length > VUSB_MAX_PACKET_SIZE - sizeof(header)) {
                LogMessage(ctx, "Payload too large: %u", header.Length);
                break;
            }
            
            result = recv(client->Socket, (char*)buffer, header.Length, MSG_WAITALL);
            if (result != (int)header.Length) {
                LogMessage(ctx, "Failed to receive payload");
                break;
            }
        }
        
        /* Process message */
        ProcessClientMessage(ctx, client, &header, buffer, header.Length);
    }
    
cleanup:
    client->Connected = FALSE;
    
    if (buffer) {
        free(buffer);
    }
    
    /* Cleanup client devices */
    EnterCriticalSection(&ctx->DeviceLock);
    for (int i = 0; i < client->DeviceCount; i++) {
        for (int j = 0; j < VUSB_US_MAX_DEVICES; j++) {
            if (ctx->Devices[j].Active && 
                ctx->Devices[j].DeviceId == client->DeviceIds[i]) {
                CleanupDevice(&ctx->Devices[j]);
            }
        }
    }
    LeaveCriticalSection(&ctx->DeviceLock);
    
    /* Remove from client list */
    EnterCriticalSection(&ctx->ClientLock);
    for (int i = 0; i < VUSB_US_MAX_CLIENTS; i++) {
        if (ctx->Clients[i] == client) {
            ctx->Clients[i] = NULL;
            ctx->ClientCount--;
            break;
        }
    }
    LeaveCriticalSection(&ctx->ClientLock);
    
    LogMessage(ctx, "Client %s disconnected (session %u)", 
               client->AddressString, client->SessionId);
    
    closesocket(client->Socket);
    free(client);
    
    return 0;
}

/* ============================================================
 * Endpoint Operations
 * ============================================================ */

int VusbUsEpWrite(PVUSB_US_DEVICE device, uint8_t endpoint,
                  uint8_t* data, uint32_t length)
{
    if (!device || !data) return -1;
    
    int epIndex = endpoint & 0x0F;
    if (epIndex >= VUSB_US_MAX_ENDPOINTS) return -1;
    
    PVUSB_US_ENDPOINT ep = &device->Endpoints[epIndex];
    
    EnterCriticalSection(&ep->Lock);
    
    /* Allocate buffer if needed */
    if (!ep->Buffer) {
        ep->Buffer = (uint8_t*)malloc(VUSB_US_URB_BUFFER_SIZE);
        ep->BufferSize = VUSB_US_URB_BUFFER_SIZE;
    }
    
    if (!ep->Buffer || length > ep->BufferSize) {
        LeaveCriticalSection(&ep->Lock);
        return -1;
    }
    
    memcpy(ep->Buffer, data, length);
    ep->DataLength = length;
    ep->DataOffset = 0;
    
    SetEvent(ep->DataEvent);
    
    LeaveCriticalSection(&ep->Lock);
    
    return length;
}

int VusbUsEpRead(PVUSB_US_DEVICE device, uint8_t endpoint,
                 uint8_t* buffer, uint32_t maxLength)
{
    if (!device || !buffer) return -1;
    
    int epIndex = endpoint & 0x0F;
    if (epIndex >= VUSB_US_MAX_ENDPOINTS) return -1;
    
    PVUSB_US_ENDPOINT ep = &device->Endpoints[epIndex];
    
    EnterCriticalSection(&ep->Lock);
    
    if (!ep->Buffer || ep->DataLength == 0) {
        LeaveCriticalSection(&ep->Lock);
        return 0;
    }
    
    uint32_t available = ep->DataLength - ep->DataOffset;
    uint32_t toRead = (available < maxLength) ? available : maxLength;
    
    memcpy(buffer, ep->Buffer + ep->DataOffset, toRead);
    ep->DataOffset += toRead;
    
    if (ep->DataOffset >= ep->DataLength) {
        ep->DataLength = 0;
        ep->DataOffset = 0;
    }
    
    LeaveCriticalSection(&ep->Lock);
    
    return toRead;
}

void VusbUsEpStall(PVUSB_US_DEVICE device, uint8_t endpoint)
{
    if (!device) return;
    
    int epIndex = endpoint & 0x0F;
    if (epIndex < VUSB_US_MAX_ENDPOINTS) {
        device->Endpoints[epIndex].State = VUSB_US_EP_STALLED;
    }
}

void VusbUsEpUnstall(PVUSB_US_DEVICE device, uint8_t endpoint)
{
    if (!device) return;
    
    int epIndex = endpoint & 0x0F;
    if (epIndex < VUSB_US_MAX_ENDPOINTS) {
        device->Endpoints[epIndex].State = VUSB_US_EP_ENABLED;
    }
}

/* ============================================================
 * Gadget Operations
 * ============================================================ */

void VusbUsSetGadgetOps(PVUSB_US_CONTEXT ctx, PVUSB_US_GADGET_OPS ops)
{
    if (ctx) {
        ctx->GadgetOps = ops;
    }
}

/* ============================================================
 * Capture Functions
 * ============================================================ */

int VusbUsStartCapture(PVUSB_US_CONTEXT ctx, const char* filename)
{
    if (!ctx || !filename) return -1;
    
    EnterCriticalSection(&ctx->CaptureLock);
    
    if (ctx->CaptureFile != INVALID_HANDLE_VALUE) {
        LeaveCriticalSection(&ctx->CaptureLock);
        return -1; /* Already capturing */
    }
    
    ctx->CaptureFile = CreateFileA(filename, GENERIC_WRITE, 0, NULL,
                                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    
    if (ctx->CaptureFile == INVALID_HANDLE_VALUE) {
        LeaveCriticalSection(&ctx->CaptureLock);
        return -1;
    }
    
    /* Write header */
    const char* magic = "VUSB_CAP";
    DWORD written;
    WriteFile(ctx->CaptureFile, magic, 8, &written, NULL);
    
    LeaveCriticalSection(&ctx->CaptureLock);
    
    LogMessage(ctx, "Started capture to %s", filename);
    return 0;
}

void VusbUsStopCapture(PVUSB_US_CONTEXT ctx)
{
    if (!ctx) return;
    
    EnterCriticalSection(&ctx->CaptureLock);
    
    if (ctx->CaptureFile != INVALID_HANDLE_VALUE) {
        CloseHandle(ctx->CaptureFile);
        ctx->CaptureFile = INVALID_HANDLE_VALUE;
        LogMessage(ctx, "Stopped capture");
    }
    
    LeaveCriticalSection(&ctx->CaptureLock);
}

/* ============================================================
 * Statistics
 * ============================================================ */

void VusbUsGetStats(PVUSB_US_CONTEXT ctx, VUSB_STATISTICS* stats)
{
    if (!ctx || !stats) return;
    
    memset(stats, 0, sizeof(VUSB_STATISTICS));
    
    EnterCriticalSection(&ctx->DeviceLock);
    
    for (int i = 0; i < VUSB_US_MAX_DEVICES; i++) {
        if (ctx->Devices[i].Active) {
            stats->ActiveDevices++;
            stats->TotalUrbsSubmitted += ctx->Devices[i].UrbsSubmitted;
            stats->TotalUrbsCompleted += ctx->Devices[i].UrbsCompleted;
            stats->TotalBytesIn += ctx->Devices[i].BytesIn;
            stats->TotalBytesOut += ctx->Devices[i].BytesOut;
            stats->PendingUrbs += ctx->Devices[i].PendingUrbCount;
        }
    }
    
    LeaveCriticalSection(&ctx->DeviceLock);
}

int VusbUsListDevices(PVUSB_US_CONTEXT ctx, VUSB_DEVICE_INFO* list, int maxDevices)
{
    if (!ctx || !list) return 0;
    
    int count = 0;
    
    EnterCriticalSection(&ctx->DeviceLock);
    
    for (int i = 0; i < VUSB_US_MAX_DEVICES && count < maxDevices; i++) {
        if (ctx->Devices[i].Active) {
            memcpy(&list[count], &ctx->Devices[i].DeviceInfo, sizeof(VUSB_DEVICE_INFO));
            count++;
        }
    }
    
    LeaveCriticalSection(&ctx->DeviceLock);
    
    return count;
}

void VusbUsListClients(PVUSB_US_CONTEXT ctx, 
                       void (*callback)(PVUSB_US_CLIENT, void*),
                       void* userdata)
{
    if (!ctx || !callback) return;
    
    EnterCriticalSection(&ctx->ClientLock);
    
    for (int i = 0; i < VUSB_US_MAX_CLIENTS; i++) {
        if (ctx->Clients[i]) {
            callback(ctx->Clients[i], userdata);
        }
    }
    
    LeaveCriticalSection(&ctx->ClientLock);
}

/* ============================================================
 * Core API Implementation
 * ============================================================ */

int VusbUsInit(PVUSB_US_CONTEXT ctx, PVUSB_US_CONFIG config)
{
    WSADATA wsaData;
    int result;
    
    if (!ctx || !config) return -1;
    
    memset(ctx, 0, sizeof(VUSB_US_CONTEXT));
    ctx->Config = *config;
    ctx->Running = FALSE;
    ctx->ListenSocket = INVALID_SOCKET;
    ctx->CaptureFile = INVALID_HANDLE_VALUE;
    ctx->StartTime = GetTimestampMs();
    
    /* Initialize Winsock */
    result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        fprintf(stderr, "WSAStartup failed: %d\n", result);
        return result;
    }
    
    /* Initialize synchronization */
    InitializeCriticalSection(&ctx->ClientLock);
    InitializeCriticalSection(&ctx->DeviceLock);
    InitializeCriticalSection(&ctx->CaptureLock);
    
    ctx->ShutdownEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    
    ctx->Initialized = TRUE;
    
    LogMessage(ctx, "Userspace server initialized");
    return 0;
}

void VusbUsCleanup(PVUSB_US_CONTEXT ctx)
{
    if (!ctx || !ctx->Initialized) return;
    
    VusbUsStop(ctx);
    
    /* Cleanup devices */
    EnterCriticalSection(&ctx->DeviceLock);
    for (int i = 0; i < VUSB_US_MAX_DEVICES; i++) {
        if (ctx->Devices[i].Active) {
            CleanupDevice(&ctx->Devices[i]);
        }
    }
    LeaveCriticalSection(&ctx->DeviceLock);
    
    /* Stop capture */
    VusbUsStopCapture(ctx);
    
    /* Cleanup synchronization */
    DeleteCriticalSection(&ctx->ClientLock);
    DeleteCriticalSection(&ctx->DeviceLock);
    DeleteCriticalSection(&ctx->CaptureLock);
    
    if (ctx->ShutdownEvent) {
        CloseHandle(ctx->ShutdownEvent);
    }
    
    WSACleanup();
    
    ctx->Initialized = FALSE;
    
    printf("Userspace server cleaned up\n");
}

int VusbUsRun(PVUSB_US_CONTEXT ctx)
{
    if (!ctx || !ctx->Initialized) return -1;
    
    SOCKET listenSocket = INVALID_SOCKET;
    struct sockaddr_in serverAddr;
    int result;
    
    /* Create listening socket */
    listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == INVALID_SOCKET) {
        fprintf(stderr, "socket() failed: %d\n", WSAGetLastError());
        return -1;
    }
    
    /* Allow address reuse */
    int optval = 1;
    setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, (char*)&optval, sizeof(optval));
    
    /* Bind */
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(ctx->Config.Port);
    
    result = bind(listenSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr));
    if (result == SOCKET_ERROR) {
        fprintf(stderr, "bind() failed: %d\n", WSAGetLastError());
        closesocket(listenSocket);
        return -1;
    }
    
    /* Listen */
    result = listen(listenSocket, SOMAXCONN);
    if (result == SOCKET_ERROR) {
        fprintf(stderr, "listen() failed: %d\n", WSAGetLastError());
        closesocket(listenSocket);
        return -1;
    }
    
    ctx->ListenSocket = listenSocket;
    ctx->Running = TRUE;
    
    printf("\n");
    printf("=====================================\n");
    printf(" Virtual USB Userspace Server\n");
    printf("=====================================\n");
    printf(" Port: %d\n", ctx->Config.Port);
    printf(" Max clients: %d\n", ctx->Config.MaxClients);
    printf(" Max devices: %d\n", ctx->Config.MaxDevices);
    printf(" Simulation: %s\n", ctx->Config.EnableSimulation ? "enabled" : "disabled");
    printf(" Logging: %s\n", ctx->Config.EnableLogging ? "enabled" : "disabled");
    printf("=====================================\n");
    printf("\nListening for connections...\n");
    printf("Press Ctrl+C to stop.\n\n");
    
    /* Accept loop */
    while (ctx->Running) {
        struct sockaddr_in clientAddr;
        int clientAddrLen = sizeof(clientAddr);
        SOCKET clientSocket;
        
        /* Use select with timeout for graceful shutdown */
        fd_set readfds;
        struct timeval tv;
        FD_ZERO(&readfds);
        FD_SET(listenSocket, &readfds);
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        
        result = select(0, &readfds, NULL, NULL, &tv);
        if (result <= 0) continue;
        
        clientSocket = accept(listenSocket, (struct sockaddr*)&clientAddr, &clientAddrLen);
        if (clientSocket == INVALID_SOCKET) {
            if (ctx->Running) {
                fprintf(stderr, "accept() failed: %d\n", WSAGetLastError());
            }
            continue;
        }
        
        /* Allocate client */
        PVUSB_US_CLIENT client = (PVUSB_US_CLIENT)calloc(1, sizeof(VUSB_US_CLIENT));
        if (!client) {
            closesocket(clientSocket);
            continue;
        }
        
        client->Socket = clientSocket;
        client->Context = ctx;
        client->SessionId = ++ctx->NextSessionId;
        client->Connected = TRUE;
        memcpy(&client->Address, &clientAddr, sizeof(clientAddr));
        inet_ntop(AF_INET, &clientAddr.sin_addr, client->AddressString, 
                  sizeof(client->AddressString));
        
        /* Add to client list */
        EnterCriticalSection(&ctx->ClientLock);
        BOOL added = FALSE;
        for (int i = 0; i < VUSB_US_MAX_CLIENTS; i++) {
            if (!ctx->Clients[i]) {
                ctx->Clients[i] = client;
                ctx->ClientCount++;
                added = TRUE;
                break;
            }
        }
        LeaveCriticalSection(&ctx->ClientLock);
        
        if (!added) {
            LogMessage(ctx, "Server full, rejecting connection from %s", 
                       client->AddressString);
            closesocket(clientSocket);
            free(client);
            continue;
        }
        
        LogMessage(ctx, "New connection from %s:%d", 
                   client->AddressString, ntohs(clientAddr.sin_port));
        
        /* Start client thread */
        client->Thread = CreateThread(NULL, 0, ClientThread, client, 0, NULL);
        if (!client->Thread) {
            LogMessage(ctx, "Failed to create client thread");
            EnterCriticalSection(&ctx->ClientLock);
            for (int i = 0; i < VUSB_US_MAX_CLIENTS; i++) {
                if (ctx->Clients[i] == client) {
                    ctx->Clients[i] = NULL;
                    ctx->ClientCount--;
                    break;
                }
            }
            LeaveCriticalSection(&ctx->ClientLock);
            closesocket(clientSocket);
            free(client);
        }
    }
    
    /* Cleanup */
    closesocket(listenSocket);
    ctx->ListenSocket = INVALID_SOCKET;
    
    /* Wait for client threads */
    EnterCriticalSection(&ctx->ClientLock);
    for (int i = 0; i < VUSB_US_MAX_CLIENTS; i++) {
        if (ctx->Clients[i]) {
            ctx->Clients[i]->Connected = FALSE;
            if (ctx->Clients[i]->Thread) {
                LeaveCriticalSection(&ctx->ClientLock);
                WaitForSingleObject(ctx->Clients[i]->Thread, 5000);
                EnterCriticalSection(&ctx->ClientLock);
            }
        }
    }
    LeaveCriticalSection(&ctx->ClientLock);
    
    return 0;
}

void VusbUsStop(PVUSB_US_CONTEXT ctx)
{
    if (!ctx) return;
    
    ctx->Running = FALSE;
    
    if (ctx->ShutdownEvent) {
        SetEvent(ctx->ShutdownEvent);
    }
    
    /* Close listen socket to unblock accept */
    if (ctx->ListenSocket != INVALID_SOCKET) {
        closesocket(ctx->ListenSocket);
        ctx->ListenSocket = INVALID_SOCKET;
    }
}
