/**
 * Virtual USB URB Handler Header
 */

#ifndef VUSB_URB_H
#define VUSB_URB_H

#include <ntddk.h>
#include <wdf.h>
#include <usb.h>
#include "vusb_driver.h"

/* URB Creation and management */
NTSTATUS VusbUrbCreate(
    _In_ PVUSB_DEVICE_CONTEXT DeviceContext,
    _In_ ULONG DeviceId,
    _In_ PURB Urb,
    _In_opt_ WDFREQUEST Request,
    _Out_ PVUSB_URB_ENTRY* UrbEntry
);

VOID VusbUrbParse(
    _Inout_ PVUSB_URB_ENTRY Entry,
    _In_ PURB Urb
);

VOID VusbUrbFree(
    _In_ PVUSB_URB_ENTRY Entry
);

/* Buffer operations */
PVOID VusbUrbGetBuffer(
    _In_ PVUSB_URB_ENTRY Entry,
    _Out_opt_ PULONG Length
);

NTSTATUS VusbUrbCopyData(
    _In_ PVUSB_URB_ENTRY Entry,
    _In_ PVOID Data,
    _In_ ULONG DataLength,
    _In_ BOOLEAN ToUrb
);

/* Response building */
NTSTATUS VusbUrbBuildPendingResponse(
    _In_ PVUSB_URB_ENTRY Entry,
    _Out_writes_bytes_(BufferSize) PVUSB_PENDING_URB Response,
    _In_ ULONG BufferSize,
    _Out_ PULONG ActualSize
);

/* URB completion */
NTSTATUS VusbUrbComplete(
    _In_ PVUSB_DEVICE_CONTEXT DeviceContext,
    _In_ PVUSB_URB_ENTRY Entry,
    _In_ NTSTATUS Status,
    _In_ ULONG ActualLength,
    _In_reads_bytes_opt_(ActualLength) PVOID Data
);

/* Helper functions */
USHORT VusbUrbGetFunction(
    _In_ PVUSB_URB_ENTRY Entry
);

/* Standard request handling */
NTSTATUS VusbProcessStandardRequest(
    _In_ PVUSB_DEVICE_CONTEXT DeviceContext,
    _In_ PVUSB_URB_ENTRY Entry,
    _Out_ PBOOLEAN Handled
);

#endif /* VUSB_URB_H */
