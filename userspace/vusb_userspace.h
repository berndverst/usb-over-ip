/**
 * Virtual USB Userspace Driver Header
 * 
 * Provides a userspace implementation of virtual USB device emulation
 * without requiring a kernel driver. This combines server functionality
 * with simulated device presentation using Windows APIs.
 * 
 * Note: Full USB device presentation to the system still requires kernel
 * support. This implementation provides:
 * - Full protocol handling
 * - Device simulation for testing
 * - USB data capture and forwarding
 * - Application-level USB gadget emulation
 */

#ifndef VUSB_USERSPACE_H
#define VUSB_USERSPACE_H

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdint.h>
#include "../protocol/vusb_protocol.h"
#include "../protocol/vusb_ioctl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Configuration */
#define VUSB_US_MAX_DEVICES         16
#define VUSB_US_MAX_CLIENTS         32
#define VUSB_US_MAX_ENDPOINTS       32
#define VUSB_US_MAX_PENDING_URBS    256
#define VUSB_US_URB_BUFFER_SIZE     65536

/* Endpoint state */
typedef enum _VUSB_US_EP_STATE {
    VUSB_US_EP_DISABLED = 0,
    VUSB_US_EP_ENABLED,
    VUSB_US_EP_STALLED,
    VUSB_US_EP_HALTED,
} VUSB_US_EP_STATE;

/* Userspace endpoint */
typedef struct _VUSB_US_ENDPOINT {
    uint8_t             Address;
    uint8_t             Attributes;         /* Transfer type + sync + usage */
    uint16_t            MaxPacketSize;
    uint8_t             Interval;
    VUSB_US_EP_STATE    State;
    
    /* Data buffers for endpoint */
    CRITICAL_SECTION    Lock;
    uint8_t*            Buffer;
    uint32_t            BufferSize;
    uint32_t            DataLength;
    uint32_t            DataOffset;
    
    /* For interrupt endpoints */
    HANDLE              DataEvent;
} VUSB_US_ENDPOINT, *PVUSB_US_ENDPOINT;

/* Userspace device state */
typedef enum _VUSB_US_DEV_STATE {
    VUSB_US_DEV_DETACHED = 0,
    VUSB_US_DEV_ATTACHED,
    VUSB_US_DEV_POWERED,
    VUSB_US_DEV_DEFAULT,
    VUSB_US_DEV_ADDRESSED,
    VUSB_US_DEV_CONFIGURED,
    VUSB_US_DEV_SUSPENDED,
} VUSB_US_DEV_STATE;

/* Pending URB in userspace */
typedef struct _VUSB_US_PENDING_URB {
    struct _VUSB_US_PENDING_URB* Next;
    uint32_t            UrbId;
    uint32_t            Sequence;
    uint8_t             EndpointAddress;
    uint8_t             TransferType;
    uint8_t             Direction;
    uint32_t            TransferFlags;
    uint32_t            TransferBufferLength;
    uint32_t            Interval;
    VUSB_SETUP_PACKET   SetupPacket;
    uint8_t*            TransferBuffer;
    uint32_t            ActualLength;
    uint32_t            Status;
    BOOL                Completed;
    HANDLE              CompletionEvent;
    uint64_t            SubmitTime;
    
    /* Callback for completion */
    void*               CallbackContext;
    void                (*CompletionCallback)(struct _VUSB_US_PENDING_URB*, void*);
} VUSB_US_PENDING_URB, *PVUSB_US_PENDING_URB;

/* Userspace virtual device */
typedef struct _VUSB_US_DEVICE {
    BOOL                Active;
    uint32_t            DeviceId;
    uint32_t            RemoteDeviceId;     /* ID on the client side */
    VUSB_US_DEV_STATE   State;
    
    /* Device info */
    VUSB_DEVICE_INFO    DeviceInfo;
    
    /* Full descriptors */
    uint8_t*            Descriptors;
    uint32_t            DescriptorLength;
    
    /* Current configuration */
    uint8_t             Configuration;
    uint8_t             Address;
    
    /* Endpoints */
    VUSB_US_ENDPOINT    Endpoints[VUSB_US_MAX_ENDPOINTS];
    int                 NumEndpoints;
    
    /* Pending URBs */
    CRITICAL_SECTION    UrbLock;
    PVUSB_US_PENDING_URB PendingUrbs;
    uint32_t            PendingUrbCount;
    uint32_t            NextUrbId;
    
    /* Client connection owning this device */
    void*               OwnerClient;
    
    /* Statistics */
    uint64_t            BytesIn;
    uint64_t            BytesOut;
    uint64_t            UrbsSubmitted;
    uint64_t            UrbsCompleted;
} VUSB_US_DEVICE, *PVUSB_US_DEVICE;

/* Forward declarations */
typedef struct _VUSB_US_CLIENT VUSB_US_CLIENT, *PVUSB_US_CLIENT;
typedef struct _VUSB_US_CONTEXT VUSB_US_CONTEXT, *PVUSB_US_CONTEXT;

/* Client connection */
typedef struct _VUSB_US_CLIENT {
    SOCKET              Socket;
    HANDLE              Thread;
    PVUSB_US_CONTEXT    Context;
    uint32_t            SessionId;
    BOOL                Connected;
    BOOL                Authenticated;
    struct sockaddr_in  Address;
    char                AddressString[INET_ADDRSTRLEN];
    char                ClientName[64];
    uint32_t            ClientVersion;
    uint32_t            Capabilities;
    
    /* Devices owned by this client */
    uint32_t            DeviceIds[VUSB_US_MAX_DEVICES];
    int                 DeviceCount;
} VUSB_US_CLIENT;

/* Userspace server configuration */
typedef struct _VUSB_US_CONFIG {
    uint16_t    Port;
    int         MaxClients;
    int         MaxDevices;
    BOOL        EnableSimulation;   /* Allow simulated device responses */
    BOOL        EnableLogging;      /* Verbose logging */
    BOOL        EnableCapture;      /* Capture USB traffic to file */
    char        CaptureFile[MAX_PATH];
} VUSB_US_CONFIG, *PVUSB_US_CONFIG;

/* USB traffic capture entry */
typedef struct _VUSB_US_CAPTURE_ENTRY {
    uint64_t    Timestamp;
    uint32_t    DeviceId;
    uint8_t     Direction;          /* 0=OUT, 1=IN */
    uint8_t     TransferType;
    uint8_t     Endpoint;
    uint8_t     Reserved;
    uint32_t    Status;
    uint32_t    DataLength;
    /* Followed by data */
} VUSB_US_CAPTURE_ENTRY;

/* Gadget function callback interface */
typedef struct _VUSB_US_GADGET_OPS {
    /* Called when a setup packet is received */
    int (*HandleSetup)(PVUSB_US_DEVICE device, PVUSB_SETUP_PACKET setup,
                       uint8_t* buffer, uint32_t* length);
    
    /* Called when data is received on an OUT endpoint */
    int (*HandleDataOut)(PVUSB_US_DEVICE device, uint8_t endpoint,
                         uint8_t* data, uint32_t length);
    
    /* Called when data is requested on an IN endpoint */
    int (*HandleDataIn)(PVUSB_US_DEVICE device, uint8_t endpoint,
                        uint8_t* buffer, uint32_t* length);
    
    /* Called when device is reset */
    void (*HandleReset)(PVUSB_US_DEVICE device);
    
    /* Called when configuration changes */
    void (*HandleSetConfiguration)(PVUSB_US_DEVICE device, uint8_t config);
    
    /* Called when interface alt setting changes */
    void (*HandleSetInterface)(PVUSB_US_DEVICE device, uint8_t interface, uint8_t alt);
    
    /* User context */
    void* Context;
} VUSB_US_GADGET_OPS, *PVUSB_US_GADGET_OPS;

/* Main userspace context */
typedef struct _VUSB_US_CONTEXT {
    VUSB_US_CONFIG      Config;
    BOOL                Running;
    BOOL                Initialized;
    
    /* Network */
    SOCKET              ListenSocket;
    
    /* Client management */
    CRITICAL_SECTION    ClientLock;
    PVUSB_US_CLIENT     Clients[VUSB_US_MAX_CLIENTS];
    int                 ClientCount;
    uint32_t            NextSessionId;
    
    /* Device management */
    CRITICAL_SECTION    DeviceLock;
    VUSB_US_DEVICE      Devices[VUSB_US_MAX_DEVICES];
    uint32_t            NextDeviceId;
    
    /* Optional gadget operations for custom device emulation */
    PVUSB_US_GADGET_OPS GadgetOps;
    
    /* Capture */
    HANDLE              CaptureFile;
    CRITICAL_SECTION    CaptureLock;
    
    /* Statistics */
    uint64_t            TotalUrbsProcessed;
    uint64_t            TotalBytesTransferred;
    uint64_t            StartTime;
    
    /* Event for shutdown signaling */
    HANDLE              ShutdownEvent;
} VUSB_US_CONTEXT;

/* ============================================================
 * Core API Functions
 * ============================================================ */

/**
 * VusbUsInit - Initialize userspace server context
 * @ctx: Context to initialize
 * @config: Server configuration
 * @return: 0 on success, negative on error
 */
int VusbUsInit(PVUSB_US_CONTEXT ctx, PVUSB_US_CONFIG config);

/**
 * VusbUsCleanup - Cleanup and free resources
 * @ctx: Context to cleanup
 */
void VusbUsCleanup(PVUSB_US_CONTEXT ctx);

/**
 * VusbUsRun - Run the main server loop (blocking)
 * @ctx: Server context
 * @return: 0 on clean shutdown, negative on error
 */
int VusbUsRun(PVUSB_US_CONTEXT ctx);

/**
 * VusbUsStop - Signal server to stop
 * @ctx: Server context
 */
void VusbUsStop(PVUSB_US_CONTEXT ctx);

/* ============================================================
 * Device Management
 * ============================================================ */

/**
 * VusbUsCreateDevice - Create a local virtual device
 * @ctx: Server context
 * @deviceInfo: Device information
 * @descriptors: USB descriptors
 * @descriptorLength: Length of descriptors
 * @deviceId: Output device ID
 * @return: 0 on success
 */
int VusbUsCreateDevice(PVUSB_US_CONTEXT ctx, PVUSB_DEVICE_INFO deviceInfo,
                       uint8_t* descriptors, uint32_t descriptorLength,
                       uint32_t* deviceId);

/**
 * VusbUsDestroyDevice - Remove a virtual device
 * @ctx: Server context
 * @deviceId: Device to remove
 * @return: 0 on success
 */
int VusbUsDestroyDevice(PVUSB_US_CONTEXT ctx, uint32_t deviceId);

/**
 * VusbUsGetDevice - Get device pointer by ID
 * @ctx: Server context
 * @deviceId: Device ID
 * @return: Device pointer or NULL
 */
PVUSB_US_DEVICE VusbUsGetDevice(PVUSB_US_CONTEXT ctx, uint32_t deviceId);

/* ============================================================
 * URB Processing
 * ============================================================ */

/**
 * VusbUsSubmitUrb - Submit a URB to a device
 * @ctx: Server context
 * @deviceId: Target device
 * @urb: URB to submit
 * @return: 0 on success
 */
int VusbUsSubmitUrb(PVUSB_US_CONTEXT ctx, uint32_t deviceId, 
                    PVUSB_US_PENDING_URB urb);

/**
 * VusbUsCompleteUrb - Complete a pending URB
 * @ctx: Server context
 * @deviceId: Device ID
 * @urbId: URB to complete
 * @status: Completion status
 * @data: Response data (for IN transfers)
 * @length: Response length
 * @return: 0 on success
 */
int VusbUsCompleteUrb(PVUSB_US_CONTEXT ctx, uint32_t deviceId, 
                      uint32_t urbId, uint32_t status,
                      uint8_t* data, uint32_t length);

/**
 * VusbUsCancelUrb - Cancel a pending URB
 * @ctx: Server context
 * @deviceId: Device ID
 * @urbId: URB to cancel
 * @return: 0 on success
 */
int VusbUsCancelUrb(PVUSB_US_CONTEXT ctx, uint32_t deviceId, uint32_t urbId);

/* ============================================================
 * Gadget Mode
 * ============================================================ */

/**
 * VusbUsSetGadgetOps - Set gadget operation callbacks
 * @ctx: Server context
 * @ops: Gadget operations
 */
void VusbUsSetGadgetOps(PVUSB_US_CONTEXT ctx, PVUSB_US_GADGET_OPS ops);

/**
 * VusbUsEpWrite - Write data to an IN endpoint buffer
 * @device: Device
 * @endpoint: Endpoint address
 * @data: Data to write
 * @length: Data length
 * @return: Bytes written or negative on error
 */
int VusbUsEpWrite(PVUSB_US_DEVICE device, uint8_t endpoint,
                  uint8_t* data, uint32_t length);

/**
 * VusbUsEpRead - Read data from an OUT endpoint buffer
 * @device: Device
 * @endpoint: Endpoint address
 * @buffer: Buffer for data
 * @maxLength: Maximum bytes to read
 * @return: Bytes read or negative on error
 */
int VusbUsEpRead(PVUSB_US_DEVICE device, uint8_t endpoint,
                 uint8_t* buffer, uint32_t maxLength);

/**
 * VusbUsEpStall - Stall an endpoint
 * @device: Device
 * @endpoint: Endpoint address
 */
void VusbUsEpStall(PVUSB_US_DEVICE device, uint8_t endpoint);

/**
 * VusbUsEpUnstall - Clear stall on an endpoint
 * @device: Device
 * @endpoint: Endpoint address
 */
void VusbUsEpUnstall(PVUSB_US_DEVICE device, uint8_t endpoint);

/* ============================================================
 * Capture Functions
 * ============================================================ */

/**
 * VusbUsStartCapture - Start capturing USB traffic
 * @ctx: Server context
 * @filename: Output file path
 * @return: 0 on success
 */
int VusbUsStartCapture(PVUSB_US_CONTEXT ctx, const char* filename);

/**
 * VusbUsStopCapture - Stop capturing
 * @ctx: Server context
 */
void VusbUsStopCapture(PVUSB_US_CONTEXT ctx);

/* ============================================================
 * Utility Functions
 * ============================================================ */

/**
 * VusbUsGetStats - Get server statistics
 * @ctx: Server context
 * @stats: Output statistics structure
 */
void VusbUsGetStats(PVUSB_US_CONTEXT ctx, VUSB_STATISTICS* stats);

/**
 * VusbUsListDevices - List connected devices
 * @ctx: Server context
 * @list: Output device list
 * @maxDevices: Maximum devices to return
 * @return: Number of devices
 */
int VusbUsListDevices(PVUSB_US_CONTEXT ctx, VUSB_DEVICE_INFO* list, int maxDevices);

/**
 * VusbUsListClients - List connected clients
 * @ctx: Server context
 * @callback: Callback for each client
 * @userdata: User data for callback
 */
void VusbUsListClients(PVUSB_US_CONTEXT ctx, 
                       void (*callback)(PVUSB_US_CLIENT, void*),
                       void* userdata);

#ifdef __cplusplus
}
#endif

#endif /* VUSB_USERSPACE_H */
