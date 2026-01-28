/**
 * Virtual Device Management
 * 
 * Implementation of virtual USB device creation, destruction, and management.
 */

#include <ntddk.h>
#include <wdf.h>

#include "vusb_driver.h"

/**
 * VusbCreateVirtualDevice - Create a new virtual USB device
 */
NTSTATUS
VusbCreateVirtualDevice(
    _In_ PVUSB_DEVICE_CONTEXT DeviceContext,
    _In_ PVUSB_DEVICE_INFO DeviceInfo,
    _In_reads_bytes_(DescriptorLength) PUCHAR Descriptors,
    _In_ ULONG DescriptorLength,
    _Out_ PULONG DeviceId
)
{
    NTSTATUS status = STATUS_SUCCESS;
    PVUSB_VIRTUAL_DEVICE vdev = NULL;
    KIRQL oldIrql;
    ULONG slot;

    *DeviceId = 0;

    /* Allocate virtual device structure */
    vdev = (PVUSB_VIRTUAL_DEVICE)ExAllocatePool2(
        POOL_FLAG_NON_PAGED,
        sizeof(VUSB_VIRTUAL_DEVICE),
        'vusb'
    );

    if (vdev == NULL) {
        KdPrint(("VirtualUSB: Failed to allocate virtual device\n"));
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(vdev, sizeof(VUSB_VIRTUAL_DEVICE));

    /* Allocate and copy descriptors */
    if (DescriptorLength > 0 && Descriptors != NULL) {
        vdev->Descriptors = (PUCHAR)ExAllocatePool2(
            POOL_FLAG_NON_PAGED,
            DescriptorLength,
            'vusb'
        );

        if (vdev->Descriptors == NULL) {
            ExFreePoolWithTag(vdev, 'vusb');
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        RtlCopyMemory(vdev->Descriptors, Descriptors, DescriptorLength);
        vdev->DescriptorLength = DescriptorLength;
    }

    /* Copy device info */
    RtlCopyMemory(&vdev->DeviceInfo, DeviceInfo, sizeof(VUSB_DEVICE_INFO));

    /* Find a free slot and assign ID */
    KeAcquireSpinLock(&DeviceContext->DeviceListLock, &oldIrql);

    if (DeviceContext->DeviceCount >= DeviceContext->MaxDevices) {
        KeReleaseSpinLock(&DeviceContext->DeviceListLock, oldIrql);
        if (vdev->Descriptors) {
            ExFreePoolWithTag(vdev->Descriptors, 'vusb');
        }
        ExFreePoolWithTag(vdev, 'vusb');
        return STATUS_TOO_MANY_NODES;
    }

    for (slot = 0; slot < VUSB_MAX_DEVICES; slot++) {
        if (DeviceContext->Devices[slot] == NULL) {
            vdev->DeviceId = slot + 1;  /* IDs start at 1 */
            vdev->PortNumber = slot + 1;
            vdev->State = VUSB_STATE_ATTACHED;
            
            DeviceContext->Devices[slot] = vdev;
            DeviceContext->DeviceCount++;
            
            *DeviceId = vdev->DeviceId;
            break;
        }
    }

    KeReleaseSpinLock(&DeviceContext->DeviceListLock, oldIrql);

    if (*DeviceId == 0) {
        /* No slot found (shouldn't happen) */
        if (vdev->Descriptors) {
            ExFreePoolWithTag(vdev->Descriptors, 'vusb');
        }
        ExFreePoolWithTag(vdev, 'vusb');
        return STATUS_TOO_MANY_NODES;
    }

    KdPrint(("VirtualUSB: Created virtual device ID %lu, VID:PID %04X:%04X\n",
             vdev->DeviceId, DeviceInfo->VendorId, DeviceInfo->ProductId));

    return STATUS_SUCCESS;
}

/**
 * VusbDestroyVirtualDevice - Destroy a virtual USB device
 */
NTSTATUS
VusbDestroyVirtualDevice(
    _In_ PVUSB_DEVICE_CONTEXT DeviceContext,
    _In_ ULONG DeviceId
)
{
    PVUSB_VIRTUAL_DEVICE vdev;
    KIRQL oldIrql;
    ULONG slot;

    if (DeviceId == 0 || DeviceId > VUSB_MAX_DEVICES) {
        return STATUS_INVALID_PARAMETER;
    }

    slot = DeviceId - 1;

    KeAcquireSpinLock(&DeviceContext->DeviceListLock, &oldIrql);

    vdev = DeviceContext->Devices[slot];
    if (vdev == NULL) {
        KeReleaseSpinLock(&DeviceContext->DeviceListLock, oldIrql);
        return STATUS_DEVICE_NOT_CONNECTED;
    }

    /* Remove from array */
    DeviceContext->Devices[slot] = NULL;
    DeviceContext->DeviceCount--;

    KeReleaseSpinLock(&DeviceContext->DeviceListLock, oldIrql);

    /* Free resources */
    if (vdev->Descriptors) {
        ExFreePoolWithTag(vdev->Descriptors, 'vusb');
    }
    ExFreePoolWithTag(vdev, 'vusb');

    KdPrint(("VirtualUSB: Destroyed virtual device ID %lu\n", DeviceId));

    return STATUS_SUCCESS;
}

/**
 * VusbFindDevice - Find a virtual device by ID
 */
PVUSB_VIRTUAL_DEVICE
VusbFindDevice(
    _In_ PVUSB_DEVICE_CONTEXT DeviceContext,
    _In_ ULONG DeviceId
)
{
    if (DeviceId == 0 || DeviceId > VUSB_MAX_DEVICES) {
        return NULL;
    }

    return DeviceContext->Devices[DeviceId - 1];
}

/**
 * VusbCleanupAllDevices - Destroy all virtual devices
 */
VOID
VusbCleanupAllDevices(
    _In_ PVUSB_DEVICE_CONTEXT DeviceContext
)
{
    ULONG i;

    for (i = 1; i <= VUSB_MAX_DEVICES; i++) {
        VusbDestroyVirtualDevice(DeviceContext, i);
    }
}

/**
 * VusbQueueUrb - Add URB to pending queue
 */
NTSTATUS
VusbQueueUrb(
    _In_ PVUSB_DEVICE_CONTEXT DeviceContext,
    _In_ PVUSB_URB_ENTRY UrbEntry
)
{
    KIRQL oldIrql;

    KeAcquireSpinLock(&DeviceContext->UrbQueueLock, &oldIrql);
    
    InsertTailList(&DeviceContext->PendingUrbList, &UrbEntry->ListEntry);
    DeviceContext->PendingUrbCount++;
    DeviceContext->Statistics.TotalUrbsSubmitted++;

    KeReleaseSpinLock(&DeviceContext->UrbQueueLock, oldIrql);

    return STATUS_SUCCESS;
}

/**
 * VusbDequeueUrb - Remove next URB from pending queue
 */
PVUSB_URB_ENTRY
VusbDequeueUrb(
    _In_ PVUSB_DEVICE_CONTEXT DeviceContext
)
{
    KIRQL oldIrql;
    PLIST_ENTRY entry;
    PVUSB_URB_ENTRY urbEntry = NULL;

    KeAcquireSpinLock(&DeviceContext->UrbQueueLock, &oldIrql);

    if (!IsListEmpty(&DeviceContext->PendingUrbList)) {
        entry = RemoveHeadList(&DeviceContext->PendingUrbList);
        urbEntry = CONTAINING_RECORD(entry, VUSB_URB_ENTRY, ListEntry);
        DeviceContext->PendingUrbCount--;
    }

    KeReleaseSpinLock(&DeviceContext->UrbQueueLock, oldIrql);

    return urbEntry;
}

/**
 * VusbFindUrb - Find a pending URB by ID
 */
PVUSB_URB_ENTRY
VusbFindUrb(
    _In_ PVUSB_DEVICE_CONTEXT DeviceContext,
    _In_ ULONG UrbId
)
{
    KIRQL oldIrql;
    PLIST_ENTRY entry;
    PVUSB_URB_ENTRY urbEntry = NULL;

    KeAcquireSpinLock(&DeviceContext->UrbQueueLock, &oldIrql);

    for (entry = DeviceContext->PendingUrbList.Flink;
         entry != &DeviceContext->PendingUrbList;
         entry = entry->Flink) {
        
        PVUSB_URB_ENTRY current = CONTAINING_RECORD(entry, VUSB_URB_ENTRY, ListEntry);
        if (current->UrbId == UrbId) {
            /* Remove from list */
            RemoveEntryList(entry);
            DeviceContext->PendingUrbCount--;
            urbEntry = current;
            break;
        }
    }

    KeReleaseSpinLock(&DeviceContext->UrbQueueLock, oldIrql);

    return urbEntry;
}

/**
 * VusbCompleteUrb - Complete a URB with data
 */
VOID
VusbCompleteUrb(
    _In_ PVUSB_DEVICE_CONTEXT DeviceContext,
    _In_ PVUSB_URB_ENTRY UrbEntry,
    _In_ NTSTATUS Status,
    _In_ ULONG ActualLength,
    _In_reads_bytes_opt_(ActualLength) PVOID Data
)
{
    PVUSB_VIRTUAL_DEVICE vdev;

    /* Update statistics */
    if (NT_SUCCESS(Status)) {
        DeviceContext->Statistics.TotalUrbsCompleted++;
        if (UrbEntry->Direction == VUSB_DIR_IN) {
            DeviceContext->Statistics.TotalBytesIn += ActualLength;
        } else {
            DeviceContext->Statistics.TotalBytesOut += ActualLength;
        }
    } else {
        DeviceContext->Statistics.TotalErrors++;
    }

    /* Update device statistics */
    vdev = VusbFindDevice(DeviceContext, UrbEntry->DeviceId);
    if (vdev) {
        if (NT_SUCCESS(Status)) {
            vdev->UrbsCompleted++;
            if (UrbEntry->Direction == VUSB_DIR_IN) {
                vdev->BytesIn += ActualLength;
            } else {
                vdev->BytesOut += ActualLength;
            }
        } else {
            vdev->UrbsError++;
        }
    }

    /* Copy data for IN transfers */
    if (Data && ActualLength > 0 && UrbEntry->TransferBuffer) {
        ULONG copyLength = min(ActualLength, UrbEntry->TransferBufferLength);
        RtlCopyMemory(UrbEntry->TransferBuffer, Data, copyLength);
    }

    /* Complete the original request if present */
    if (UrbEntry->Request) {
        WdfRequestCompleteWithInformation(UrbEntry->Request, Status, ActualLength);
    }

    /* Free URB entry */
    if (UrbEntry->TransferBuffer && UrbEntry->Direction == VUSB_DIR_OUT) {
        /* We allocated this for OUT transfers */
        ExFreePoolWithTag(UrbEntry->TransferBuffer, 'vusb');
    }
    ExFreePoolWithTag(UrbEntry, 'vusb');
}

/**
 * VusbCancelUrb - Cancel a pending URB
 */
VOID
VusbCancelUrb(
    _In_ PVUSB_DEVICE_CONTEXT DeviceContext,
    _In_ ULONG UrbId
)
{
    PVUSB_URB_ENTRY urbEntry;

    urbEntry = VusbFindUrb(DeviceContext, UrbId);
    if (urbEntry) {
        DeviceContext->Statistics.TotalUrbsCanceled++;
        VusbCompleteUrb(DeviceContext, urbEntry, STATUS_CANCELLED, 0, NULL);
    }
}
