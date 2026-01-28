/**
 * Client URB Handler
 * 
 * Handles URB (USB Request Block) requests from the server
 * and forwards them to the actual USB device.
 */

#ifndef VUSB_CLIENT_URB_H
#define VUSB_CLIENT_URB_H

#include <stdint.h>
#include "../protocol/vusb_protocol.h"
#include "vusb_capture.h"

/* Pending URB tracking */
typedef struct _CLIENT_PENDING_URB {
    uint32_t    UrbId;
    uint32_t    DeviceId;
    uint32_t    LocalDeviceId;
    uint8_t     EndpointAddress;
    uint8_t     TransferType;
    uint8_t     Direction;
    uint32_t    TransferBufferLength;
    VUSB_SETUP_PACKET SetupPacket;
    
    /* Async support */
    USB_ASYNC_TRANSFER AsyncTransfer;
    void*       Context;
} CLIENT_PENDING_URB, *PCLIENT_PENDING_URB;

/* URB handler context */
typedef struct _CLIENT_URB_CONTEXT {
    PUSB_CAPTURE_CONTEXT    CaptureContext;
    void*                   ClientContext;
    
    /* Callback to send URB completion */
    int (*SendCompletion)(void* ctx, uint32_t deviceId, uint32_t urbId,
                          uint32_t status, uint32_t actualLength, uint8_t* data);
} CLIENT_URB_CONTEXT, *PCLIENT_URB_CONTEXT;

/* Initialize URB handler */
int ClientUrbInit(PCLIENT_URB_CONTEXT ctx, PUSB_CAPTURE_CONTEXT captureCtx);

/* Process incoming URB request from server */
int ClientUrbProcess(
    PCLIENT_URB_CONTEXT ctx,
    PVUSB_URB_SUBMIT urbSubmit,
    uint8_t* outData,
    uint32_t outDataLength);

/* Handle URB completion (for async) */
void ClientUrbComplete(PCLIENT_PENDING_URB urb, uint32_t status, uint32_t actualLength);

/* Cancel a pending URB */
int ClientUrbCancel(PCLIENT_URB_CONTEXT ctx, uint32_t deviceId, uint32_t urbId);

#endif /* VUSB_CLIENT_URB_H */
