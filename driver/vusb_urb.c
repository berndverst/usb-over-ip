/**
 * Virtual USB URB Handler
 * 
 * Handles URB (USB Request Block) processing and forwarding.
 * This module manages the complete URB lifecycle from Windows USB stack
 * through to user-mode and back.
 */

#include <ntddk.h>
#include <wdf.h>
#include <usb.h>
#include <usbdlib.h>

#include "vusb_driver.h"
#include "vusb_urb.h"

/* URB pool tag */
#define VUSB_URB_TAG 'bruV'

/**
 * VusbUrbCreate - Create a new URB entry
 */
NTSTATUS
VusbUrbCreate(
    _In_ PVUSB_DEVICE_CONTEXT DeviceContext,
    _In_ ULONG DeviceId,
    _In_ PURB Urb,
    _In_opt_ WDFREQUEST Request,
    _Out_ PVUSB_URB_ENTRY* UrbEntry
)
{
    PVUSB_URB_ENTRY entry;
    KIRQL oldIrql;

    *UrbEntry = NULL;

    /* Allocate URB entry */
    entry = (PVUSB_URB_ENTRY)ExAllocatePool2(
        POOL_FLAG_NON_PAGED,
        sizeof(VUSB_URB_ENTRY),
        VUSB_URB_TAG
    );

    if (!entry) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(entry, sizeof(VUSB_URB_ENTRY));

    /* Assign IDs */
    KeAcquireSpinLock(&DeviceContext->UrbQueueLock, &oldIrql);
    entry->UrbId = ++DeviceContext->NextUrbId;
    entry->SequenceNumber = ++DeviceContext->NextSequence;
    KeReleaseSpinLock(&DeviceContext->UrbQueueLock, oldIrql);

    entry->DeviceId = DeviceId;
    entry->Request = Request;
    KeQuerySystemTime(&entry->SubmitTime);
    entry->Timeout = 5000; /* 5 second default timeout */

    /* Parse URB */
    VusbUrbParse(entry, Urb);

    *UrbEntry = entry;
    return STATUS_SUCCESS;
}

/**
 * VusbUrbParse - Parse URB into internal format
 */
VOID
VusbUrbParse(
    _Inout_ PVUSB_URB_ENTRY Entry,
    _In_ PURB Urb
)
{
    switch (Urb->UrbHeader.Function) {
    case URB_FUNCTION_CONTROL_TRANSFER:
    case URB_FUNCTION_CONTROL_TRANSFER_EX:
        Entry->TransferType = VUSB_TRANSFER_CONTROL;
        Entry->EndpointAddress = 0;
        
        if (Urb->UrbHeader.Function == URB_FUNCTION_CONTROL_TRANSFER) {
            struct _URB_CONTROL_TRANSFER* ct = &Urb->UrbControlTransfer;
            Entry->TransferFlags = ct->TransferFlags;
            Entry->TransferBufferLength = ct->TransferBufferLength;
            Entry->TransferBuffer = ct->TransferBuffer;
            Entry->TransferBufferMdl = ct->TransferBufferMDL;
            
            /* Copy setup packet */
            RtlCopyMemory(&Entry->SetupPacket, ct->SetupPacket, 8);
            
            Entry->Direction = (ct->TransferFlags & USBD_TRANSFER_DIRECTION_IN) 
                               ? VUSB_DIR_IN : VUSB_DIR_OUT;
        } else {
            struct _URB_CONTROL_TRANSFER_EX* ctex = &Urb->UrbControlTransferEx;
            Entry->TransferFlags = ctex->TransferFlags;
            Entry->TransferBufferLength = ctex->TransferBufferLength;
            Entry->TransferBuffer = ctex->TransferBuffer;
            Entry->TransferBufferMdl = ctex->TransferBufferMDL;
            Entry->Timeout = ctex->Timeout;
            
            RtlCopyMemory(&Entry->SetupPacket, ctex->SetupPacket, 8);
            
            Entry->Direction = (ctex->TransferFlags & USBD_TRANSFER_DIRECTION_IN) 
                               ? VUSB_DIR_IN : VUSB_DIR_OUT;
        }
        break;

    case URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER:
        {
            struct _URB_BULK_OR_INTERRUPT_TRANSFER* bt = &Urb->UrbBulkOrInterruptTransfer;
            
            Entry->EndpointAddress = bt->PipeHandle ? 
                ((ULONG_PTR)bt->PipeHandle & 0xFF) : 0;
            Entry->TransferFlags = bt->TransferFlags;
            Entry->TransferBufferLength = bt->TransferBufferLength;
            Entry->TransferBuffer = bt->TransferBuffer;
            Entry->TransferBufferMdl = bt->TransferBufferMDL;
            
            /* Determine if bulk or interrupt based on endpoint type */
            /* For now, assume bulk - would need pipe info for accuracy */
            Entry->TransferType = VUSB_TRANSFER_BULK;
            Entry->Direction = (bt->TransferFlags & USBD_TRANSFER_DIRECTION_IN) 
                               ? VUSB_DIR_IN : VUSB_DIR_OUT;
        }
        break;

    case URB_FUNCTION_ISOCH_TRANSFER:
        {
            struct _URB_ISOCH_TRANSFER* it = &Urb->UrbIsochronousTransfer;
            
            Entry->TransferType = VUSB_TRANSFER_ISOCHRONOUS;
            Entry->EndpointAddress = it->PipeHandle ? 
                ((ULONG_PTR)it->PipeHandle & 0xFF) : 0;
            Entry->TransferFlags = it->TransferFlags;
            Entry->TransferBufferLength = it->TransferBufferLength;
            Entry->TransferBuffer = it->TransferBuffer;
            Entry->TransferBufferMdl = it->TransferBufferMDL;
            
            Entry->Direction = (it->TransferFlags & USBD_TRANSFER_DIRECTION_IN) 
                               ? VUSB_DIR_IN : VUSB_DIR_OUT;
        }
        break;

    case URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE:
    case URB_FUNCTION_GET_DESCRIPTOR_FROM_INTERFACE:
    case URB_FUNCTION_GET_DESCRIPTOR_FROM_ENDPOINT:
        {
            struct _URB_CONTROL_DESCRIPTOR_REQUEST* dr = &Urb->UrbControlDescriptorRequest;
            
            Entry->TransferType = VUSB_TRANSFER_CONTROL;
            Entry->Direction = VUSB_DIR_IN;
            Entry->TransferFlags = USBD_TRANSFER_DIRECTION_IN;
            Entry->TransferBufferLength = dr->TransferBufferLength;
            Entry->TransferBuffer = dr->TransferBuffer;
            Entry->TransferBufferMdl = dr->TransferBufferMDL;
            
            /* Build setup packet for GET_DESCRIPTOR */
            Entry->SetupPacket.bmRequestType = 0x80; /* Device-to-host, standard, device */
            Entry->SetupPacket.bRequest = 0x06; /* GET_DESCRIPTOR */
            Entry->SetupPacket.wValue = (dr->DescriptorType << 8) | dr->Index;
            Entry->SetupPacket.wIndex = dr->LanguageId;
            Entry->SetupPacket.wLength = (USHORT)dr->TransferBufferLength;
        }
        break;

    case URB_FUNCTION_SELECT_CONFIGURATION:
        {
            struct _URB_SELECT_CONFIGURATION* sc = &Urb->UrbSelectConfiguration;
            
            Entry->TransferType = VUSB_TRANSFER_CONTROL;
            Entry->Direction = VUSB_DIR_OUT;
            
            /* Build SET_CONFIGURATION setup packet */
            Entry->SetupPacket.bmRequestType = 0x00;
            Entry->SetupPacket.bRequest = 0x09; /* SET_CONFIGURATION */
            Entry->SetupPacket.wValue = sc->ConfigurationDescriptor ? 
                sc->ConfigurationDescriptor->bConfigurationValue : 0;
            Entry->SetupPacket.wIndex = 0;
            Entry->SetupPacket.wLength = 0;
        }
        break;

    case URB_FUNCTION_SELECT_INTERFACE:
        {
            struct _URB_SELECT_INTERFACE* si = &Urb->UrbSelectInterface;
            
            Entry->TransferType = VUSB_TRANSFER_CONTROL;
            Entry->Direction = VUSB_DIR_OUT;
            
            /* Build SET_INTERFACE setup packet */
            Entry->SetupPacket.bmRequestType = 0x01; /* Host-to-device, standard, interface */
            Entry->SetupPacket.bRequest = 0x0B; /* SET_INTERFACE */
            Entry->SetupPacket.wValue = si->Interface.AlternateSetting;
            Entry->SetupPacket.wIndex = si->Interface.InterfaceNumber;
            Entry->SetupPacket.wLength = 0;
        }
        break;

    case URB_FUNCTION_CLASS_DEVICE:
    case URB_FUNCTION_CLASS_INTERFACE:
    case URB_FUNCTION_CLASS_ENDPOINT:
    case URB_FUNCTION_CLASS_OTHER:
    case URB_FUNCTION_VENDOR_DEVICE:
    case URB_FUNCTION_VENDOR_INTERFACE:
    case URB_FUNCTION_VENDOR_ENDPOINT:
    case URB_FUNCTION_VENDOR_OTHER:
        {
            struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST* vcr = 
                &Urb->UrbControlVendorClassRequest;
            
            Entry->TransferType = VUSB_TRANSFER_CONTROL;
            Entry->TransferFlags = vcr->TransferFlags;
            Entry->TransferBufferLength = vcr->TransferBufferLength;
            Entry->TransferBuffer = vcr->TransferBuffer;
            Entry->TransferBufferMdl = vcr->TransferBufferMDL;
            
            Entry->Direction = (vcr->TransferFlags & USBD_TRANSFER_DIRECTION_IN) 
                               ? VUSB_DIR_IN : VUSB_DIR_OUT;
            
            /* Build setup packet */
            Entry->SetupPacket.bmRequestType = vcr->RequestTypeReservedBits;
            Entry->SetupPacket.bRequest = vcr->Request;
            Entry->SetupPacket.wValue = vcr->Value;
            Entry->SetupPacket.wIndex = vcr->Index;
            Entry->SetupPacket.wLength = (USHORT)vcr->TransferBufferLength;
        }
        break;

    case URB_FUNCTION_ABORT_PIPE:
    case URB_FUNCTION_RESET_PIPE:
    case URB_FUNCTION_SYNC_RESET_PIPE:
    case URB_FUNCTION_SYNC_CLEAR_STALL:
        /* These don't require network forwarding - handle locally */
        Entry->TransferType = VUSB_TRANSFER_CONTROL;
        Entry->TransferBufferLength = 0;
        break;

    default:
        KdPrint(("VirtualUSB: Unknown URB function 0x%04X\n", Urb->UrbHeader.Function));
        break;
    }
}

/**
 * VusbUrbGetBuffer - Get transfer buffer pointer
 */
PVOID
VusbUrbGetBuffer(
    _In_ PVUSB_URB_ENTRY Entry,
    _Out_opt_ PULONG Length
)
{
    PVOID buffer = NULL;

    if (Entry->TransferBuffer) {
        buffer = Entry->TransferBuffer;
    } else if (Entry->TransferBufferMdl) {
        buffer = MmGetSystemAddressForMdlSafe(Entry->TransferBufferMdl, NormalPagePriority);
    }

    if (Length) {
        *Length = Entry->TransferBufferLength;
    }

    return buffer;
}

/**
 * VusbUrbCopyData - Copy data to/from URB buffer
 */
NTSTATUS
VusbUrbCopyData(
    _In_ PVUSB_URB_ENTRY Entry,
    _In_ PVOID Data,
    _In_ ULONG DataLength,
    _In_ BOOLEAN ToUrb
)
{
    PVOID buffer;
    ULONG copyLength;

    buffer = VusbUrbGetBuffer(Entry, NULL);
    if (!buffer) {
        return STATUS_INVALID_PARAMETER;
    }

    copyLength = min(DataLength, Entry->TransferBufferLength);

    if (ToUrb) {
        RtlCopyMemory(buffer, Data, copyLength);
    } else {
        RtlCopyMemory(Data, buffer, copyLength);
    }

    return STATUS_SUCCESS;
}

/**
 * VusbUrbBuildPendingResponse - Build response for user-mode
 */
NTSTATUS
VusbUrbBuildPendingResponse(
    _In_ PVUSB_URB_ENTRY Entry,
    _Out_writes_bytes_(BufferSize) PVUSB_PENDING_URB Response,
    _In_ ULONG BufferSize,
    _Out_ PULONG ActualSize
)
{
    ULONG requiredSize;
    PVOID transferData;
    ULONG dataLength = 0;

    /* Calculate required size */
    requiredSize = sizeof(VUSB_PENDING_URB);
    if (Entry->Direction == VUSB_DIR_OUT && Entry->TransferBufferLength > 0) {
        requiredSize += Entry->TransferBufferLength;
    }

    if (BufferSize < requiredSize) {
        *ActualSize = requiredSize;
        return STATUS_BUFFER_TOO_SMALL;
    }

    /* Fill header */
    Response->DeviceId = Entry->DeviceId;
    Response->UrbId = Entry->UrbId;
    Response->SequenceNumber = Entry->SequenceNumber;
    Response->EndpointAddress = Entry->EndpointAddress;
    Response->TransferType = Entry->TransferType;
    Response->Direction = Entry->Direction;
    Response->Reserved = 0;
    Response->TransferFlags = Entry->TransferFlags;
    Response->TransferBufferLength = Entry->TransferBufferLength;
    Response->Interval = 0;
    RtlCopyMemory(&Response->SetupPacket, &Entry->SetupPacket, sizeof(VUSB_SETUP_PACKET));

    /* Copy OUT data */
    if (Entry->Direction == VUSB_DIR_OUT && Entry->TransferBufferLength > 0) {
        transferData = VusbUrbGetBuffer(Entry, &dataLength);
        if (transferData && dataLength > 0) {
            RtlCopyMemory(Response + 1, transferData, dataLength);
        }
    }

    *ActualSize = requiredSize;
    return STATUS_SUCCESS;
}

/**
 * VusbUrbComplete - Complete URB with data
 */
NTSTATUS
VusbUrbComplete(
    _In_ PVUSB_DEVICE_CONTEXT DeviceContext,
    _In_ PVUSB_URB_ENTRY Entry,
    _In_ NTSTATUS Status,
    _In_ ULONG ActualLength,
    _In_reads_bytes_opt_(ActualLength) PVOID Data
)
{
    UNREFERENCED_PARAMETER(DeviceContext);

    /* Copy IN data to URB buffer */
    if (NT_SUCCESS(Status) && Entry->Direction == VUSB_DIR_IN && Data && ActualLength > 0) {
        PVOID buffer = VusbUrbGetBuffer(Entry, NULL);
        if (buffer) {
            RtlCopyMemory(buffer, Data, min(ActualLength, Entry->TransferBufferLength));
        }
    }

    /* Complete the request if present */
    if (Entry->Request) {
        WdfRequestCompleteWithInformation(Entry->Request, Status, ActualLength);
    }

    /* Free entry */
    ExFreePoolWithTag(Entry, VUSB_URB_TAG);

    return STATUS_SUCCESS;
}

/**
 * VusbUrbFree - Free a URB entry
 */
VOID
VusbUrbFree(
    _In_ PVUSB_URB_ENTRY Entry
)
{
    if (Entry) {
        ExFreePoolWithTag(Entry, VUSB_URB_TAG);
    }
}

/**
 * VusbUrbGetFunction - Get URB function code for response
 */
USHORT
VusbUrbGetFunction(
    _In_ PVUSB_URB_ENTRY Entry
)
{
    switch (Entry->TransferType) {
    case VUSB_TRANSFER_CONTROL:
        return URB_FUNCTION_CONTROL_TRANSFER;
    case VUSB_TRANSFER_BULK:
    case VUSB_TRANSFER_INTERRUPT:
        return URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER;
    case VUSB_TRANSFER_ISOCHRONOUS:
        return URB_FUNCTION_ISOCH_TRANSFER;
    default:
        return 0;
    }
}

/**
 * VusbProcessStandardRequest - Handle standard USB requests locally if possible
 */
NTSTATUS
VusbProcessStandardRequest(
    _In_ PVUSB_DEVICE_CONTEXT DeviceContext,
    _In_ PVUSB_URB_ENTRY Entry,
    _Out_ PBOOLEAN Handled
)
{
    PVUSB_VIRTUAL_DEVICE device;
    PUCHAR descriptorData;
    ULONG descriptorLength;

    *Handled = FALSE;

    /* Only handle control transfers */
    if (Entry->TransferType != VUSB_TRANSFER_CONTROL) {
        return STATUS_SUCCESS;
    }

    device = VusbFindDevice(DeviceContext, Entry->DeviceId);
    if (!device) {
        return STATUS_DEVICE_NOT_CONNECTED;
    }

    /* Check for GET_DESCRIPTOR requests we can handle locally */
    if (Entry->SetupPacket.bmRequestType == 0x80 &&
        Entry->SetupPacket.bRequest == 0x06) {
        
        UCHAR descriptorType = (Entry->SetupPacket.wValue >> 8) & 0xFF;
        UCHAR descriptorIndex = Entry->SetupPacket.wValue & 0xFF;
        
        /* Check if we have cached descriptors */
        if (device->Descriptors && device->DescriptorLength > 0) {
            if (descriptorType == 0x01 && descriptorIndex == 0) {
                /* Device descriptor */
                descriptorData = device->Descriptors;
                descriptorLength = min(18, device->DescriptorLength);
                
                if (Entry->TransferBufferLength >= descriptorLength) {
                    PVOID buffer = VusbUrbGetBuffer(Entry, NULL);
                    if (buffer) {
                        RtlCopyMemory(buffer, descriptorData, descriptorLength);
                        *Handled = TRUE;
                        return STATUS_SUCCESS;
                    }
                }
            }
            /* Could add more descriptor caching here */
        }
    }

    return STATUS_SUCCESS;
}
