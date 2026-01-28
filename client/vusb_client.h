/**
 * Virtual USB Client Header
 */

#ifndef VUSB_CLIENT_H
#define VUSB_CLIENT_H

#include <stdint.h>
#include "../protocol/vusb_protocol.h"

#ifdef _WIN32
#include <winsock2.h>
typedef SOCKET socket_t;
#else
typedef int socket_t;
#endif

/* Client configuration */
typedef struct _VUSB_CLIENT_CONFIG {
    char        ServerAddress[256];
    uint16_t    ServerPort;
    char        ClientName[64];
} VUSB_CLIENT_CONFIG, *PVUSB_CLIENT_CONFIG;

/* Local device tracking */
typedef struct _VUSB_LOCAL_DEVICE {
    int         Active;
    uint32_t    LocalId;
    uint32_t    RemoteId;
    VUSB_DEVICE_INFO DeviceInfo;
} VUSB_LOCAL_DEVICE;

/* Client context */
typedef struct _VUSB_CLIENT_CONTEXT {
    VUSB_CLIENT_CONFIG  Config;
    socket_t            Socket;
    int                 Connected;
    uint32_t            SessionId;
    uint32_t            Sequence;
    uint32_t            NextDeviceId;
    VUSB_LOCAL_DEVICE   Devices[VUSB_MAX_DEVICES];
} VUSB_CLIENT_CONTEXT, *PVUSB_CLIENT_CONTEXT;

/* Client functions */
int VusbClientInit(PVUSB_CLIENT_CONTEXT ctx, PVUSB_CLIENT_CONFIG config);
int VusbClientConnect(PVUSB_CLIENT_CONTEXT ctx);
void VusbClientDisconnect(PVUSB_CLIENT_CONTEXT ctx);
void VusbClientCleanup(PVUSB_CLIENT_CONTEXT ctx);

/* Device operations */
int VusbClientAttachDevice(
    PVUSB_CLIENT_CONTEXT ctx,
    PVUSB_DEVICE_INFO deviceInfo,
    uint8_t* descriptors,
    uint32_t descriptorLength,
    uint32_t* remoteDeviceId);

int VusbClientDetachDevice(PVUSB_CLIENT_CONTEXT ctx, uint32_t remoteDeviceId);

/* Interactive mode */
int VusbClientRunInteractive(PVUSB_CLIENT_CONTEXT ctx);
int VusbClientAttachSimulatedDevice(PVUSB_CLIENT_CONTEXT ctx, uint16_t vid, uint16_t pid);
int VusbClientListDevices(PVUSB_CLIENT_CONTEXT ctx);
int VusbClientPing(PVUSB_CLIENT_CONTEXT ctx);

#endif /* VUSB_CLIENT_H */
