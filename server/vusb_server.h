/**
 * Virtual USB Server Header
 */

#ifndef VUSB_SERVER_H
#define VUSB_SERVER_H

#include <winsock2.h>
#include <windows.h>
#include "../protocol/vusb_protocol.h"

#define VUSB_SERVER_MAX_CLIENTS 32

/* Forward declarations */
typedef struct _VUSB_SERVER_CONTEXT VUSB_SERVER_CONTEXT, *PVUSB_SERVER_CONTEXT;
typedef struct _VUSB_CLIENT_CONNECTION VUSB_CLIENT_CONNECTION, *PVUSB_CLIENT_CONNECTION;

/* Server configuration */
typedef struct _VUSB_SERVER_CONFIG {
    USHORT  Port;
    int     MaxClients;
} VUSB_SERVER_CONFIG, *PVUSB_SERVER_CONFIG;

/* Device tracking for a client */
typedef struct _VUSB_CLIENT_DEVICE {
    BOOL    Active;
    ULONG   DeviceId;       /* Local device ID */
    ULONG   RemoteId;       /* Client's device ID */
} VUSB_CLIENT_DEVICE;

/* Client connection */
typedef struct _VUSB_CLIENT_CONNECTION {
    SOCKET                  Socket;
    HANDLE                  Thread;
    PVUSB_SERVER_CONTEXT    ServerContext;
    ULONG                   SessionId;
    BOOL                    Connected;
    struct sockaddr_in      Address;
    char                    AddressString[INET_ADDRSTRLEN];
    VUSB_CLIENT_DEVICE      Devices[VUSB_MAX_DEVICES];
} VUSB_CLIENT_CONNECTION, *PVUSB_CLIENT_CONNECTION;

/* Server context */
typedef struct _VUSB_SERVER_CONTEXT {
    VUSB_SERVER_CONFIG      Config;
    BOOL                    Running;
    SOCKET                  ListenSocket;
    HANDLE                  DriverHandle;
    
    /* Client management */
    CRITICAL_SECTION        ClientLock;
    int                     ClientCount;
    ULONG                   NextSessionId;
    PVUSB_CLIENT_CONNECTION* Clients;
} VUSB_SERVER_CONTEXT, *PVUSB_SERVER_CONTEXT;

/* Server functions */
int VusbServerInit(PVUSB_SERVER_CONTEXT ctx, PVUSB_SERVER_CONFIG config);
int VusbServerOpenDriver(PVUSB_SERVER_CONTEXT ctx);
int VusbServerRun(PVUSB_SERVER_CONTEXT ctx);
void VusbServerCleanup(PVUSB_SERVER_CONTEXT ctx);

/* Client management */
PVUSB_CLIENT_CONNECTION VusbServerAcceptClient(
    PVUSB_SERVER_CONTEXT ctx,
    SOCKET socket,
    struct sockaddr_in* addr);
void VusbServerDisconnectClient(PVUSB_SERVER_CONTEXT ctx, PVUSB_CLIENT_CONNECTION client);
DWORD WINAPI VusbClientThread(LPVOID param);

/* Message processing */
void VusbServerProcessMessage(
    PVUSB_SERVER_CONTEXT ctx,
    PVUSB_CLIENT_CONNECTION client,
    PVUSB_HEADER header,
    PUCHAR payload,
    ULONG payloadLength);

/* Command handlers */
void VusbServerHandleConnect(
    PVUSB_SERVER_CONTEXT ctx,
    PVUSB_CLIENT_CONNECTION client,
    PVUSB_HEADER header,
    PUCHAR payload,
    ULONG payloadLength);

void VusbServerHandleDeviceAttach(
    PVUSB_SERVER_CONTEXT ctx,
    PVUSB_CLIENT_CONNECTION client,
    PVUSB_HEADER header,
    PUCHAR payload,
    ULONG payloadLength);

void VusbServerHandleDeviceDetach(
    PVUSB_SERVER_CONTEXT ctx,
    PVUSB_CLIENT_CONNECTION client,
    PVUSB_HEADER header,
    PUCHAR payload,
    ULONG payloadLength);

void VusbServerHandleUrbComplete(
    PVUSB_SERVER_CONTEXT ctx,
    PVUSB_CLIENT_CONNECTION client,
    PVUSB_HEADER header,
    PUCHAR payload,
    ULONG payloadLength);

void VusbServerHandleDeviceList(
    PVUSB_SERVER_CONTEXT ctx,
    PVUSB_CLIENT_CONNECTION client,
    PVUSB_HEADER header);

/* Driver interface */
int VusbServerPluginDevice(
    PVUSB_SERVER_CONTEXT ctx,
    PVUSB_DEVICE_INFO deviceInfo,
    PUCHAR descriptors,
    ULONG descriptorLength,
    PULONG deviceId);

int VusbServerUnplugDevice(PVUSB_SERVER_CONTEXT ctx, ULONG deviceId);

/* Utility */
void VusbServerSendPong(PVUSB_CLIENT_CONNECTION client, ULONG sequence);
void VusbServerSendError(PVUSB_CLIENT_CONNECTION client, ULONG sequence, 
                         ULONG errorCode, const char* message);

#endif /* VUSB_SERVER_H */
