/**
 * Virtual USB Driver Header
 * 
 * Internal definitions for the virtual USB driver.
 */

#ifndef VUSB_DRIVER_H
#define VUSB_DRIVER_H

#include <ntddk.h>
#include <wdf.h>
#include "../protocol/vusb_protocol.h"

/* Device naming */
#define VUSB_DEVICE_NAME    L"\\Device\\VirtualUSB"
#define VUSB_SYMBOLIC_NAME  L"\\DosDevices\\VirtualUSB"

/* Maximum endpoints per device */
#define VUSB_MAX_ENDPOINTS  32

/* Forward declarations */
typedef struct _VUSB_DEVICE_CONTEXT VUSB_DEVICE_CONTEXT, *PVUSB_DEVICE_CONTEXT;
typedef struct _VUSB_VIRTUAL_DEVICE VUSB_VIRTUAL_DEVICE, *PVUSB_VIRTUAL_DEVICE;
typedef struct _VUSB_URB_ENTRY VUSB_URB_ENTRY, *PVUSB_URB_ENTRY;

/**
 * Virtual Device Structure
 * Represents a single virtual USB device
 */
typedef struct _VUSB_VIRTUAL_DEVICE {
    LIST_ENTRY          ListEntry;          /* Link in device list */
    ULONG               DeviceId;           /* Unique device ID */
    ULONG               PortNumber;         /* Virtual port number */
    VUSB_DEVICE_STATE   State;              /* Current device state */
    VUSB_DEVICE_INFO    DeviceInfo;         /* Device information */
    
    /* Descriptors */
    PUCHAR              Descriptors;        /* All USB descriptors */
    ULONG               DescriptorLength;   /* Total descriptor length */
    
    /* Endpoints */
    UCHAR               NumEndpoints;
    UCHAR               EndpointAddresses[VUSB_MAX_ENDPOINTS];
    
    /* Configuration */
    UCHAR               CurrentConfiguration;
    UCHAR               CurrentInterface;
    UCHAR               CurrentAlternateSetting;
    
    /* Statistics */
    ULONG64             BytesIn;
    ULONG64             BytesOut;
    ULONG               UrbsCompleted;
    ULONG               UrbsError;
} VUSB_VIRTUAL_DEVICE, *PVUSB_VIRTUAL_DEVICE;

/**
 * URB Entry - Pending URB waiting for user-mode completion
 */
typedef struct _VUSB_URB_ENTRY {
    LIST_ENTRY          ListEntry;          /* Link in pending URB list */
    ULONG               UrbId;              /* Unique URB ID */
    ULONG               SequenceNumber;     /* Sequence number */
    ULONG               DeviceId;           /* Target device */
    WDFREQUEST          Request;            /* Original WDF request */
    PIRP                Irp;                /* Original IRP (if applicable) */
    
    /* Transfer info */
    UCHAR               EndpointAddress;
    UCHAR               TransferType;
    UCHAR               Direction;
    ULONG               TransferFlags;
    ULONG               TransferBufferLength;
    PVOID               TransferBuffer;
    PMDL                TransferBufferMdl;
    VUSB_SETUP_PACKET   SetupPacket;
    
    /* Timing */
    LARGE_INTEGER       SubmitTime;
    ULONG               Timeout;
} VUSB_URB_ENTRY, *PVUSB_URB_ENTRY;

/**
 * Device Context - Per-device driver data
 */
typedef struct _VUSB_DEVICE_CONTEXT {
    WDFDEVICE           Device;             /* WDF device handle */
    WDFQUEUE            PendingUrbQueue;    /* Queue for pending URB requests */
    
    /* Virtual devices */
    ULONG               MaxDevices;         /* Maximum devices supported */
    ULONG               DeviceCount;        /* Current device count */
    KSPIN_LOCK          DeviceListLock;     /* Spinlock for device list */
    LIST_ENTRY          DeviceList;         /* List of virtual devices */
    PVUSB_VIRTUAL_DEVICE Devices[VUSB_MAX_DEVICES]; /* Quick access array */
    
    /* URB management */
    ULONG               NextUrbId;          /* Next URB ID to assign */
    ULONG               NextSequence;       /* Next sequence number */
    KSPIN_LOCK          UrbQueueLock;       /* Spinlock for URB queue */
    LIST_ENTRY          PendingUrbList;     /* List of pending URBs */
    ULONG               PendingUrbCount;    /* Number of pending URBs */
    
    /* Statistics */
    VUSB_STATISTICS     Statistics;
} VUSB_DEVICE_CONTEXT, *PVUSB_DEVICE_CONTEXT;

/* Context accessor macro */
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(VUSB_DEVICE_CONTEXT, VusbGetDeviceContext)

/* Driver entry and callbacks */
DRIVER_INITIALIZE DriverEntry;
EVT_WDF_OBJECT_CONTEXT_CLEANUP VusbEvtDeviceContextCleanup;

/* IOCTL handlers */
NTSTATUS VusbHandleGetVersion(
    _In_ PVUSB_DEVICE_CONTEXT DeviceContext,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _Out_ size_t* BytesReturned
);

NTSTATUS VusbHandlePluginDevice(
    _In_ PVUSB_DEVICE_CONTEXT DeviceContext,
    _In_ WDFREQUEST Request,
    _In_ size_t InputBufferLength,
    _In_ size_t OutputBufferLength,
    _Out_ size_t* BytesReturned
);

NTSTATUS VusbHandleUnplugDevice(
    _In_ PVUSB_DEVICE_CONTEXT DeviceContext,
    _In_ WDFREQUEST Request,
    _In_ size_t InputBufferLength
);

NTSTATUS VusbHandleGetDeviceList(
    _In_ PVUSB_DEVICE_CONTEXT DeviceContext,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _Out_ size_t* BytesReturned
);

NTSTATUS VusbHandleGetPendingUrb(
    _In_ PVUSB_DEVICE_CONTEXT DeviceContext,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _Out_ size_t* BytesReturned
);

NTSTATUS VusbHandleCompleteUrb(
    _In_ PVUSB_DEVICE_CONTEXT DeviceContext,
    _In_ WDFREQUEST Request,
    _In_ size_t InputBufferLength
);

NTSTATUS VusbHandleCancelUrb(
    _In_ PVUSB_DEVICE_CONTEXT DeviceContext,
    _In_ WDFREQUEST Request,
    _In_ size_t InputBufferLength
);

NTSTATUS VusbHandleGetStatistics(
    _In_ PVUSB_DEVICE_CONTEXT DeviceContext,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _Out_ size_t* BytesReturned
);

NTSTATUS VusbHandleResetDevice(
    _In_ PVUSB_DEVICE_CONTEXT DeviceContext,
    _In_ WDFREQUEST Request,
    _In_ size_t InputBufferLength
);

/* Virtual device management */
NTSTATUS VusbCreateVirtualDevice(
    _In_ PVUSB_DEVICE_CONTEXT DeviceContext,
    _In_ PVUSB_DEVICE_INFO DeviceInfo,
    _In_reads_bytes_(DescriptorLength) PUCHAR Descriptors,
    _In_ ULONG DescriptorLength,
    _Out_ PULONG DeviceId
);

NTSTATUS VusbDestroyVirtualDevice(
    _In_ PVUSB_DEVICE_CONTEXT DeviceContext,
    _In_ ULONG DeviceId
);

PVUSB_VIRTUAL_DEVICE VusbFindDevice(
    _In_ PVUSB_DEVICE_CONTEXT DeviceContext,
    _In_ ULONG DeviceId
);

VOID VusbCleanupAllDevices(
    _In_ PVUSB_DEVICE_CONTEXT DeviceContext
);

/* URB management */
NTSTATUS VusbQueueUrb(
    _In_ PVUSB_DEVICE_CONTEXT DeviceContext,
    _In_ PVUSB_URB_ENTRY UrbEntry
);

PVUSB_URB_ENTRY VusbDequeueUrb(
    _In_ PVUSB_DEVICE_CONTEXT DeviceContext
);

PVUSB_URB_ENTRY VusbFindUrb(
    _In_ PVUSB_DEVICE_CONTEXT DeviceContext,
    _In_ ULONG UrbId
);

VOID VusbCompleteUrb(
    _In_ PVUSB_DEVICE_CONTEXT DeviceContext,
    _In_ PVUSB_URB_ENTRY UrbEntry,
    _In_ NTSTATUS Status,
    _In_ ULONG ActualLength,
    _In_reads_bytes_opt_(ActualLength) PVOID Data
);

VOID VusbCancelUrb(
    _In_ PVUSB_DEVICE_CONTEXT DeviceContext,
    _In_ ULONG UrbId
);

#endif /* VUSB_DRIVER_H */
