/**
 * Virtual USB IOCTL Handlers
 * 
 * Implementation of IOCTL request handlers for the virtual USB driver.
 */

#include <ntddk.h>
#include <wdf.h>

#include "vusb_driver.h"
#include "../protocol/vusb_ioctl.h"

/**
 * VusbHandleGetVersion - Return driver version information
 */
NTSTATUS
VusbHandleGetVersion(
    _In_ PVUSB_DEVICE_CONTEXT DeviceContext,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _Out_ size_t* BytesReturned
)
{
    NTSTATUS status;
    PVUSB_VERSION_INFO versionInfo;

    UNREFERENCED_PARAMETER(DeviceContext);

    *BytesReturned = 0;

    if (OutputBufferLength < sizeof(VUSB_VERSION_INFO)) {
        return STATUS_BUFFER_TOO_SMALL;
    }

    status = WdfRequestRetrieveOutputBuffer(Request, sizeof(VUSB_VERSION_INFO),
                                            (PVOID*)&versionInfo, NULL);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    versionInfo->DriverVersion = 0x00010000;  /* Version 1.0 */
    versionInfo->ProtocolVersion = VUSB_PROTOCOL_VERSION;
    versionInfo->MaxDevices = VUSB_MAX_DEVICES;
    versionInfo->Capabilities = 0;

    *BytesReturned = sizeof(VUSB_VERSION_INFO);
    return STATUS_SUCCESS;
}

/**
 * VusbHandlePluginDevice - Create a new virtual USB device
 */
NTSTATUS
VusbHandlePluginDevice(
    _In_ PVUSB_DEVICE_CONTEXT DeviceContext,
    _In_ WDFREQUEST Request,
    _In_ size_t InputBufferLength,
    _In_ size_t OutputBufferLength,
    _Out_ size_t* BytesReturned
)
{
    NTSTATUS status;
    PVUSB_PLUGIN_REQUEST pluginRequest;
    PVUSB_PLUGIN_RESPONSE pluginResponse;
    PUCHAR descriptors;
    ULONG deviceId;
    size_t inputSize;

    *BytesReturned = 0;

    /* Validate buffer sizes */
    if (InputBufferLength < sizeof(VUSB_PLUGIN_REQUEST)) {
        return STATUS_BUFFER_TOO_SMALL;
    }

    if (OutputBufferLength < sizeof(VUSB_PLUGIN_RESPONSE)) {
        return STATUS_BUFFER_TOO_SMALL;
    }

    /* Get input buffer */
    status = WdfRequestRetrieveInputBuffer(Request, sizeof(VUSB_PLUGIN_REQUEST),
                                           (PVOID*)&pluginRequest, &inputSize);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    /* Validate descriptor length */
    if (inputSize < sizeof(VUSB_PLUGIN_REQUEST) + pluginRequest->DescriptorLength) {
        return STATUS_BUFFER_TOO_SMALL;
    }

    /* Get descriptor data (follows the request structure) */
    descriptors = (PUCHAR)(pluginRequest + 1);

    /* Create the virtual device */
    status = VusbCreateVirtualDevice(
        DeviceContext,
        &pluginRequest->DeviceInfo,
        descriptors,
        pluginRequest->DescriptorLength,
        &deviceId
    );

    /* Get output buffer */
    status = WdfRequestRetrieveOutputBuffer(Request, sizeof(VUSB_PLUGIN_RESPONSE),
                                            (PVOID*)&pluginResponse, NULL);
    if (!NT_SUCCESS(status)) {
        /* Cleanup on failure */
        if (deviceId != 0) {
            VusbDestroyVirtualDevice(DeviceContext, deviceId);
        }
        return status;
    }

    /* Fill response */
    pluginResponse->Status = NT_SUCCESS(status) ? VUSB_STATUS_SUCCESS : VUSB_STATUS_ERROR;
    pluginResponse->DeviceId = deviceId;
    pluginResponse->PortNumber = deviceId; /* Port = Device ID for simplicity */

    *BytesReturned = sizeof(VUSB_PLUGIN_RESPONSE);

    KdPrint(("VirtualUSB: Plugin device - ID %lu, Status 0x%x\n", deviceId, status));
    return status;
}

/**
 * VusbHandleUnplugDevice - Remove a virtual USB device
 */
NTSTATUS
VusbHandleUnplugDevice(
    _In_ PVUSB_DEVICE_CONTEXT DeviceContext,
    _In_ WDFREQUEST Request,
    _In_ size_t InputBufferLength
)
{
    NTSTATUS status;
    PVUSB_UNPLUG_REQUEST unplugRequest;

    if (InputBufferLength < sizeof(VUSB_UNPLUG_REQUEST)) {
        return STATUS_BUFFER_TOO_SMALL;
    }

    status = WdfRequestRetrieveInputBuffer(Request, sizeof(VUSB_UNPLUG_REQUEST),
                                           (PVOID*)&unplugRequest, NULL);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    status = VusbDestroyVirtualDevice(DeviceContext, unplugRequest->DeviceId);

    KdPrint(("VirtualUSB: Unplug device - ID %lu, Status 0x%x\n", 
             unplugRequest->DeviceId, status));
    return status;
}

/**
 * VusbHandleGetDeviceList - Get list of all virtual devices
 */
NTSTATUS
VusbHandleGetDeviceList(
    _In_ PVUSB_DEVICE_CONTEXT DeviceContext,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _Out_ size_t* BytesReturned
)
{
    NTSTATUS status;
    PVUSB_DEVICE_LIST deviceList;
    KIRQL oldIrql;
    ULONG i, count;

    *BytesReturned = 0;

    if (OutputBufferLength < sizeof(VUSB_DEVICE_LIST)) {
        return STATUS_BUFFER_TOO_SMALL;
    }

    status = WdfRequestRetrieveOutputBuffer(Request, sizeof(VUSB_DEVICE_LIST),
                                            (PVOID*)&deviceList, NULL);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    RtlZeroMemory(deviceList, sizeof(VUSB_DEVICE_LIST));

    /* Lock and enumerate devices */
    KeAcquireSpinLock(&DeviceContext->DeviceListLock, &oldIrql);

    count = 0;
    for (i = 0; i < VUSB_MAX_DEVICES && count < VUSB_MAX_DEVICES; i++) {
        PVUSB_VIRTUAL_DEVICE vdev = DeviceContext->Devices[i];
        if (vdev != NULL) {
            deviceList->Devices[count].DeviceId = vdev->DeviceId;
            deviceList->Devices[count].PortNumber = vdev->PortNumber;
            deviceList->Devices[count].State = vdev->State;
            RtlCopyMemory(&deviceList->Devices[count].DeviceInfo, 
                         &vdev->DeviceInfo, sizeof(VUSB_DEVICE_INFO));
            count++;
        }
    }

    KeReleaseSpinLock(&DeviceContext->DeviceListLock, oldIrql);

    deviceList->DeviceCount = count;
    *BytesReturned = sizeof(VUSB_DEVICE_LIST);

    return STATUS_SUCCESS;
}

/**
 * VusbHandleGetPendingUrb - Get next pending URB for processing
 */
NTSTATUS
VusbHandleGetPendingUrb(
    _In_ PVUSB_DEVICE_CONTEXT DeviceContext,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _Out_ size_t* BytesReturned
)
{
    NTSTATUS status;
    PVUSB_PENDING_URB pendingUrb;
    PVUSB_URB_ENTRY urbEntry;
    PMDL outputMdl;
    PVOID outputBuffer;
    size_t requiredSize;

    *BytesReturned = 0;

    /* Check if there's a pending URB */
    urbEntry = VusbDequeueUrb(DeviceContext);

    if (urbEntry == NULL) {
        /* No pending URB - queue the request to wait */
        status = WdfRequestForwardToIoQueue(Request, DeviceContext->PendingUrbQueue);
        if (NT_SUCCESS(status)) {
            return STATUS_PENDING;
        }
        return status;
    }

    /* Calculate required buffer size */
    requiredSize = sizeof(VUSB_PENDING_URB);
    if (urbEntry->Direction == VUSB_DIR_OUT) {
        requiredSize += urbEntry->TransferBufferLength;
    }

    if (OutputBufferLength < requiredSize) {
        /* Re-queue the URB entry */
        VusbQueueUrb(DeviceContext, urbEntry);
        return STATUS_BUFFER_TOO_SMALL;
    }

    /* Get output buffer using MDL for direct I/O */
    status = WdfRequestRetrieveOutputWdmMdl(Request, &outputMdl);
    if (NT_SUCCESS(status) && outputMdl != NULL) {
        outputBuffer = MmGetSystemAddressForMdlSafe(outputMdl, NormalPagePriority);
    } else {
        /* Fallback to buffered I/O */
        status = WdfRequestRetrieveOutputBuffer(Request, sizeof(VUSB_PENDING_URB),
                                                &outputBuffer, NULL);
        if (!NT_SUCCESS(status)) {
            VusbQueueUrb(DeviceContext, urbEntry);
            return status;
        }
    }

    pendingUrb = (PVUSB_PENDING_URB)outputBuffer;

    /* Fill pending URB structure */
    pendingUrb->DeviceId = urbEntry->DeviceId;
    pendingUrb->UrbId = urbEntry->UrbId;
    pendingUrb->SequenceNumber = urbEntry->SequenceNumber;
    pendingUrb->EndpointAddress = urbEntry->EndpointAddress;
    pendingUrb->TransferType = urbEntry->TransferType;
    pendingUrb->Direction = urbEntry->Direction;
    pendingUrb->Reserved = 0;
    pendingUrb->TransferFlags = urbEntry->TransferFlags;
    pendingUrb->TransferBufferLength = urbEntry->TransferBufferLength;
    pendingUrb->Interval = 0;
    RtlCopyMemory(&pendingUrb->SetupPacket, &urbEntry->SetupPacket, sizeof(VUSB_SETUP_PACKET));

    /* Copy OUT data if present */
    if (urbEntry->Direction == VUSB_DIR_OUT && urbEntry->TransferBufferLength > 0) {
        RtlCopyMemory(pendingUrb + 1, urbEntry->TransferBuffer, urbEntry->TransferBufferLength);
    }

    *BytesReturned = requiredSize;

    /* Note: URB entry is kept pending until completion */
    
    return STATUS_SUCCESS;
}

/**
 * VusbHandleCompleteUrb - Complete a pending URB
 */
NTSTATUS
VusbHandleCompleteUrb(
    _In_ PVUSB_DEVICE_CONTEXT DeviceContext,
    _In_ WDFREQUEST Request,
    _In_ size_t InputBufferLength
)
{
    NTSTATUS status;
    PVUSB_URB_COMPLETION completion;
    PVUSB_URB_ENTRY urbEntry;
    PUCHAR data = NULL;
    PMDL inputMdl;
    PVOID inputBuffer;

    if (InputBufferLength < sizeof(VUSB_URB_COMPLETION)) {
        return STATUS_BUFFER_TOO_SMALL;
    }

    /* Get input buffer */
    status = WdfRequestRetrieveInputWdmMdl(Request, &inputMdl);
    if (NT_SUCCESS(status) && inputMdl != NULL) {
        inputBuffer = MmGetSystemAddressForMdlSafe(inputMdl, NormalPagePriority);
    } else {
        status = WdfRequestRetrieveInputBuffer(Request, sizeof(VUSB_URB_COMPLETION),
                                               &inputBuffer, NULL);
        if (!NT_SUCCESS(status)) {
            return status;
        }
    }

    completion = (PVUSB_URB_COMPLETION)inputBuffer;

    /* Find the pending URB */
    urbEntry = VusbFindUrb(DeviceContext, completion->UrbId);
    if (urbEntry == NULL) {
        KdPrint(("VirtualUSB: CompleteUrb - URB %lu not found\n", completion->UrbId));
        return STATUS_NOT_FOUND;
    }

    /* Get IN data if present */
    if (completion->ActualLength > 0 && 
        InputBufferLength >= sizeof(VUSB_URB_COMPLETION) + completion->ActualLength) {
        data = (PUCHAR)(completion + 1);
    }

    /* Complete the URB */
    VusbCompleteUrb(DeviceContext, urbEntry, 
                   (completion->Status == VUSB_STATUS_SUCCESS) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL,
                   completion->ActualLength, data);

    return STATUS_SUCCESS;
}

/**
 * VusbHandleCancelUrb - Cancel a pending URB
 */
NTSTATUS
VusbHandleCancelUrb(
    _In_ PVUSB_DEVICE_CONTEXT DeviceContext,
    _In_ WDFREQUEST Request,
    _In_ size_t InputBufferLength
)
{
    NTSTATUS status;
    PVUSB_URB_CANCEL_REQUEST cancelRequest;

    if (InputBufferLength < sizeof(VUSB_URB_CANCEL_REQUEST)) {
        return STATUS_BUFFER_TOO_SMALL;
    }

    status = WdfRequestRetrieveInputBuffer(Request, sizeof(VUSB_URB_CANCEL_REQUEST),
                                           (PVOID*)&cancelRequest, NULL);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    VusbCancelUrb(DeviceContext, cancelRequest->UrbId);

    return STATUS_SUCCESS;
}

/**
 * VusbHandleGetStatistics - Get driver statistics
 */
NTSTATUS
VusbHandleGetStatistics(
    _In_ PVUSB_DEVICE_CONTEXT DeviceContext,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _Out_ size_t* BytesReturned
)
{
    NTSTATUS status;
    PVUSB_STATISTICS statistics;

    *BytesReturned = 0;

    if (OutputBufferLength < sizeof(VUSB_STATISTICS)) {
        return STATUS_BUFFER_TOO_SMALL;
    }

    status = WdfRequestRetrieveOutputBuffer(Request, sizeof(VUSB_STATISTICS),
                                            (PVOID*)&statistics, NULL);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    RtlCopyMemory(statistics, &DeviceContext->Statistics, sizeof(VUSB_STATISTICS));
    statistics->ActiveDevices = DeviceContext->DeviceCount;
    statistics->PendingUrbs = DeviceContext->PendingUrbCount;

    *BytesReturned = sizeof(VUSB_STATISTICS);
    return STATUS_SUCCESS;
}

/**
 * VusbHandleResetDevice - Reset a virtual device
 */
NTSTATUS
VusbHandleResetDevice(
    _In_ PVUSB_DEVICE_CONTEXT DeviceContext,
    _In_ WDFREQUEST Request,
    _In_ size_t InputBufferLength
)
{
    NTSTATUS status;
    PVUSB_UNPLUG_REQUEST resetRequest;
    PVUSB_VIRTUAL_DEVICE vdev;
    KIRQL oldIrql;

    if (InputBufferLength < sizeof(VUSB_UNPLUG_REQUEST)) {
        return STATUS_BUFFER_TOO_SMALL;
    }

    status = WdfRequestRetrieveInputBuffer(Request, sizeof(VUSB_UNPLUG_REQUEST),
                                           (PVOID*)&resetRequest, NULL);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    KeAcquireSpinLock(&DeviceContext->DeviceListLock, &oldIrql);

    vdev = VusbFindDevice(DeviceContext, resetRequest->DeviceId);
    if (vdev == NULL) {
        KeReleaseSpinLock(&DeviceContext->DeviceListLock, oldIrql);
        return STATUS_DEVICE_NOT_CONNECTED;
    }

    /* Reset device state */
    vdev->State = VUSB_STATE_DEFAULT;
    vdev->CurrentConfiguration = 0;
    vdev->CurrentInterface = 0;
    vdev->CurrentAlternateSetting = 0;

    KeReleaseSpinLock(&DeviceContext->DeviceListLock, oldIrql);

    KdPrint(("VirtualUSB: Reset device - ID %lu\n", resetRequest->DeviceId));
    return STATUS_SUCCESS;
}
