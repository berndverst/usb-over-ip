/**
 * Client URB Handler Implementation
 * 
 * Processes URB requests from server and forwards to real USB devices.
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vusb_client_urb.h"
#include "vusb_capture.h"
#include "../protocol/vusb_protocol.h"

/**
 * ClientUrbInit - Initialize URB handler
 */
int ClientUrbInit(PCLIENT_URB_CONTEXT ctx, PUSB_CAPTURE_CONTEXT captureCtx)
{
    if (!ctx || !captureCtx) return -1;
    
    memset(ctx, 0, sizeof(CLIENT_URB_CONTEXT));
    ctx->CaptureContext = captureCtx;
    
    return 0;
}

/**
 * ClientUrbProcess - Process an incoming URB request
 */
int ClientUrbProcess(
    PCLIENT_URB_CONTEXT ctx,
    PVUSB_URB_SUBMIT urbSubmit,
    uint8_t* outData,
    uint32_t outDataLength)
{
    PUSB_CAPTURED_DEVICE device;
    uint32_t actualLength = 0;
    int result = 0;
    uint8_t* responseData = NULL;
    uint32_t responseDataLength = 0;
    
    if (!ctx || !urbSubmit) return -1;
    
    printf("[URB] Processing URB %u for device %u, EP=0x%02X, Type=%d, Dir=%d, Len=%u\n",
           urbSubmit->UrbId, urbSubmit->DeviceId, urbSubmit->EndpointAddress,
           urbSubmit->TransferType, urbSubmit->Direction, urbSubmit->TransferBufferLength);
    
    /* Find the device */
    device = UsbCaptureFindDevice(ctx->CaptureContext, urbSubmit->DeviceId);
    if (!device) {
        printf("[URB] Device %u not found\n", urbSubmit->DeviceId);
        
        /* Send error completion */
        if (ctx->SendCompletion) {
            ctx->SendCompletion(ctx->ClientContext, urbSubmit->DeviceId, 
                               urbSubmit->UrbId, VUSB_STATUS_NO_DEVICE, 0, NULL);
        }
        return -1;
    }
    
    /* Make sure device is open */
    if (!device->Opened) {
        if (UsbCaptureOpenDevice(device) != 0) {
            printf("[URB] Failed to open device\n");
            if (ctx->SendCompletion) {
                ctx->SendCompletion(ctx->ClientContext, urbSubmit->DeviceId,
                                   urbSubmit->UrbId, VUSB_STATUS_ERROR, 0, NULL);
            }
            return -1;
        }
    }
    
    /* Allocate response buffer for IN transfers */
    if (urbSubmit->Direction == VUSB_DIR_IN && urbSubmit->TransferBufferLength > 0) {
        responseData = (uint8_t*)malloc(urbSubmit->TransferBufferLength);
        if (!responseData) {
            if (ctx->SendCompletion) {
                ctx->SendCompletion(ctx->ClientContext, urbSubmit->DeviceId,
                                   urbSubmit->UrbId, VUSB_STATUS_NO_MEMORY, 0, NULL);
            }
            return -1;
        }
    }
    
    /* Process based on transfer type */
    switch (urbSubmit->TransferType) {
    case VUSB_TRANSFER_CONTROL:
        {
            VUSB_SETUP_PACKET setup;
            memcpy(&setup, &urbSubmit->SetupPacket, sizeof(setup));
            
            printf("[URB] Control: bmReq=0x%02X bReq=0x%02X wVal=0x%04X wIdx=0x%04X wLen=%u\n",
                   setup.bmRequestType, setup.bRequest, setup.wValue, setup.wIndex, setup.wLength);
            
            uint8_t* transferData = NULL;
            uint32_t transferLength = 0;
            
            if (urbSubmit->Direction == VUSB_DIR_IN) {
                transferData = responseData;
                transferLength = urbSubmit->TransferBufferLength;
            } else if (outData && outDataLength > 0) {
                transferData = outData;
                transferLength = outDataLength;
            }
            
            result = UsbCaptureControlTransfer(device, &setup, transferData, 
                                                transferLength, &actualLength, 5000);
            
            if (result == 0 && urbSubmit->Direction == VUSB_DIR_IN) {
                responseDataLength = actualLength;
            }
        }
        break;
        
    case VUSB_TRANSFER_BULK:
        if (urbSubmit->Direction == VUSB_DIR_IN) {
            result = UsbCaptureBulkTransfer(device, urbSubmit->EndpointAddress,
                                            responseData, urbSubmit->TransferBufferLength,
                                            &actualLength, 5000);
            responseDataLength = actualLength;
        } else {
            result = UsbCaptureBulkTransfer(device, urbSubmit->EndpointAddress,
                                            outData, outDataLength,
                                            &actualLength, 5000);
        }
        break;
        
    case VUSB_TRANSFER_INTERRUPT:
        if (urbSubmit->Direction == VUSB_DIR_IN) {
            result = UsbCaptureInterruptTransfer(device, urbSubmit->EndpointAddress,
                                                  responseData, urbSubmit->TransferBufferLength,
                                                  &actualLength, 5000);
            responseDataLength = actualLength;
        } else {
            result = UsbCaptureInterruptTransfer(device, urbSubmit->EndpointAddress,
                                                  outData, outDataLength,
                                                  &actualLength, 5000);
        }
        break;
        
    case VUSB_TRANSFER_ISOCHRONOUS:
        /* Isochronous requires special handling - not fully implemented */
        printf("[URB] Isochronous transfers not fully supported\n");
        result = -1;
        break;
        
    default:
        printf("[URB] Unknown transfer type: %d\n", urbSubmit->TransferType);
        result = -1;
        break;
    }
    
    /* Send completion */
    uint32_t status = (result == 0) ? VUSB_STATUS_SUCCESS : VUSB_STATUS_ERROR;
    
    printf("[URB] Complete: status=%u, actualLength=%u\n", status, actualLength);
    
    if (ctx->SendCompletion) {
        ctx->SendCompletion(ctx->ClientContext, urbSubmit->DeviceId, urbSubmit->UrbId,
                           status, responseDataLength, responseData);
    }
    
    if (responseData) {
        free(responseData);
    }
    
    return result;
}

/**
 * ClientUrbCancel - Cancel a pending URB
 */
int ClientUrbCancel(PCLIENT_URB_CONTEXT ctx, uint32_t deviceId, uint32_t urbId)
{
    /* For now, cancellation is not fully implemented for sync transfers */
    printf("[URB] Cancel request for URB %u on device %u\n", urbId, deviceId);
    return 0;
}
