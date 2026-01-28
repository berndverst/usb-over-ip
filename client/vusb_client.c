/**
 * Virtual USB Client Application
 * 
 * Client application that runs on a remote device to capture real USB devices
 * and send them over the network to the Virtual USB Server.
 */

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#define SOCKET int
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket close
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "vusb_client.h"
#include "../protocol/vusb_protocol.h"

/* Global client context */
static VUSB_CLIENT_CONTEXT g_ClientContext = {0};

/**
 * main - Client entry point
 */
int main(int argc, char* argv[])
{
    VUSB_CLIENT_CONFIG config = {0};
    int result;

    printf("Virtual USB Client v1.0\n");
    printf("========================\n\n");

    /* Default configuration */
    strcpy(config.ServerAddress, "127.0.0.1");
    config.ServerPort = VUSB_DEFAULT_PORT;
    strcpy(config.ClientName, "VUSBClient");

    /* Parse command line arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--server") == 0 && i + 1 < argc) {
            strncpy(config.ServerAddress, argv[++i], sizeof(config.ServerAddress) - 1);
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            config.ServerPort = (uint16_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--name") == 0 && i + 1 < argc) {
            strncpy(config.ClientName, argv[++i], sizeof(config.ClientName) - 1);
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: vusb_client [options]\n");
            printf("Options:\n");
            printf("  --server <address>    Server address (default: 127.0.0.1)\n");
            printf("  --port <port>         Server port (default: %d)\n", VUSB_DEFAULT_PORT);
            printf("  --name <name>         Client name (default: VUSBClient)\n");
            printf("  --help, -h            Show this help\n");
            return 0;
        }
    }

    printf("Configuration:\n");
    printf("  Server: %s:%d\n", config.ServerAddress, config.ServerPort);
    printf("  Client name: %s\n\n", config.ClientName);

    /* Initialize client */
    result = VusbClientInit(&g_ClientContext, &config);
    if (result != 0) {
        fprintf(stderr, "Failed to initialize client: %d\n", result);
        return 1;
    }

    /* Connect to server */
    result = VusbClientConnect(&g_ClientContext);
    if (result != 0) {
        fprintf(stderr, "Failed to connect to server: %d\n", result);
        VusbClientCleanup(&g_ClientContext);
        return 1;
    }

    /* Run client (interactive mode) */
    result = VusbClientRunInteractive(&g_ClientContext);

    /* Cleanup */
    VusbClientCleanup(&g_ClientContext);

    return result;
}

/**
 * VusbClientInit - Initialize client
 */
int VusbClientInit(PVUSB_CLIENT_CONTEXT ctx, PVUSB_CLIENT_CONFIG config)
{
#ifdef _WIN32
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        fprintf(stderr, "WSAStartup failed: %d\n", result);
        return result;
    }
#endif

    memset(ctx, 0, sizeof(VUSB_CLIENT_CONTEXT));
    ctx->Config = *config;
    ctx->Socket = INVALID_SOCKET;
    ctx->Connected = 0;
    ctx->Sequence = 0;

    printf("Client initialized.\n");
    return 0;
}

/**
 * VusbClientConnect - Connect to server
 */
int VusbClientConnect(PVUSB_CLIENT_CONTEXT ctx)
{
    struct sockaddr_in serverAddr;
    VUSB_CONNECT_REQUEST request;
    VUSB_CONNECT_RESPONSE response;
    int result;

    /* Create socket */
    ctx->Socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ctx->Socket == INVALID_SOCKET) {
        fprintf(stderr, "socket() failed\n");
        return -1;
    }

    /* Setup server address */
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(ctx->Config.ServerPort);

    if (inet_pton(AF_INET, ctx->Config.ServerAddress, &serverAddr.sin_addr) <= 0) {
        /* Try DNS resolution */
        struct hostent* host = gethostbyname(ctx->Config.ServerAddress);
        if (host == NULL) {
            fprintf(stderr, "Invalid server address: %s\n", ctx->Config.ServerAddress);
            closesocket(ctx->Socket);
            ctx->Socket = INVALID_SOCKET;
            return -1;
        }
        memcpy(&serverAddr.sin_addr, host->h_addr_list[0], host->h_length);
    }

    /* Connect */
    printf("Connecting to %s:%d...\n", ctx->Config.ServerAddress, ctx->Config.ServerPort);

    result = connect(ctx->Socket, (struct sockaddr*)&serverAddr, sizeof(serverAddr));
    if (result == SOCKET_ERROR) {
        fprintf(stderr, "connect() failed\n");
        closesocket(ctx->Socket);
        ctx->Socket = INVALID_SOCKET;
        return -1;
    }

    /* Send connect request */
    VusbInitHeader(&request.Header, VUSB_CMD_CONNECT, 
                   sizeof(request) - sizeof(VUSB_HEADER), ++ctx->Sequence);
    request.ClientVersion = 0x00010000;
    request.Capabilities = 0;
    strncpy(request.ClientName, ctx->Config.ClientName, sizeof(request.ClientName) - 1);

    result = send(ctx->Socket, (char*)&request, sizeof(request), 0);
    if (result != sizeof(request)) {
        fprintf(stderr, "Failed to send connect request\n");
        closesocket(ctx->Socket);
        ctx->Socket = INVALID_SOCKET;
        return -1;
    }

    /* Receive response */
    result = recv(ctx->Socket, (char*)&response, sizeof(response), MSG_WAITALL);
    if (result != sizeof(response)) {
        fprintf(stderr, "Failed to receive connect response\n");
        closesocket(ctx->Socket);
        ctx->Socket = INVALID_SOCKET;
        return -1;
    }

    if (!VusbValidateHeader(&response.Header) || response.Status != VUSB_STATUS_SUCCESS) {
        fprintf(stderr, "Connect rejected by server\n");
        closesocket(ctx->Socket);
        ctx->Socket = INVALID_SOCKET;
        return -1;
    }

    ctx->Connected = 1;
    ctx->SessionId = response.SessionId;

    printf("Connected! Session ID: %u\n", ctx->SessionId);
    return 0;
}

/**
 * VusbClientDisconnect - Disconnect from server
 */
void VusbClientDisconnect(PVUSB_CLIENT_CONTEXT ctx)
{
    if (ctx->Socket != INVALID_SOCKET) {
        /* Send disconnect notification */
        VUSB_HEADER disconnect;
        VusbInitHeader(&disconnect, VUSB_CMD_DISCONNECT, 0, ++ctx->Sequence);
        send(ctx->Socket, (char*)&disconnect, sizeof(disconnect), 0);

        closesocket(ctx->Socket);
        ctx->Socket = INVALID_SOCKET;
    }

    ctx->Connected = 0;
    printf("Disconnected from server.\n");
}

/**
 * VusbClientAttachDevice - Attach a device to the server
 */
int VusbClientAttachDevice(
    PVUSB_CLIENT_CONTEXT ctx,
    PVUSB_DEVICE_INFO deviceInfo,
    uint8_t* descriptors,
    uint32_t descriptorLength,
    uint32_t* remoteDeviceId)
{
    VUSB_DEVICE_ATTACH_RESPONSE response;
    size_t requestSize;
    uint8_t* requestBuffer;
    VUSB_HEADER* header;
    int result;

    if (!ctx->Connected) {
        return -1;
    }

    *remoteDeviceId = 0;

    /* Build attach request */
    requestSize = sizeof(VUSB_HEADER) + sizeof(VUSB_DEVICE_INFO) + sizeof(uint32_t) + descriptorLength;
    requestBuffer = (uint8_t*)malloc(requestSize);
    if (!requestBuffer) {
        return -1;
    }

    header = (VUSB_HEADER*)requestBuffer;
    VusbInitHeader(header, VUSB_CMD_DEVICE_ATTACH, 
                   (uint32_t)(requestSize - sizeof(VUSB_HEADER)), ++ctx->Sequence);
    
    memcpy(requestBuffer + sizeof(VUSB_HEADER), deviceInfo, sizeof(VUSB_DEVICE_INFO));
    memcpy(requestBuffer + sizeof(VUSB_HEADER) + sizeof(VUSB_DEVICE_INFO), 
           &descriptorLength, sizeof(uint32_t));
    if (descriptorLength > 0) {
        memcpy(requestBuffer + sizeof(VUSB_HEADER) + sizeof(VUSB_DEVICE_INFO) + sizeof(uint32_t),
               descriptors, descriptorLength);
    }

    /* Send request */
    result = send(ctx->Socket, (char*)requestBuffer, (int)requestSize, 0);
    free(requestBuffer);

    if (result != (int)requestSize) {
        fprintf(stderr, "Failed to send attach request\n");
        return -1;
    }

    /* Receive response */
    result = recv(ctx->Socket, (char*)&response, sizeof(response), MSG_WAITALL);
    if (result != sizeof(response)) {
        fprintf(stderr, "Failed to receive attach response\n");
        return -1;
    }

    if (response.Status != VUSB_STATUS_SUCCESS) {
        fprintf(stderr, "Attach failed with status %u\n", response.Status);
        return -1;
    }

    *remoteDeviceId = response.DeviceId;
    printf("Device attached with remote ID: %u\n", *remoteDeviceId);

    return 0;
}

/**
 * VusbClientDetachDevice - Detach a device from the server
 */
int VusbClientDetachDevice(PVUSB_CLIENT_CONTEXT ctx, uint32_t remoteDeviceId)
{
    uint8_t buffer[sizeof(VUSB_HEADER) + sizeof(uint32_t)];
    VUSB_HEADER* header = (VUSB_HEADER*)buffer;
    int result;

    if (!ctx->Connected) {
        return -1;
    }

    VusbInitHeader(header, VUSB_CMD_DEVICE_DETACH, sizeof(uint32_t), ++ctx->Sequence);
    memcpy(buffer + sizeof(VUSB_HEADER), &remoteDeviceId, sizeof(uint32_t));

    result = send(ctx->Socket, (char*)buffer, sizeof(buffer), 0);
    if (result != sizeof(buffer)) {
        return -1;
    }

    printf("Device %u detached.\n", remoteDeviceId);
    return 0;
}

/**
 * VusbClientRunInteractive - Run interactive command loop
 */
int VusbClientRunInteractive(PVUSB_CLIENT_CONTEXT ctx)
{
    char command[256];

    printf("\nInteractive mode. Commands:\n");
    printf("  attach <vid> <pid>   - Attach a simulated USB device\n");
    printf("  detach <id>          - Detach a device\n");
    printf("  list                 - List attached devices\n");
    printf("  ping                 - Ping server\n");
    printf("  quit                 - Exit\n\n");

    while (ctx->Connected) {
        printf("> ");
        fflush(stdout);

        if (!fgets(command, sizeof(command), stdin)) {
            break;
        }

        /* Remove newline */
        command[strcspn(command, "\r\n")] = 0;

        if (strlen(command) == 0) {
            continue;
        }

        if (strncmp(command, "attach", 6) == 0) {
            uint16_t vid, pid;
            if (sscanf(command + 6, "%hx %hx", &vid, &pid) == 2) {
                VusbClientAttachSimulatedDevice(ctx, vid, pid);
            } else {
                printf("Usage: attach <vid> <pid>\n");
            }
        } else if (strncmp(command, "detach", 6) == 0) {
            uint32_t id;
            if (sscanf(command + 6, "%u", &id) == 1) {
                VusbClientDetachDevice(ctx, id);
            } else {
                printf("Usage: detach <id>\n");
            }
        } else if (strcmp(command, "list") == 0) {
            VusbClientListDevices(ctx);
        } else if (strcmp(command, "ping") == 0) {
            VusbClientPing(ctx);
        } else if (strcmp(command, "quit") == 0 || strcmp(command, "exit") == 0) {
            break;
        } else {
            printf("Unknown command: %s\n", command);
        }
    }

    return 0;
}

/**
 * VusbClientAttachSimulatedDevice - Attach a simulated device for testing
 */
int VusbClientAttachSimulatedDevice(PVUSB_CLIENT_CONTEXT ctx, uint16_t vid, uint16_t pid)
{
    VUSB_DEVICE_INFO deviceInfo;
    uint32_t remoteId;

    /* Build simulated device info */
    memset(&deviceInfo, 0, sizeof(deviceInfo));
    deviceInfo.DeviceId = ++ctx->NextDeviceId;
    deviceInfo.VendorId = vid;
    deviceInfo.ProductId = pid;
    deviceInfo.DeviceClass = 0xFF;  /* Vendor specific */
    deviceInfo.DeviceSubClass = 0;
    deviceInfo.DeviceProtocol = 0;
    deviceInfo.Speed = VUSB_SPEED_HIGH;
    deviceInfo.NumConfigurations = 1;
    deviceInfo.NumInterfaces = 1;
    snprintf(deviceInfo.Manufacturer, sizeof(deviceInfo.Manufacturer), "Virtual");
    snprintf(deviceInfo.Product, sizeof(deviceInfo.Product), "USB Device %04X:%04X", vid, pid);
    snprintf(deviceInfo.SerialNumber, sizeof(deviceInfo.SerialNumber), "SIM%08X", deviceInfo.DeviceId);

    /* Build minimal USB descriptors */
    uint8_t descriptors[64];
    int offset = 0;

    /* Device descriptor */
    descriptors[offset++] = 18;     /* bLength */
    descriptors[offset++] = 1;      /* bDescriptorType (Device) */
    descriptors[offset++] = 0x00;   /* bcdUSB (2.0) */
    descriptors[offset++] = 0x02;
    descriptors[offset++] = 0xFF;   /* bDeviceClass */
    descriptors[offset++] = 0x00;   /* bDeviceSubClass */
    descriptors[offset++] = 0x00;   /* bDeviceProtocol */
    descriptors[offset++] = 64;     /* bMaxPacketSize0 */
    descriptors[offset++] = vid & 0xFF;
    descriptors[offset++] = vid >> 8;
    descriptors[offset++] = pid & 0xFF;
    descriptors[offset++] = pid >> 8;
    descriptors[offset++] = 0x00;   /* bcdDevice */
    descriptors[offset++] = 0x01;
    descriptors[offset++] = 1;      /* iManufacturer */
    descriptors[offset++] = 2;      /* iProduct */
    descriptors[offset++] = 3;      /* iSerialNumber */
    descriptors[offset++] = 1;      /* bNumConfigurations */

    return VusbClientAttachDevice(ctx, &deviceInfo, descriptors, offset, &remoteId);
}

/**
 * VusbClientListDevices - Request device list from server
 */
int VusbClientListDevices(PVUSB_CLIENT_CONTEXT ctx)
{
    VUSB_HEADER request;
    VUSB_DEVICE_LIST_RESPONSE response;
    int result;

    if (!ctx->Connected) {
        return -1;
    }

    VusbInitHeader(&request, VUSB_CMD_DEVICE_LIST, 0, ++ctx->Sequence);

    result = send(ctx->Socket, (char*)&request, sizeof(request), 0);
    if (result != sizeof(request)) {
        return -1;
    }

    result = recv(ctx->Socket, (char*)&response, sizeof(response), MSG_WAITALL);
    if (result != sizeof(response)) {
        return -1;
    }

    printf("Devices attached: %u\n", response.DeviceCount);

    /* Receive device info */
    for (uint32_t i = 0; i < response.DeviceCount; i++) {
        VUSB_DEVICE_INFO info;
        result = recv(ctx->Socket, (char*)&info, sizeof(info), MSG_WAITALL);
        if (result == sizeof(info)) {
            printf("  [%u] VID:%04X PID:%04X - %s %s\n",
                   info.DeviceId, info.VendorId, info.ProductId,
                   info.Manufacturer, info.Product);
        }
    }

    return 0;
}

/**
 * VusbClientPing - Ping server
 */
int VusbClientPing(PVUSB_CLIENT_CONTEXT ctx)
{
    VUSB_HEADER request, response;
    int result;

    if (!ctx->Connected) {
        return -1;
    }

    VusbInitHeader(&request, VUSB_CMD_PING, 0, ++ctx->Sequence);

    result = send(ctx->Socket, (char*)&request, sizeof(request), 0);
    if (result != sizeof(request)) {
        return -1;
    }

    result = recv(ctx->Socket, (char*)&response, sizeof(response), MSG_WAITALL);
    if (result == sizeof(response) && response.Command == VUSB_CMD_PONG) {
        printf("Pong received.\n");
        return 0;
    }

    printf("Ping failed.\n");
    return -1;
}

/**
 * VusbClientCleanup - Cleanup client resources
 */
void VusbClientCleanup(PVUSB_CLIENT_CONTEXT ctx)
{
    VusbClientDisconnect(ctx);

#ifdef _WIN32
    WSACleanup();
#endif

    printf("Client cleanup complete.\n");
}
