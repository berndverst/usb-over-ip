/**
 * Virtual USB Driver IOCTL Definitions
 * 
 * Defines the IOCTL interface between the user-mode server application
 * and the kernel-mode virtual USB driver.
 */

#ifndef VUSB_IOCTL_H
#define VUSB_IOCTL_H

#ifdef _KERNEL_MODE
#include <ntddk.h>
#else
#include <windows.h>
#include <winioctl.h>
#endif

#include "vusb_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Device interface GUID for the virtual USB controller */
/* {8D8E8C7A-1B2C-4D5E-9F0A-1B2C3D4E5F6A} */
DEFINE_GUID(GUID_DEVINTERFACE_VUSB_CONTROLLER,
    0x8d8e8c7a, 0x1b2c, 0x4d5e, 0x9f, 0x0a, 0x1b, 0x2c, 0x3d, 0x4e, 0x5f, 0x6a);

/* Device type for IOCTL codes */
#define FILE_DEVICE_VUSB    0x8000

/* IOCTL Function codes */
#define VUSB_IOCTL_INDEX_BASE               0x800

#define IOCTL_VUSB_GET_VERSION              CTL_CODE(FILE_DEVICE_VUSB, VUSB_IOCTL_INDEX_BASE + 0, METHOD_BUFFERED, FILE_READ_ACCESS)
#define IOCTL_VUSB_PLUGIN_DEVICE            CTL_CODE(FILE_DEVICE_VUSB, VUSB_IOCTL_INDEX_BASE + 1, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define IOCTL_VUSB_UNPLUG_DEVICE            CTL_CODE(FILE_DEVICE_VUSB, VUSB_IOCTL_INDEX_BASE + 2, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define IOCTL_VUSB_GET_DEVICE_LIST          CTL_CODE(FILE_DEVICE_VUSB, VUSB_IOCTL_INDEX_BASE + 3, METHOD_BUFFERED, FILE_READ_ACCESS)
#define IOCTL_VUSB_GET_PENDING_URB          CTL_CODE(FILE_DEVICE_VUSB, VUSB_IOCTL_INDEX_BASE + 4, METHOD_OUT_DIRECT, FILE_READ_ACCESS)
#define IOCTL_VUSB_COMPLETE_URB             CTL_CODE(FILE_DEVICE_VUSB, VUSB_IOCTL_INDEX_BASE + 5, METHOD_IN_DIRECT, FILE_WRITE_ACCESS)
#define IOCTL_VUSB_CANCEL_URB               CTL_CODE(FILE_DEVICE_VUSB, VUSB_IOCTL_INDEX_BASE + 6, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define IOCTL_VUSB_GET_STATISTICS           CTL_CODE(FILE_DEVICE_VUSB, VUSB_IOCTL_INDEX_BASE + 7, METHOD_BUFFERED, FILE_READ_ACCESS)
#define IOCTL_VUSB_RESET_DEVICE             CTL_CODE(FILE_DEVICE_VUSB, VUSB_IOCTL_INDEX_BASE + 8, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define IOCTL_VUSB_SET_DEVICE_STATE         CTL_CODE(FILE_DEVICE_VUSB, VUSB_IOCTL_INDEX_BASE + 9, METHOD_BUFFERED, FILE_WRITE_ACCESS)

#pragma pack(push, 1)

/* Version information */
typedef struct _VUSB_VERSION_INFO {
    uint32_t    DriverVersion;      /* Driver version */
    uint32_t    ProtocolVersion;    /* Supported protocol version */
    uint32_t    MaxDevices;         /* Maximum supported devices */
    uint32_t    Capabilities;       /* Driver capabilities */
} VUSB_VERSION_INFO;

/* Plugin device request */
typedef struct _VUSB_PLUGIN_REQUEST {
    VUSB_DEVICE_INFO    DeviceInfo;
    uint32_t            DescriptorLength;
    /* Followed by descriptor data */
} VUSB_PLUGIN_REQUEST;

/* Plugin device response */
typedef struct _VUSB_PLUGIN_RESPONSE {
    uint32_t    Status;             /* Result status */
    uint32_t    DeviceId;           /* Assigned device ID */
    uint32_t    PortNumber;         /* Assigned port number */
} VUSB_PLUGIN_RESPONSE;

/* Unplug device request */
typedef struct _VUSB_UNPLUG_REQUEST {
    uint32_t    DeviceId;           /* Device to unplug */
} VUSB_UNPLUG_REQUEST;

/* Device list entry */
typedef struct _VUSB_DEVICE_ENTRY {
    uint32_t            DeviceId;
    uint32_t            PortNumber;
    uint32_t            State;          /* Device state */
    VUSB_DEVICE_INFO    DeviceInfo;
} VUSB_DEVICE_ENTRY;

/* Device list response */
typedef struct _VUSB_DEVICE_LIST {
    uint32_t            DeviceCount;
    VUSB_DEVICE_ENTRY   Devices[VUSB_MAX_DEVICES];
} VUSB_DEVICE_LIST;

/* Pending URB information (sent to user mode for processing) */
typedef struct _VUSB_PENDING_URB {
    uint32_t            DeviceId;
    uint32_t            UrbId;
    uint32_t            SequenceNumber;
    uint8_t             EndpointAddress;
    uint8_t             TransferType;
    uint8_t             Direction;
    uint8_t             Reserved;
    uint32_t            TransferFlags;
    uint32_t            TransferBufferLength;
    uint32_t            Interval;
    VUSB_SETUP_PACKET   SetupPacket;
    /* For OUT transfers, data follows */
} VUSB_PENDING_URB;

/* URB completion (from user mode to driver) */
typedef struct _VUSB_URB_COMPLETION {
    uint32_t    DeviceId;
    uint32_t    UrbId;
    uint32_t    SequenceNumber;
    uint32_t    Status;             /* VUSB_STATUS */
    uint32_t    ActualLength;
    /* For IN transfers, data follows */
} VUSB_URB_COMPLETION;

/* URB cancel request */
typedef struct _VUSB_URB_CANCEL_REQUEST {
    uint32_t    DeviceId;
    uint32_t    UrbId;
} VUSB_URB_CANCEL_REQUEST;

/* Driver statistics */
typedef struct _VUSB_STATISTICS {
    uint64_t    TotalUrbsSubmitted;
    uint64_t    TotalUrbsCompleted;
    uint64_t    TotalUrbsCanceled;
    uint64_t    TotalBytesIn;
    uint64_t    TotalBytesOut;
    uint64_t    TotalErrors;
    uint32_t    ActiveDevices;
    uint32_t    PendingUrbs;
} VUSB_STATISTICS;

/* Device state */
typedef enum _VUSB_DEVICE_STATE {
    VUSB_STATE_DISCONNECTED     = 0,
    VUSB_STATE_ATTACHED         = 1,
    VUSB_STATE_POWERED          = 2,
    VUSB_STATE_DEFAULT          = 3,
    VUSB_STATE_ADDRESSED        = 4,
    VUSB_STATE_CONFIGURED       = 5,
    VUSB_STATE_SUSPENDED        = 6,
} VUSB_DEVICE_STATE;

/* Set device state request */
typedef struct _VUSB_SET_STATE_REQUEST {
    uint32_t    DeviceId;
    uint32_t    NewState;           /* VUSB_DEVICE_STATE */
} VUSB_SET_STATE_REQUEST;

#pragma pack(pop)

#ifdef __cplusplus
}
#endif

#endif /* VUSB_IOCTL_H */
