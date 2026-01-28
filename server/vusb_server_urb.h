/**
 * Server URB Forwarder
 * 
 * Handles forwarding URBs from the driver to connected clients
 * and routing responses back.
 */

#ifndef VUSB_SERVER_URB_H
#define VUSB_SERVER_URB_H

#include <windows.h>
#include "../protocol/vusb_protocol.h"
#include "../protocol/vusb_ioctl.h"

/* Forward declarations */
struct _VUSB_SERVER_CONTEXT;
struct _VUSB_CLIENT_CONNECTION;

/* Pending URB tracking on server side */
typedef struct _SERVER_PENDING_URB {
    struct _SERVER_PENDING_URB* Next;
    uint32_t    UrbId;
    uint32_t    DeviceId;
    uint32_t    ClientDeviceId;     /* Client's device ID */
    struct _VUSB_CLIENT_CONNECTION* Client;
    LARGE_INTEGER SubmitTime;
    uint32_t    Timeout;
} SERVER_PENDING_URB, *PSERVER_PENDING_URB;

/* URB forwarder context */
typedef struct _SERVER_URB_CONTEXT {
    struct _VUSB_SERVER_CONTEXT* ServerContext;
    HANDLE      DriverHandle;
    HANDLE      ForwarderThread;
    volatile BOOL Running;
    
    /* Pending URB list */
    CRITICAL_SECTION PendingLock;
    PSERVER_PENDING_URB PendingList;
    uint32_t    PendingCount;
} SERVER_URB_CONTEXT, *PSERVER_URB_CONTEXT;

/* Initialize URB forwarder */
int ServerUrbInit(PSERVER_URB_CONTEXT ctx, struct _VUSB_SERVER_CONTEXT* serverCtx, 
                  HANDLE driverHandle);

/* Start URB forwarding thread */
int ServerUrbStart(PSERVER_URB_CONTEXT ctx);

/* Stop URB forwarding */
void ServerUrbStop(PSERVER_URB_CONTEXT ctx);

/* Forward a URB to the appropriate client */
int ServerUrbForward(PSERVER_URB_CONTEXT ctx, PVUSB_PENDING_URB pendingUrb);

/* Handle URB completion from client */
int ServerUrbComplete(PSERVER_URB_CONTEXT ctx, uint32_t urbId, uint32_t status,
                      uint32_t actualLength, uint8_t* data);

/* Find client for a device */
struct _VUSB_CLIENT_CONNECTION* ServerUrbFindClientForDevice(
    PSERVER_URB_CONTEXT ctx, uint32_t deviceId);

#endif /* VUSB_SERVER_URB_H */
