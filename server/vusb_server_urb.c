/**
 * Server URB Forwarder Implementation
 * 
 * Polls driver for pending URBs and forwards them to connected clients.
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#include "vusb_server.h"
#include "vusb_server_urb.h"
#include "../protocol/vusb_protocol.h"
#include "../protocol/vusb_ioctl.h"

/* Forward declarations */
static DWORD WINAPI UrbForwarderThread(LPVOID param);
static PSERVER_PENDING_URB AllocPendingUrb(void);
static void FreePendingUrb(PSERVER_PENDING_URB urb);

/**
 * ServerUrbInit - Initialize URB forwarder
 */
int ServerUrbInit(PSERVER_URB_CONTEXT ctx, PVUSB_SERVER_CONTEXT serverCtx,
                  HANDLE driverHandle)
{
    if (!ctx || !serverCtx) return -1;

    memset(ctx, 0, sizeof(SERVER_URB_CONTEXT));
    ctx->ServerContext = serverCtx;
    ctx->DriverHandle = driverHandle;
    ctx->Running = FALSE;
    
    InitializeCriticalSection(&ctx->PendingLock);
    
    return 0;
}

/**
 * ServerUrbStart - Start URB forwarding thread
 */
int ServerUrbStart(PSERVER_URB_CONTEXT ctx)
{
    if (!ctx) return -1;
    
    if (ctx->DriverHandle == INVALID_HANDLE_VALUE) {
        printf("[URB Forwarder] No driver handle - running in simulation mode\n");
        return 0;
    }
    
    ctx->Running = TRUE;
    ctx->ForwarderThread = CreateThread(NULL, 0, UrbForwarderThread, ctx, 0, NULL);
    
    if (!ctx->ForwarderThread) {
        ctx->Running = FALSE;
        return -1;
    }
    
    printf("[URB Forwarder] Started\n");
    return 0;
}

/**
 * ServerUrbStop - Stop URB forwarding
 */
void ServerUrbStop(PSERVER_URB_CONTEXT ctx)
{
    if (!ctx) return;
    
    ctx->Running = FALSE;
    
    if (ctx->ForwarderThread) {
        WaitForSingleObject(ctx->ForwarderThread, 5000);
        CloseHandle(ctx->ForwarderThread);
        ctx->ForwarderThread = NULL;
    }
    
    /* Free pending URBs */
    EnterCriticalSection(&ctx->PendingLock);
    while (ctx->PendingList) {
        PSERVER_PENDING_URB next = ctx->PendingList->Next;
        FreePendingUrb(ctx->PendingList);
        ctx->PendingList = next;
    }
    LeaveCriticalSection(&ctx->PendingLock);
    
    DeleteCriticalSection(&ctx->PendingLock);
    
    printf("[URB Forwarder] Stopped\n");
}

/**
 * UrbForwarderThread - Main URB forwarding loop
 */
static DWORD WINAPI UrbForwarderThread(LPVOID param)
{
    PSERVER_URB_CONTEXT ctx = (PSERVER_URB_CONTEXT)param;
    uint8_t* buffer;
    DWORD bytesReturned;
    OVERLAPPED overlapped = {0};
    
    printf("[URB Forwarder] Thread started\n");
    
    buffer = (uint8_t*)malloc(VUSB_MAX_PACKET_SIZE);
    if (!buffer) {
        return 1;
    }
    
    overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    
    while (ctx->Running) {
        /* Request pending URB from driver */
        BOOL result = DeviceIoControl(
            ctx->DriverHandle,
            IOCTL_VUSB_GET_PENDING_URB,
            NULL, 0,
            buffer, VUSB_MAX_PACKET_SIZE,
            &bytesReturned,
            &overlapped
        );
        
        if (!result) {
            DWORD error = GetLastError();
            if (error == ERROR_IO_PENDING) {
                /* Wait for URB with timeout */
                DWORD waitResult = WaitForSingleObject(overlapped.hEvent, 100);
                
                if (waitResult == WAIT_OBJECT_0) {
                    if (!GetOverlappedResult(ctx->DriverHandle, &overlapped, 
                                             &bytesReturned, FALSE)) {
                        ResetEvent(overlapped.hEvent);
                        continue;
                    }
                } else {
                    /* Timeout - cancel and retry */
                    CancelIoEx(ctx->DriverHandle, &overlapped);
                    ResetEvent(overlapped.hEvent);
                    continue;
                }
            } else {
                /* Error */
                Sleep(100);
                continue;
            }
        }
        
        ResetEvent(overlapped.hEvent);
        
        if (bytesReturned >= sizeof(VUSB_PENDING_URB)) {
            PVUSB_PENDING_URB pendingUrb = (PVUSB_PENDING_URB)buffer;
            ServerUrbForward(ctx, pendingUrb);
        }
    }
    
    CloseHandle(overlapped.hEvent);
    free(buffer);
    
    printf("[URB Forwarder] Thread ended\n");
    return 0;
}

/**
 * ServerUrbForward - Forward URB to appropriate client
 */
int ServerUrbForward(PSERVER_URB_CONTEXT ctx, PVUSB_PENDING_URB pendingUrb)
{
    PVUSB_SERVER_CONTEXT serverCtx = ctx->ServerContext;
    PVUSB_CLIENT_CONNECTION client;
    uint8_t* sendBuffer;
    size_t sendSize;
    VUSB_URB_SUBMIT* submit;
    int result;
    
    printf("[URB Forward] URB %u for device %u, EP=0x%02X, Type=%d, Len=%u\n",
           pendingUrb->UrbId, pendingUrb->DeviceId, pendingUrb->EndpointAddress,
           pendingUrb->TransferType, pendingUrb->TransferBufferLength);
    
    /* Find client that owns this device */
    client = ServerUrbFindClientForDevice(ctx, pendingUrb->DeviceId);
    if (!client) {
        printf("[URB Forward] No client for device %u\n", pendingUrb->DeviceId);
        
        /* Complete URB with error */
        VUSB_URB_COMPLETION completion = {0};
        completion.DeviceId = pendingUrb->DeviceId;
        completion.UrbId = pendingUrb->UrbId;
        completion.SequenceNumber = pendingUrb->SequenceNumber;
        completion.Status = VUSB_STATUS_NO_DEVICE;
        completion.ActualLength = 0;
        
        DWORD bytesReturned;
        DeviceIoControl(ctx->DriverHandle, IOCTL_VUSB_COMPLETE_URB,
                       &completion, sizeof(completion), NULL, 0, &bytesReturned, NULL);
        return -1;
    }
    
    /* Build URB submit message */
    sendSize = sizeof(VUSB_URB_SUBMIT);
    if (pendingUrb->Direction == VUSB_DIR_OUT && pendingUrb->TransferBufferLength > 0) {
        sendSize += pendingUrb->TransferBufferLength;
    }
    
    sendBuffer = (uint8_t*)malloc(sendSize);
    if (!sendBuffer) return -1;
    
    submit = (VUSB_URB_SUBMIT*)sendBuffer;
    VusbInitHeader(&submit->Header, VUSB_CMD_SUBMIT_URB, 
                   (uint32_t)(sendSize - sizeof(VUSB_HEADER)), pendingUrb->SequenceNumber);
    submit->DeviceId = pendingUrb->DeviceId;
    submit->UrbId = pendingUrb->UrbId;
    submit->EndpointAddress = pendingUrb->EndpointAddress;
    submit->TransferType = pendingUrb->TransferType;
    submit->Direction = pendingUrb->Direction;
    submit->Reserved = 0;
    submit->TransferFlags = pendingUrb->TransferFlags;
    submit->TransferBufferLength = pendingUrb->TransferBufferLength;
    submit->Interval = pendingUrb->Interval;
    memcpy(&submit->SetupPacket, &pendingUrb->SetupPacket, sizeof(VUSB_SETUP_PACKET));
    
    /* Copy OUT data if present */
    if (pendingUrb->Direction == VUSB_DIR_OUT && pendingUrb->TransferBufferLength > 0) {
        memcpy(sendBuffer + sizeof(VUSB_URB_SUBMIT), 
               (uint8_t*)(pendingUrb + 1), pendingUrb->TransferBufferLength);
    }
    
    /* Track pending URB */
    PSERVER_PENDING_URB tracking = AllocPendingUrb();
    if (tracking) {
        tracking->UrbId = pendingUrb->UrbId;
        tracking->DeviceId = pendingUrb->DeviceId;
        tracking->Client = client;
        QueryPerformanceCounter(&tracking->SubmitTime);
        
        EnterCriticalSection(&ctx->PendingLock);
        tracking->Next = ctx->PendingList;
        ctx->PendingList = tracking;
        ctx->PendingCount++;
        LeaveCriticalSection(&ctx->PendingLock);
    }
    
    /* Send to client */
    result = send(client->Socket, (char*)sendBuffer, (int)sendSize, 0);
    free(sendBuffer);
    
    return (result == (int)sendSize) ? 0 : -1;
}

/**
 * ServerUrbComplete - Handle URB completion from client
 */
int ServerUrbComplete(PSERVER_URB_CONTEXT ctx, uint32_t urbId, uint32_t status,
                      uint32_t actualLength, uint8_t* data)
{
    PSERVER_PENDING_URB prev = NULL;
    PSERVER_PENDING_URB curr;
    DWORD bytesReturned;
    
    /* Find and remove from pending list */
    EnterCriticalSection(&ctx->PendingLock);
    
    curr = ctx->PendingList;
    while (curr) {
        if (curr->UrbId == urbId) {
            if (prev) {
                prev->Next = curr->Next;
            } else {
                ctx->PendingList = curr->Next;
            }
            ctx->PendingCount--;
            break;
        }
        prev = curr;
        curr = curr->Next;
    }
    
    LeaveCriticalSection(&ctx->PendingLock);
    
    if (!curr) {
        printf("[URB Complete] URB %u not found in pending list\n", urbId);
        return -1;
    }
    
    printf("[URB Complete] URB %u, status=%u, length=%u\n", urbId, status, actualLength);
    
    /* Send completion to driver */
    if (ctx->DriverHandle != INVALID_HANDLE_VALUE) {
        size_t completionSize = sizeof(VUSB_URB_COMPLETION) + actualLength;
        uint8_t* completionBuffer = (uint8_t*)malloc(completionSize);
        
        if (completionBuffer) {
            PVUSB_URB_COMPLETION completion = (PVUSB_URB_COMPLETION)completionBuffer;
            completion->DeviceId = curr->DeviceId;
            completion->UrbId = urbId;
            completion->SequenceNumber = 0;
            completion->Status = status;
            completion->ActualLength = actualLength;
            
            if (data && actualLength > 0) {
                memcpy(completionBuffer + sizeof(VUSB_URB_COMPLETION), data, actualLength);
            }
            
            DeviceIoControl(ctx->DriverHandle, IOCTL_VUSB_COMPLETE_URB,
                           completionBuffer, (DWORD)completionSize, 
                           NULL, 0, &bytesReturned, NULL);
            
            free(completionBuffer);
        }
    }
    
    FreePendingUrb(curr);
    return 0;
}

/**
 * ServerUrbFindClientForDevice - Find client that owns a device
 */
PVUSB_CLIENT_CONNECTION ServerUrbFindClientForDevice(PSERVER_URB_CONTEXT ctx, 
                                                      uint32_t deviceId)
{
    PVUSB_SERVER_CONTEXT serverCtx = ctx->ServerContext;
    
    EnterCriticalSection(&serverCtx->ClientLock);
    
    for (int i = 0; i < serverCtx->Config.MaxClients; i++) {
        PVUSB_CLIENT_CONNECTION client = serverCtx->Clients[i];
        if (client && client->Connected) {
            for (int j = 0; j < VUSB_MAX_DEVICES; j++) {
                if (client->Devices[j].Active && client->Devices[j].DeviceId == deviceId) {
                    LeaveCriticalSection(&serverCtx->ClientLock);
                    return client;
                }
            }
        }
    }
    
    LeaveCriticalSection(&serverCtx->ClientLock);
    return NULL;
}

/* Helper functions */
static PSERVER_PENDING_URB AllocPendingUrb(void)
{
    return (PSERVER_PENDING_URB)calloc(1, sizeof(SERVER_PENDING_URB));
}

static void FreePendingUrb(PSERVER_PENDING_URB urb)
{
    if (urb) free(urb);
}
