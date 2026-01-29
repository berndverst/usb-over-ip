/**
 * Virtual USB Protocol Definitions
 * 
 * Common protocol definitions for USB over network communication.
 * This header is shared between driver, server, and client components.
 */

#ifndef VUSB_PROTOCOL_H
#define VUSB_PROTOCOL_H

#ifdef _KERNEL_MODE
#include <ntddk.h>
typedef UCHAR uint8_t;
typedef USHORT uint16_t;
typedef ULONG uint32_t;
typedef ULONG64 uint64_t;
#else
#include <stdint.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Protocol Constants */
#define VUSB_PROTOCOL_MAGIC     0x56555342  /* "VUSB" */
#define VUSB_PROTOCOL_VERSION   0x0100      /* Version 1.0 */
#define VUSB_DEFAULT_PORT       7575
#define VUSB_MAX_PACKET_SIZE    65536
#define VUSB_MAX_DEVICES        16

/* Command Types */
typedef enum _VUSB_COMMAND {
    /* Connection Management */
    VUSB_CMD_CONNECT            = 0x0001,   /* Client connects to server */
    VUSB_CMD_DISCONNECT         = 0x0002,   /* Client disconnects */
    VUSB_CMD_PING               = 0x0003,   /* Keep-alive ping */
    VUSB_CMD_PONG               = 0x0004,   /* Keep-alive response */
    
    /* Device Management */
    VUSB_CMD_DEVICE_ATTACH      = 0x0010,   /* Attach a new USB device */
    VUSB_CMD_DEVICE_DETACH      = 0x0011,   /* Detach a USB device */
    VUSB_CMD_DEVICE_LIST        = 0x0012,   /* List available devices */
    VUSB_CMD_DEVICE_INFO        = 0x0013,   /* Get device information */
    
    /* USB Transfers */
    VUSB_CMD_SUBMIT_URB         = 0x0020,   /* Submit USB Request Block */
    VUSB_CMD_URB_COMPLETE       = 0x0021,   /* URB completion notification */
    VUSB_CMD_CANCEL_URB         = 0x0022,   /* Cancel pending URB */
    
    /* Descriptor Requests */
    VUSB_CMD_GET_DESCRIPTOR     = 0x0030,   /* Get USB descriptor */
    VUSB_CMD_DESCRIPTOR_DATA    = 0x0031,   /* Descriptor response */
    
    /* Control Transfers */
    VUSB_CMD_CONTROL_TRANSFER   = 0x0040,   /* Control transfer request */
    VUSB_CMD_CONTROL_RESPONSE   = 0x0041,   /* Control transfer response */
    
    /* Bulk/Interrupt Transfers */
    VUSB_CMD_BULK_TRANSFER      = 0x0050,   /* Bulk transfer */
    VUSB_CMD_INTERRUPT_TRANSFER = 0x0051,   /* Interrupt transfer */
    VUSB_CMD_TRANSFER_COMPLETE  = 0x0052,   /* Transfer completion */
    
    /* Isochronous Transfers */
    VUSB_CMD_ISO_TRANSFER       = 0x0060,   /* Isochronous transfer */
    VUSB_CMD_ISO_COMPLETE       = 0x0061,   /* Isochronous completion */
    
    /* Error/Status */
    VUSB_CMD_ERROR              = 0x00FF,   /* Error response */
    VUSB_CMD_STATUS             = 0x00FE,   /* Status response */
} VUSB_COMMAND;

/* Status Codes */
typedef enum _VUSB_STATUS {
    VUSB_STATUS_SUCCESS         = 0x0000,
    VUSB_STATUS_PENDING         = 0x0001,
    VUSB_STATUS_ERROR           = 0x0002,
    VUSB_STATUS_STALL           = 0x0003,
    VUSB_STATUS_TIMEOUT         = 0x0004,
    VUSB_STATUS_CANCELED        = 0x0005,
    VUSB_STATUS_NO_DEVICE       = 0x0006,
    VUSB_STATUS_INVALID_PARAM   = 0x0007,
    VUSB_STATUS_NO_MEMORY       = 0x0008,
    VUSB_STATUS_NOT_SUPPORTED   = 0x0009,
    VUSB_STATUS_DISCONNECTED    = 0x000A,
} VUSB_STATUS;

/* USB Speed */
typedef enum _VUSB_SPEED {
    VUSB_SPEED_UNKNOWN          = 0,
    VUSB_SPEED_LOW              = 1,    /* 1.5 Mbps */
    VUSB_SPEED_FULL             = 2,    /* 12 Mbps */
    VUSB_SPEED_HIGH             = 3,    /* 480 Mbps */
    VUSB_SPEED_SUPER            = 4,    /* 5 Gbps */
    VUSB_SPEED_SUPER_PLUS       = 5,    /* 10 Gbps */
} VUSB_SPEED;

/* USB Transfer Type */
typedef enum _VUSB_TRANSFER_TYPE {
    VUSB_TRANSFER_CONTROL       = 0,
    VUSB_TRANSFER_ISOCHRONOUS   = 1,
    VUSB_TRANSFER_BULK          = 2,
    VUSB_TRANSFER_INTERRUPT     = 3,
} VUSB_TRANSFER_TYPE;

/* USB Direction */
typedef enum _VUSB_DIRECTION {
    VUSB_DIR_OUT                = 0,    /* Host to device */
    VUSB_DIR_IN                 = 1,    /* Device to host */
} VUSB_DIRECTION;

#pragma pack(push, 1)

/* Protocol Header - All messages start with this */
typedef struct _VUSB_HEADER {
    uint32_t    Magic;          /* VUSB_PROTOCOL_MAGIC */
    uint16_t    Version;        /* Protocol version */
    uint16_t    Command;        /* VUSB_COMMAND */
    uint32_t    Length;         /* Payload length (excluding header) */
    uint32_t    Sequence;       /* Sequence number for request/response matching */
} VUSB_HEADER, *PVUSB_HEADER;

/* Device Descriptor (simplified) */
typedef struct _VUSB_DEVICE_DESCRIPTOR {
    uint8_t     bLength;
    uint8_t     bDescriptorType;
    uint16_t    bcdUSB;
    uint8_t     bDeviceClass;
    uint8_t     bDeviceSubClass;
    uint8_t     bDeviceProtocol;
    uint8_t     bMaxPacketSize0;
    uint16_t    idVendor;
    uint16_t    idProduct;
    uint16_t    bcdDevice;
    uint8_t     iManufacturer;
    uint8_t     iProduct;
    uint8_t     iSerialNumber;
    uint8_t     bNumConfigurations;
} VUSB_DEVICE_DESCRIPTOR;

/* Device Information */
typedef struct _VUSB_DEVICE_INFO {
    uint32_t    DeviceId;           /* Unique device identifier */
    uint16_t    VendorId;           /* USB Vendor ID */
    uint16_t    ProductId;          /* USB Product ID */
    uint8_t     DeviceClass;        /* USB Device Class */
    uint8_t     DeviceSubClass;     /* USB Device SubClass */
    uint8_t     DeviceProtocol;     /* USB Device Protocol */
    uint8_t     Speed;              /* VUSB_SPEED */
    uint8_t     NumConfigurations;  /* Number of configurations */
    uint8_t     NumInterfaces;      /* Number of interfaces (current config) */
    uint8_t     Reserved[2];        /* Padding */
    char        Manufacturer[64];   /* Manufacturer string */
    char        Product[64];        /* Product string */
    char        SerialNumber[64];   /* Serial number string */
} VUSB_DEVICE_INFO, *PVUSB_DEVICE_INFO;

/* Connect Request */
typedef struct _VUSB_CONNECT_REQUEST {
    VUSB_HEADER Header;
    uint32_t    ClientVersion;      /* Client software version */
    uint32_t    Capabilities;       /* Client capabilities flags */
    char        ClientName[64];     /* Client identifier/name */
} VUSB_CONNECT_REQUEST;

/* Connect Response */
typedef struct _VUSB_CONNECT_RESPONSE {
    VUSB_HEADER Header;
    uint32_t    Status;             /* VUSB_STATUS */
    uint32_t    ServerVersion;      /* Server software version */
    uint32_t    Capabilities;       /* Server capabilities flags */
    uint32_t    SessionId;          /* Assigned session ID */
} VUSB_CONNECT_RESPONSE;

/* Device Attach Request - sent by client when USB device is connected */
typedef struct _VUSB_DEVICE_ATTACH_REQUEST {
    VUSB_HEADER         Header;
    VUSB_DEVICE_INFO    DeviceInfo;
    uint32_t            DescriptorLength;   /* Length of full descriptors */
    /* Followed by: uint8_t Descriptors[DescriptorLength] - all USB descriptors */
} VUSB_DEVICE_ATTACH_REQUEST;

/* Device Attach Response */
typedef struct _VUSB_DEVICE_ATTACH_RESPONSE {
    VUSB_HEADER Header;
    uint32_t    Status;             /* VUSB_STATUS */
    uint32_t    DeviceId;           /* Assigned virtual device ID */
} VUSB_DEVICE_ATTACH_RESPONSE;

/* Device Detach Request */
typedef struct _VUSB_DEVICE_DETACH_REQUEST {
    VUSB_HEADER Header;
    uint32_t    DeviceId;           /* Device to detach */
} VUSB_DEVICE_DETACH_REQUEST;

/* USB Setup Packet (for control transfers) */
typedef struct _VUSB_SETUP_PACKET {
    uint8_t     bmRequestType;
    uint8_t     bRequest;
    uint16_t    wValue;
    uint16_t    wIndex;
    uint16_t    wLength;
} VUSB_SETUP_PACKET, *PVUSB_SETUP_PACKET;

/* URB (USB Request Block) Submit */
typedef struct _VUSB_URB_SUBMIT {
    VUSB_HEADER         Header;
    uint32_t            DeviceId;           /* Target device */
    uint32_t            UrbId;              /* Unique URB identifier */
    uint8_t             EndpointAddress;    /* Endpoint address */
    uint8_t             TransferType;       /* VUSB_TRANSFER_TYPE */
    uint8_t             Direction;          /* VUSB_DIRECTION */
    uint8_t             Reserved;
    uint32_t            TransferFlags;      /* Transfer flags */
    uint32_t            TransferBufferLength;
    uint32_t            Interval;           /* For interrupt/iso */
    VUSB_SETUP_PACKET   SetupPacket;        /* For control transfers */
    /* Followed by: uint8_t TransferBuffer[TransferBufferLength] for OUT transfers */
} VUSB_URB_SUBMIT, *PVUSB_URB_SUBMIT;

/* URB Completion */
typedef struct _VUSB_URB_COMPLETE {
    VUSB_HEADER Header;
    uint32_t    DeviceId;           /* Source device */
    uint32_t    UrbId;              /* Matching URB identifier */
    uint32_t    Status;             /* VUSB_STATUS */
    uint32_t    ActualLength;       /* Actual bytes transferred */
    uint32_t    ErrorCount;         /* For isochronous */
    /* Followed by: uint8_t TransferBuffer[ActualLength] for IN transfers */
} VUSB_URB_COMPLETE, *PVUSB_URB_COMPLETE;

/* Cancel URB Request */
typedef struct _VUSB_URB_CANCEL {
    VUSB_HEADER Header;
    uint32_t    DeviceId;           /* Target device */
    uint32_t    UrbId;              /* URB to cancel */
} VUSB_URB_CANCEL;

/* Error Message */
typedef struct _VUSB_ERROR {
    VUSB_HEADER Header;
    uint32_t    ErrorCode;          /* Error code */
    uint32_t    OriginalCommand;    /* Command that caused error */
    uint32_t    OriginalSequence;   /* Original sequence number */
    char        ErrorMessage[256];  /* Human-readable error message */
} VUSB_ERROR;

/* Device List Request */
typedef struct _VUSB_DEVICE_LIST_REQUEST {
    VUSB_HEADER Header;
    /* No additional fields */
} VUSB_DEVICE_LIST_REQUEST;

/* Device List Response */
typedef struct _VUSB_DEVICE_LIST_RESPONSE {
    VUSB_HEADER Header;
    uint32_t    Status;             /* VUSB_STATUS */
    uint32_t    DeviceCount;        /* Number of devices */
    /* Followed by: VUSB_DEVICE_INFO Devices[DeviceCount] */
} VUSB_DEVICE_LIST_RESPONSE;

#pragma pack(pop)

/* Helper macros */
#define VUSB_HEADER_SIZE            sizeof(VUSB_HEADER)
#define VUSB_MAKE_ENDPOINT(num, dir) (((dir) << 7) | ((num) & 0x0F))
#define VUSB_ENDPOINT_NUMBER(ep)    ((ep) & 0x0F)
#define VUSB_ENDPOINT_DIRECTION(ep) (((ep) >> 7) & 0x01)

/* Initialize a protocol header */
static inline void VusbInitHeader(VUSB_HEADER* header, uint16_t command, 
                                   uint32_t length, uint32_t sequence) {
    header->Magic = VUSB_PROTOCOL_MAGIC;
    header->Version = VUSB_PROTOCOL_VERSION;
    header->Command = command;
    header->Length = length;
    header->Sequence = sequence;
}

/* Validate a protocol header */
static inline int VusbValidateHeader(const VUSB_HEADER* header) {
    return (header->Magic == VUSB_PROTOCOL_MAGIC &&
            header->Version == VUSB_PROTOCOL_VERSION);
}

#ifdef __cplusplus
}
#endif

#endif /* VUSB_PROTOCOL_H */
