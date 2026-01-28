/**
 * Virtual USB Server Application
 * 
 * Main server that handles network connections from remote clients
 * and interfaces with the kernel-mode virtual USB driver.
 */

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vusb_server.h"
#include "../protocol/vusb_protocol.h"
#include "../protocol/vusb_ioctl.h"

#pragma comment(lib, "ws2_32.lib")

/* Global server context */
static VUSB_SERVER_CONTEXT g_ServerContext = {0};

/**
 * main - Server entry point
 */
int main(int argc, char* argv[])
{
    VUSB_SERVER_CONFIG config = {0};
    int result;

    printf("Virtual USB Server v1.0\n");
    printf("========================\n\n");

    /* Parse command line arguments */
    config.Port = VUSB_DEFAULT_PORT;
    config.MaxClients = VUSB_SERVER_MAX_CLIENTS;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            config.Port = (USHORT)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--max-clients") == 0 && i + 1 < argc) {
            config.MaxClients = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: vusb_server [options]\n");
            printf("Options:\n");
            printf("  --port <port>         Listen port (default: %d)\n", VUSB_DEFAULT_PORT);
            printf("  --max-clients <num>   Maximum clients (default: %d)\n", VUSB_SERVER_MAX_CLIENTS);
            printf("  --help, -h            Show this help\n");
            return 0;
        }
    }

    printf("Configuration:\n");
    printf("  Port: %d\n", config.Port);
    printf("  Max clients: %d\n\n", config.MaxClients);

    /* Initialize server */
    result = VusbServerInit(&g_ServerContext, &config);
    if (result != 0) {
        fprintf(stderr, "Failed to initialize server: %d\n", result);
        return 1;
    }

    /* Open driver handle */
    result = VusbServerOpenDriver(&g_ServerContext);
    if (result != 0) {
        fprintf(stderr, "Failed to open driver (is it installed?): %d\n", result);
        fprintf(stderr, "Server will run in simulation mode.\n\n");
    }

    /* Start server */
    result = VusbServerRun(&g_ServerContext);

    /* Cleanup */
    VusbServerCleanup(&g_ServerContext);

    return result;
}

/**
 * VusbServerInit - Initialize server
 */
int VusbServerInit(PVUSB_SERVER_CONTEXT ctx, PVUSB_SERVER_CONFIG config)
{
    WSADATA wsaData;
    int result;

    memset(ctx, 0, sizeof(VUSB_SERVER_CONTEXT));
    ctx->Config = *config;
    ctx->Running = FALSE;
    ctx->DriverHandle = INVALID_HANDLE_VALUE;

    /* Initialize Winsock */
    result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        fprintf(stderr, "WSAStartup failed: %d\n", result);
        return result;
    }

    /* Initialize critical section */
    InitializeCriticalSection(&ctx->ClientLock);

    /* Allocate client array */
    ctx->Clients = (PVUSB_CLIENT_CONNECTION*)calloc(
        config->MaxClients, sizeof(PVUSB_CLIENT_CONNECTION));
    if (!ctx->Clients) {
        return -1;
    }

    printf("Server initialized.\n");
    return 0;
}

/**
 * VusbServerOpenDriver - Open handle to the virtual USB driver
 */
int VusbServerOpenDriver(PVUSB_SERVER_CONTEXT ctx)
{
    ctx->DriverHandle = CreateFileW(
        L"\\\\.\\VirtualUSB",
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED,
        NULL
    );

    if (ctx->DriverHandle == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        fprintf(stderr, "Failed to open driver: error %lu\n", error);
        return (int)error;
    }

    /* Query driver version */
    VUSB_VERSION_INFO versionInfo;
    DWORD bytesReturned;

    if (DeviceIoControl(ctx->DriverHandle, IOCTL_VUSB_GET_VERSION,
                        NULL, 0, &versionInfo, sizeof(versionInfo),
                        &bytesReturned, NULL)) {
        printf("Driver version: %u.%u\n", 
               (versionInfo.DriverVersion >> 16) & 0xFFFF,
               versionInfo.DriverVersion & 0xFFFF);
        printf("Max devices: %u\n", versionInfo.MaxDevices);
    }

    return 0;
}

/**
 * VusbServerRun - Main server loop
 */
int VusbServerRun(PVUSB_SERVER_CONTEXT ctx)
{
    SOCKET listenSocket = INVALID_SOCKET;
    struct sockaddr_in serverAddr;
    int result;

    /* Create listening socket */
    listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == INVALID_SOCKET) {
        fprintf(stderr, "socket() failed: %d\n", WSAGetLastError());
        return -1;
    }

    /* Allow address reuse */
    int optval = 1;
    setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, (char*)&optval, sizeof(optval));

    /* Bind to port */
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(ctx->Config.Port);

    result = bind(listenSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr));
    if (result == SOCKET_ERROR) {
        fprintf(stderr, "bind() failed: %d\n", WSAGetLastError());
        closesocket(listenSocket);
        return -1;
    }

    /* Start listening */
    result = listen(listenSocket, SOMAXCONN);
    if (result == SOCKET_ERROR) {
        fprintf(stderr, "listen() failed: %d\n", WSAGetLastError());
        closesocket(listenSocket);
        return -1;
    }

    ctx->ListenSocket = listenSocket;
    ctx->Running = TRUE;

    printf("\nServer listening on port %d...\n", ctx->Config.Port);
    printf("Press Ctrl+C to stop.\n\n");

    /* Accept connections */
    while (ctx->Running) {
        struct sockaddr_in clientAddr;
        int clientAddrLen = sizeof(clientAddr);
        SOCKET clientSocket;

        clientSocket = accept(listenSocket, (struct sockaddr*)&clientAddr, &clientAddrLen);
        if (clientSocket == INVALID_SOCKET) {
            if (ctx->Running) {
                fprintf(stderr, "accept() failed: %d\n", WSAGetLastError());
            }
            continue;
        }

        char clientIP[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddr.sin_addr, clientIP, sizeof(clientIP));
        printf("New connection from %s:%d\n", clientIP, ntohs(clientAddr.sin_port));

        /* Handle client in new thread */
        PVUSB_CLIENT_CONNECTION client = VusbServerAcceptClient(ctx, clientSocket, &clientAddr);
        if (client) {
            HANDLE thread = CreateThread(NULL, 0, VusbClientThread, client, 0, NULL);
            if (thread) {
                client->Thread = thread;
            } else {
                VusbServerDisconnectClient(ctx, client);
            }
        }
    }

    closesocket(listenSocket);
    return 0;
}

/**
 * VusbServerAcceptClient - Accept a new client connection
 */
PVUSB_CLIENT_CONNECTION VusbServerAcceptClient(
    PVUSB_SERVER_CONTEXT ctx,
    SOCKET socket,
    struct sockaddr_in* addr)
{
    PVUSB_CLIENT_CONNECTION client = NULL;

    EnterCriticalSection(&ctx->ClientLock);

    /* Find free slot */
    for (int i = 0; i < ctx->Config.MaxClients; i++) {
        if (ctx->Clients[i] == NULL) {
            client = (PVUSB_CLIENT_CONNECTION)calloc(1, sizeof(VUSB_CLIENT_CONNECTION));
            if (client) {
                client->Socket = socket;
                client->ServerContext = ctx;
                client->SessionId = ++ctx->NextSessionId;
                client->Connected = TRUE;
                memcpy(&client->Address, addr, sizeof(*addr));
                inet_ntop(AF_INET, &addr->sin_addr, client->AddressString, sizeof(client->AddressString));

                ctx->Clients[i] = client;
                ctx->ClientCount++;
            }
            break;
        }
    }

    LeaveCriticalSection(&ctx->ClientLock);

    if (!client) {
        fprintf(stderr, "Server full, rejecting connection\n");
        closesocket(socket);
    }

    return client;
}

/**
 * VusbServerDisconnectClient - Disconnect and cleanup a client
 */
void VusbServerDisconnectClient(PVUSB_SERVER_CONTEXT ctx, PVUSB_CLIENT_CONNECTION client)
{
    if (!client) return;

    EnterCriticalSection(&ctx->ClientLock);

    /* Remove from client array */
    for (int i = 0; i < ctx->Config.MaxClients; i++) {
        if (ctx->Clients[i] == client) {
            ctx->Clients[i] = NULL;
            ctx->ClientCount--;
            break;
        }
    }

    LeaveCriticalSection(&ctx->ClientLock);

    /* Unplug all devices owned by this client */
    for (int i = 0; i < VUSB_MAX_DEVICES; i++) {
        if (client->Devices[i].Active) {
            VusbServerUnplugDevice(ctx, client->Devices[i].DeviceId);
        }
    }

    /* Close socket */
    if (client->Socket != INVALID_SOCKET) {
        closesocket(client->Socket);
        client->Socket = INVALID_SOCKET;
    }

    printf("Client %s disconnected (session %u)\n", 
           client->AddressString, client->SessionId);

    free(client);
}

/**
 * VusbClientThread - Client handler thread
 */
DWORD WINAPI VusbClientThread(LPVOID param)
{
    PVUSB_CLIENT_CONNECTION client = (PVUSB_CLIENT_CONNECTION)param;
    PVUSB_SERVER_CONTEXT ctx = client->ServerContext;
    VUSB_HEADER header;
    PUCHAR buffer = NULL;
    int result;

    printf("Client thread started for session %u\n", client->SessionId);

    /* Allocate receive buffer */
    buffer = (PUCHAR)malloc(VUSB_MAX_PACKET_SIZE);
    if (!buffer) {
        goto cleanup;
    }

    /* Main receive loop */
    while (client->Connected && ctx->Running) {
        /* Receive header */
        result = recv(client->Socket, (char*)&header, sizeof(header), MSG_WAITALL);
        if (result != sizeof(header)) {
            if (result == 0) {
                printf("Client %s closed connection\n", client->AddressString);
            } else {
                fprintf(stderr, "recv() failed: %d\n", WSAGetLastError());
            }
            break;
        }

        /* Validate header */
        if (!VusbValidateHeader(&header)) {
            fprintf(stderr, "Invalid protocol header from %s\n", client->AddressString);
            break;
        }

        /* Receive payload if present */
        if (header.Length > 0) {
            if (header.Length > VUSB_MAX_PACKET_SIZE - sizeof(header)) {
                fprintf(stderr, "Payload too large: %u\n", header.Length);
                break;
            }

            result = recv(client->Socket, (char*)buffer, header.Length, MSG_WAITALL);
            if (result != (int)header.Length) {
                fprintf(stderr, "Failed to receive payload\n");
                break;
            }
        }

        /* Process message */
        VusbServerProcessMessage(ctx, client, &header, buffer, header.Length);
    }

cleanup:
    client->Connected = FALSE;

    if (buffer) {
        free(buffer);
    }

    VusbServerDisconnectClient(ctx, client);
    return 0;
}

/**
 * VusbServerProcessMessage - Process a received message
 */
void VusbServerProcessMessage(
    PVUSB_SERVER_CONTEXT ctx,
    PVUSB_CLIENT_CONNECTION client,
    PVUSB_HEADER header,
    PUCHAR payload,
    ULONG payloadLength)
{
    switch (header->Command) {
    case VUSB_CMD_CONNECT:
        VusbServerHandleConnect(ctx, client, header, payload, payloadLength);
        break;

    case VUSB_CMD_DISCONNECT:
        client->Connected = FALSE;
        break;

    case VUSB_CMD_PING:
        VusbServerSendPong(client, header->Sequence);
        break;

    case VUSB_CMD_DEVICE_ATTACH:
        VusbServerHandleDeviceAttach(ctx, client, header, payload, payloadLength);
        break;

    case VUSB_CMD_DEVICE_DETACH:
        VusbServerHandleDeviceDetach(ctx, client, header, payload, payloadLength);
        break;

    case VUSB_CMD_URB_COMPLETE:
        VusbServerHandleUrbComplete(ctx, client, header, payload, payloadLength);
        break;

    case VUSB_CMD_DEVICE_LIST:
        VusbServerHandleDeviceList(ctx, client, header);
        break;

    default:
        fprintf(stderr, "Unknown command: 0x%04X\n", header->Command);
        VusbServerSendError(client, header->Sequence, VUSB_STATUS_NOT_SUPPORTED, 
                           "Unknown command");
        break;
    }
}

/**
 * VusbServerHandleConnect - Handle client connect request
 */
void VusbServerHandleConnect(
    PVUSB_SERVER_CONTEXT ctx,
    PVUSB_CLIENT_CONNECTION client,
    PVUSB_HEADER header,
    PUCHAR payload,
    ULONG payloadLength)
{
    VUSB_CONNECT_RESPONSE response;

    UNREFERENCED_PARAMETER(ctx);
    UNREFERENCED_PARAMETER(payload);
    UNREFERENCED_PARAMETER(payloadLength);

    printf("Client %s connecting...\n", client->AddressString);

    /* Build response */
    VusbInitHeader(&response.Header, VUSB_CMD_CONNECT, 
                   sizeof(response) - sizeof(VUSB_HEADER), header->Sequence);
    response.Status = VUSB_STATUS_SUCCESS;
    response.ServerVersion = 0x00010000;
    response.Capabilities = 0;
    response.SessionId = client->SessionId;

    /* Send response */
    send(client->Socket, (char*)&response, sizeof(response), 0);

    printf("Client %s connected (session %u)\n", client->AddressString, client->SessionId);
}

/**
 * VusbServerHandleDeviceAttach - Handle device attach request
 */
void VusbServerHandleDeviceAttach(
    PVUSB_SERVER_CONTEXT ctx,
    PVUSB_CLIENT_CONNECTION client,
    PVUSB_HEADER header,
    PUCHAR payload,
    ULONG payloadLength)
{
    VUSB_DEVICE_ATTACH_RESPONSE response;
    VUSB_DEVICE_INFO* deviceInfo;
    ULONG descriptorLength;
    PUCHAR descriptors;
    ULONG deviceId = 0;
    int result;

    if (payloadLength < sizeof(VUSB_DEVICE_INFO) + sizeof(ULONG)) {
        VusbServerSendError(client, header->Sequence, VUSB_STATUS_INVALID_PARAM,
                           "Invalid attach request");
        return;
    }

    deviceInfo = (VUSB_DEVICE_INFO*)payload;
    descriptorLength = *(ULONG*)(payload + sizeof(VUSB_DEVICE_INFO));
    descriptors = payload + sizeof(VUSB_DEVICE_INFO) + sizeof(ULONG);

    printf("Device attach: VID=%04X PID=%04X (%s - %s)\n",
           deviceInfo->VendorId, deviceInfo->ProductId,
           deviceInfo->Manufacturer, deviceInfo->Product);

    /* Plugin device via driver */
    result = VusbServerPluginDevice(ctx, deviceInfo, descriptors, descriptorLength, &deviceId);

    /* Track device for this client */
    if (result == 0 && deviceId > 0) {
        for (int i = 0; i < VUSB_MAX_DEVICES; i++) {
            if (!client->Devices[i].Active) {
                client->Devices[i].Active = TRUE;
                client->Devices[i].DeviceId = deviceId;
                client->Devices[i].RemoteId = deviceInfo->DeviceId;
                break;
            }
        }
    }

    /* Build response */
    VusbInitHeader(&response.Header, VUSB_CMD_DEVICE_ATTACH,
                   sizeof(response) - sizeof(VUSB_HEADER), header->Sequence);
    response.Status = (result == 0) ? VUSB_STATUS_SUCCESS : VUSB_STATUS_ERROR;
    response.DeviceId = deviceId;

    send(client->Socket, (char*)&response, sizeof(response), 0);
}

/**
 * VusbServerHandleDeviceDetach - Handle device detach request
 */
void VusbServerHandleDeviceDetach(
    PVUSB_SERVER_CONTEXT ctx,
    PVUSB_CLIENT_CONNECTION client,
    PVUSB_HEADER header,
    PUCHAR payload,
    ULONG payloadLength)
{
    VUSB_HEADER response;
    ULONG deviceId;

    if (payloadLength < sizeof(ULONG)) {
        VusbServerSendError(client, header->Sequence, VUSB_STATUS_INVALID_PARAM,
                           "Invalid detach request");
        return;
    }

    deviceId = *(ULONG*)payload;

    printf("Device detach: ID=%u\n", deviceId);

    VusbServerUnplugDevice(ctx, deviceId);

    /* Remove from client tracking */
    for (int i = 0; i < VUSB_MAX_DEVICES; i++) {
        if (client->Devices[i].Active && client->Devices[i].DeviceId == deviceId) {
            client->Devices[i].Active = FALSE;
            break;
        }
    }

    /* Send acknowledgment */
    VusbInitHeader(&response, VUSB_CMD_DEVICE_DETACH, 0, header->Sequence);
    send(client->Socket, (char*)&response, sizeof(response), 0);
}

/**
 * VusbServerHandleUrbComplete - Handle URB completion from client
 */
void VusbServerHandleUrbComplete(
    PVUSB_SERVER_CONTEXT ctx,
    PVUSB_CLIENT_CONNECTION client,
    PVUSB_HEADER header,
    PUCHAR payload,
    ULONG payloadLength)
{
    VUSB_URB_COMPLETE* urbComplete;
    VUSB_URB_COMPLETION completion;
    DWORD bytesReturned;

    UNREFERENCED_PARAMETER(client);

    if (payloadLength < sizeof(VUSB_URB_COMPLETE) - sizeof(VUSB_HEADER)) {
        return;
    }

    urbComplete = (VUSB_URB_COMPLETE*)payload;

    /* Forward to driver */
    if (ctx->DriverHandle != INVALID_HANDLE_VALUE) {
        completion.DeviceId = urbComplete->DeviceId;
        completion.UrbId = urbComplete->UrbId;
        completion.SequenceNumber = header->Sequence;
        completion.Status = urbComplete->Status;
        completion.ActualLength = urbComplete->ActualLength;

        /* Build IOCTL input with data */
        size_t inputSize = sizeof(completion) + urbComplete->ActualLength;
        PUCHAR inputBuffer = (PUCHAR)malloc(inputSize);
        if (inputBuffer) {
            memcpy(inputBuffer, &completion, sizeof(completion));
            if (urbComplete->ActualLength > 0) {
                memcpy(inputBuffer + sizeof(completion), 
                       (PUCHAR)(urbComplete + 1), urbComplete->ActualLength);
            }

            DeviceIoControl(ctx->DriverHandle, IOCTL_VUSB_COMPLETE_URB,
                           inputBuffer, (DWORD)inputSize, NULL, 0, &bytesReturned, NULL);

            free(inputBuffer);
        }
    }
}

/**
 * VusbServerHandleDeviceList - Handle device list request
 */
void VusbServerHandleDeviceList(
    PVUSB_SERVER_CONTEXT ctx,
    PVUSB_CLIENT_CONNECTION client,
    PVUSB_HEADER header)
{
    VUSB_DEVICE_LIST deviceList;
    DWORD bytesReturned;
    VUSB_DEVICE_LIST_RESPONSE response;
    size_t responseSize;

    memset(&deviceList, 0, sizeof(deviceList));

    /* Query driver */
    if (ctx->DriverHandle != INVALID_HANDLE_VALUE) {
        DeviceIoControl(ctx->DriverHandle, IOCTL_VUSB_GET_DEVICE_LIST,
                       NULL, 0, &deviceList, sizeof(deviceList), &bytesReturned, NULL);
    }

    /* Build response */
    responseSize = sizeof(VUSB_HEADER) + sizeof(UINT32) * 2 + 
                   deviceList.DeviceCount * sizeof(VUSB_DEVICE_INFO);
    
    VusbInitHeader(&response.Header, VUSB_CMD_DEVICE_LIST,
                   (UINT32)(responseSize - sizeof(VUSB_HEADER)), header->Sequence);
    response.Status = VUSB_STATUS_SUCCESS;
    response.DeviceCount = deviceList.DeviceCount;

    /* Send header and count */
    send(client->Socket, (char*)&response, sizeof(response), 0);

    /* Send device info for each device */
    for (ULONG i = 0; i < deviceList.DeviceCount; i++) {
        send(client->Socket, (char*)&deviceList.Devices[i].DeviceInfo, 
             sizeof(VUSB_DEVICE_INFO), 0);
    }
}

/**
 * VusbServerPluginDevice - Plugin a device via driver IOCTL
 */
int VusbServerPluginDevice(
    PVUSB_SERVER_CONTEXT ctx,
    PVUSB_DEVICE_INFO deviceInfo,
    PUCHAR descriptors,
    ULONG descriptorLength,
    PULONG deviceId)
{
    VUSB_PLUGIN_RESPONSE response;
    DWORD bytesReturned;
    BOOL result;

    *deviceId = 0;

    if (ctx->DriverHandle == INVALID_HANDLE_VALUE) {
        /* Simulation mode - assign fake ID */
        static ULONG nextId = 1;
        *deviceId = nextId++;
        printf("[SIM] Plugged device ID %u\n", *deviceId);
        return 0;
    }

    /* Build plugin request */
    size_t requestSize = sizeof(VUSB_PLUGIN_REQUEST) + descriptorLength;
    PUCHAR requestBuffer = (PUCHAR)malloc(requestSize);
    if (!requestBuffer) {
        return -1;
    }

    PVUSB_PLUGIN_REQUEST request = (PVUSB_PLUGIN_REQUEST)requestBuffer;
    memcpy(&request->DeviceInfo, deviceInfo, sizeof(VUSB_DEVICE_INFO));
    request->DescriptorLength = descriptorLength;
    if (descriptorLength > 0) {
        memcpy(requestBuffer + sizeof(VUSB_PLUGIN_REQUEST), descriptors, descriptorLength);
    }

    result = DeviceIoControl(ctx->DriverHandle, IOCTL_VUSB_PLUGIN_DEVICE,
                            requestBuffer, (DWORD)requestSize,
                            &response, sizeof(response), &bytesReturned, NULL);

    free(requestBuffer);

    if (result && response.Status == VUSB_STATUS_SUCCESS) {
        *deviceId = response.DeviceId;
        return 0;
    }

    return -1;
}

/**
 * VusbServerUnplugDevice - Unplug a device via driver IOCTL
 */
int VusbServerUnplugDevice(PVUSB_SERVER_CONTEXT ctx, ULONG deviceId)
{
    VUSB_UNPLUG_REQUEST request;
    DWORD bytesReturned;

    if (ctx->DriverHandle == INVALID_HANDLE_VALUE) {
        printf("[SIM] Unplugged device ID %u\n", deviceId);
        return 0;
    }

    request.DeviceId = deviceId;

    DeviceIoControl(ctx->DriverHandle, IOCTL_VUSB_UNPLUG_DEVICE,
                   &request, sizeof(request), NULL, 0, &bytesReturned, NULL);

    return 0;
}

/**
 * VusbServerSendPong - Send pong response
 */
void VusbServerSendPong(PVUSB_CLIENT_CONNECTION client, ULONG sequence)
{
    VUSB_HEADER response;
    VusbInitHeader(&response, VUSB_CMD_PONG, 0, sequence);
    send(client->Socket, (char*)&response, sizeof(response), 0);
}

/**
 * VusbServerSendError - Send error response
 */
void VusbServerSendError(PVUSB_CLIENT_CONNECTION client, ULONG sequence, 
                         ULONG errorCode, const char* message)
{
    VUSB_ERROR response;
    VusbInitHeader(&response.Header, VUSB_CMD_ERROR, 
                   sizeof(response) - sizeof(VUSB_HEADER), sequence);
    response.ErrorCode = errorCode;
    response.OriginalCommand = 0;
    response.OriginalSequence = sequence;
    strncpy_s(response.ErrorMessage, sizeof(response.ErrorMessage), message, _TRUNCATE);

    send(client->Socket, (char*)&response, sizeof(response), 0);
}

/**
 * VusbServerCleanup - Cleanup server resources
 */
void VusbServerCleanup(PVUSB_SERVER_CONTEXT ctx)
{
    ctx->Running = FALSE;

    /* Close listen socket */
    if (ctx->ListenSocket != INVALID_SOCKET) {
        closesocket(ctx->ListenSocket);
        ctx->ListenSocket = INVALID_SOCKET;
    }

    /* Disconnect all clients */
    EnterCriticalSection(&ctx->ClientLock);
    for (int i = 0; i < ctx->Config.MaxClients; i++) {
        if (ctx->Clients[i]) {
            ctx->Clients[i]->Connected = FALSE;
            closesocket(ctx->Clients[i]->Socket);
        }
    }
    LeaveCriticalSection(&ctx->ClientLock);

    /* Wait for client threads to finish */
    Sleep(1000);

    /* Close driver handle */
    if (ctx->DriverHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(ctx->DriverHandle);
        ctx->DriverHandle = INVALID_HANDLE_VALUE;
    }

    /* Free client array */
    if (ctx->Clients) {
        free(ctx->Clients);
        ctx->Clients = NULL;
    }

    DeleteCriticalSection(&ctx->ClientLock);

    WSACleanup();

    printf("Server cleanup complete.\n");
}
