/**
 * Virtual USB Client Application (Enhanced)
 * 
 * Client application with real USB device capture and URB handling.
 * Captures real USB devices using WinUSB and forwards them over the network.
 */

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "vusb_client.h"
#include "vusb_capture.h"
#include "vusb_client_urb.h"
#include "../protocol/vusb_protocol.h"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "winusb.lib")
#pragma comment(lib, "setupapi.lib")

/* Extended client context */
typedef struct _VUSB_CLIENT_CONTEXT_EX {
    VUSB_CLIENT_CONTEXT     Base;
    USB_CAPTURE_CONTEXT     Capture;
    CLIENT_URB_CONTEXT      UrbHandler;
    HANDLE                  ReceiveThread;
    HANDLE                  UrbThread;
    volatile BOOL           Running;
} VUSB_CLIENT_CONTEXT_EX, *PVUSB_CLIENT_CONTEXT_EX;

/* Global context */
static VUSB_CLIENT_CONTEXT_EX g_ClientContextEx = {0};

/* Forward declarations */
static DWORD WINAPI ReceiveThread(LPVOID param);
static DWORD WINAPI UrbProcessThread(LPVOID param);
static void ProcessServerMessage(PVUSB_CLIENT_CONTEXT_EX ctx, PVUSB_HEADER header, 
                                  uint8_t* payload, uint32_t payloadLength);
static int SendUrbCompletion(void* ctx, uint32_t deviceId, uint32_t urbId,
                             uint32_t status, uint32_t actualLength, uint8_t* data);
void RunEnhancedInteractive(PVUSB_CLIENT_CONTEXT_EX ctx);

/**
 * main - Enhanced client entry point
 */
int main(int argc, char* argv[])
{
    VUSB_CLIENT_CONFIG config = {0};
    int result;
    PVUSB_CLIENT_CONTEXT_EX ctx = &g_ClientContextEx;

    printf("Virtual USB Client v2.0 (with USB Capture)\n");
    printf("==========================================\n\n");

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

    /* Initialize Winsock */
    WSADATA wsaData;
    result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        fprintf(stderr, "WSAStartup failed: %d\n", result);
        return 1;
    }

    /* Initialize base client */
    memset(ctx, 0, sizeof(VUSB_CLIENT_CONTEXT_EX));
    ctx->Base.Config = config;
    ctx->Base.Socket = INVALID_SOCKET;

    /* Initialize USB capture */
    result = UsbCaptureInit(&ctx->Capture);
    if (result != 0) {
        fprintf(stderr, "Failed to initialize USB capture: %d\n", result);
        WSACleanup();
        return 1;
    }

    /* Initialize URB handler */
    result = ClientUrbInit(&ctx->UrbHandler, &ctx->Capture);
    if (result != 0) {
        fprintf(stderr, "Failed to initialize URB handler: %d\n", result);
        UsbCaptureCleanup(&ctx->Capture);
        WSACleanup();
        return 1;
    }
    ctx->UrbHandler.ClientContext = ctx;
    ctx->UrbHandler.SendCompletion = SendUrbCompletion;

    /* Enumerate USB devices */
    printf("Scanning for USB devices...\n");
    UsbCaptureRefreshDevices(&ctx->Capture);

    /* Connect to server */
    result = VusbClientConnect(&ctx->Base);
    if (result != 0) {
        fprintf(stderr, "Failed to connect to server: %d\n", result);
        UsbCaptureCleanup(&ctx->Capture);
        WSACleanup();
        return 1;
    }

    ctx->Running = TRUE;

    /* Start receive thread */
    ctx->ReceiveThread = CreateThread(NULL, 0, ReceiveThread, ctx, 0, NULL);

    /* Run interactive mode */
    RunEnhancedInteractive(ctx);

    /* Cleanup */
    ctx->Running = FALSE;
    closesocket(ctx->Base.Socket);
    
    if (ctx->ReceiveThread) {
        WaitForSingleObject(ctx->ReceiveThread, 2000);
        CloseHandle(ctx->ReceiveThread);
    }

    UsbCaptureCleanup(&ctx->Capture);
    WSACleanup();

    printf("Client shutdown complete.\n");
    return 0;
}

/**
 * ReceiveThread - Background thread to receive server messages
 */
static DWORD WINAPI ReceiveThread(LPVOID param)
{
    PVUSB_CLIENT_CONTEXT_EX ctx = (PVUSB_CLIENT_CONTEXT_EX)param;
    VUSB_HEADER header;
    uint8_t* payload = NULL;
    int result;

    printf("[Recv] Receive thread started\n");

    payload = (uint8_t*)malloc(VUSB_MAX_PACKET_SIZE);
    if (!payload) {
        return 1;
    }

    while (ctx->Running && ctx->Base.Connected) {
        /* Receive header */
        result = recv(ctx->Base.Socket, (char*)&header, sizeof(header), MSG_WAITALL);
        if (result != sizeof(header)) {
            if (ctx->Running) {
                printf("[Recv] Connection closed\n");
            }
            break;
        }

        /* Validate header */
        if (!VusbValidateHeader(&header)) {
            printf("[Recv] Invalid protocol header\n");
            continue;
        }

        /* Receive payload */
        if (header.Length > 0) {
            if (header.Length > VUSB_MAX_PACKET_SIZE) {
                printf("[Recv] Payload too large: %u\n", header.Length);
                break;
            }

            result = recv(ctx->Base.Socket, (char*)payload, header.Length, MSG_WAITALL);
            if (result != (int)header.Length) {
                printf("[Recv] Failed to receive payload\n");
                break;
            }
        }

        /* Process message */
        ProcessServerMessage(ctx, &header, payload, header.Length);
    }

    free(payload);
    ctx->Base.Connected = 0;
    printf("[Recv] Receive thread ended\n");
    return 0;
}

/**
 * ProcessServerMessage - Process a message from the server
 */
static void ProcessServerMessage(PVUSB_CLIENT_CONTEXT_EX ctx, PVUSB_HEADER header,
                                  uint8_t* payload, uint32_t payloadLength)
{
    switch (header->Command) {
    case VUSB_CMD_PING:
        {
            VUSB_HEADER pong;
            VusbInitHeader(&pong, VUSB_CMD_PONG, 0, header->Sequence);
            send(ctx->Base.Socket, (char*)&pong, sizeof(pong), 0);
        }
        break;

    case VUSB_CMD_SUBMIT_URB:
        {
            /* URB request from server - forward to real device */
            if (payloadLength >= sizeof(VUSB_URB_SUBMIT) - sizeof(VUSB_HEADER)) {
                VUSB_URB_SUBMIT* urbSubmit = (VUSB_URB_SUBMIT*)payload;
                uint8_t* outData = NULL;
                uint32_t outDataLen = 0;

                /* Check for OUT data following the header */
                if (urbSubmit->Direction == VUSB_DIR_OUT && 
                    urbSubmit->TransferBufferLength > 0) {
                    outData = payload + sizeof(VUSB_URB_SUBMIT) - sizeof(VUSB_HEADER);
                    outDataLen = urbSubmit->TransferBufferLength;
                }

                ClientUrbProcess(&ctx->UrbHandler, urbSubmit, outData, outDataLen);
            }
        }
        break;

    case VUSB_CMD_CANCEL_URB:
        {
            if (payloadLength >= sizeof(VUSB_URB_CANCEL) - sizeof(VUSB_HEADER)) {
                VUSB_URB_CANCEL* cancel = (VUSB_URB_CANCEL*)payload;
                ClientUrbCancel(&ctx->UrbHandler, cancel->DeviceId, cancel->UrbId);
            }
        }
        break;

    case VUSB_CMD_ERROR:
        {
            if (payloadLength >= sizeof(VUSB_ERROR) - sizeof(VUSB_HEADER)) {
                VUSB_ERROR* error = (VUSB_ERROR*)payload;
                printf("[Server Error] Code=%u: %s\n", error->ErrorCode, error->ErrorMessage);
            }
        }
        break;

    default:
        printf("[Recv] Unhandled command: 0x%04X\n", header->Command);
        break;
    }
}

/**
 * SendUrbCompletion - Send URB completion back to server
 */
static int SendUrbCompletion(void* clientCtx, uint32_t deviceId, uint32_t urbId,
                             uint32_t status, uint32_t actualLength, uint8_t* data)
{
    PVUSB_CLIENT_CONTEXT_EX ctx = (PVUSB_CLIENT_CONTEXT_EX)clientCtx;
    uint8_t* buffer;
    VUSB_URB_COMPLETE* completion;
    size_t totalSize;
    int result;

    totalSize = sizeof(VUSB_URB_COMPLETE) + actualLength;
    buffer = (uint8_t*)malloc(totalSize);
    if (!buffer) return -1;

    completion = (VUSB_URB_COMPLETE*)buffer;
    VusbInitHeader(&completion->Header, VUSB_CMD_URB_COMPLETE,
                   (uint32_t)(totalSize - sizeof(VUSB_HEADER)), ++ctx->Base.Sequence);
    completion->DeviceId = deviceId;
    completion->UrbId = urbId;
    completion->Status = status;
    completion->ActualLength = actualLength;
    completion->ErrorCount = 0;

    if (data && actualLength > 0) {
        memcpy(buffer + sizeof(VUSB_URB_COMPLETE), data, actualLength);
    }

    result = send(ctx->Base.Socket, (char*)buffer, (int)totalSize, 0);
    free(buffer);

    return (result == (int)totalSize) ? 0 : -1;
}

/**
 * AttachRealDevice - Attach a real USB device to the server
 */
int AttachRealDevice(PVUSB_CLIENT_CONTEXT_EX ctx, uint32_t localId)
{
    PUSB_CAPTURED_DEVICE device;
    uint32_t remoteId;
    int result;

    device = UsbCaptureFindDevice(&ctx->Capture, localId);
    if (!device) {
        printf("Device %u not found\n", localId);
        return -1;
    }

    if (!device->Opened) {
        result = UsbCaptureOpenDevice(device);
        if (result != 0) {
            printf("Failed to open device: %d\n", result);
            return -1;
        }
        UsbCaptureGetDescriptors(device);
    }

    /* Send attach request */
    result = VusbClientAttachDevice(&ctx->Base, &device->DeviceInfo,
                                    device->Descriptors, device->DescriptorLength,
                                    &remoteId);
    if (result == 0) {
        device->RemoteId = remoteId;
        printf("Device attached with remote ID: %u\n", remoteId);
    }

    return result;
}

/**
 * RunEnhancedInteractive - Run enhanced interactive command loop
 */
void RunEnhancedInteractive(PVUSB_CLIENT_CONTEXT_EX ctx)
{
    char command[256];

    printf("\nEnhanced Interactive Mode. Commands:\n");
    printf("  scan                 - Scan for USB devices\n");
    printf("  list                 - List local USB devices\n");
    printf("  info <id>            - Show device info\n");
    printf("  attach <id>          - Attach device to server\n");
    printf("  detach <id>          - Detach device from server\n");
    printf("  remote               - List remote (server) devices\n");
    printf("  sim <vid> <pid>      - Attach a simulated device\n");
    printf("  ping                 - Ping server\n");
    printf("  quit                 - Exit\n\n");

    while (ctx->Running && ctx->Base.Connected) {
        printf("> ");
        fflush(stdout);

        if (!fgets(command, sizeof(command), stdin)) {
            break;
        }

        command[strcspn(command, "\r\n")] = 0;

        if (strlen(command) == 0) {
            continue;
        }

        if (strcmp(command, "scan") == 0) {
            printf("Scanning for USB devices...\n");
            UsbCaptureRefreshDevices(&ctx->Capture);
        }
        else if (strcmp(command, "list") == 0) {
            printf("Local USB Devices:\n");
            for (int i = 0; i < MAX_USB_DEVICES; i++) {
                PUSB_CAPTURED_DEVICE dev = &ctx->Capture.Devices[i];
                if (dev->Active) {
                    printf("  [%u] VID:%04X PID:%04X %s %s %s\n",
                           dev->LocalId, dev->DeviceInfo.VendorId, dev->DeviceInfo.ProductId,
                           dev->DeviceInfo.Manufacturer, dev->DeviceInfo.Product,
                           dev->Opened ? "(opened)" : "");
                }
            }
        }
        else if (strncmp(command, "info", 4) == 0) {
            uint32_t id;
            if (sscanf(command + 4, "%u", &id) == 1) {
                PUSB_CAPTURED_DEVICE dev = UsbCaptureFindDevice(&ctx->Capture, id);
                if (dev) {
                    if (!dev->Opened) {
                        UsbCaptureOpenDevice(dev);
                        UsbCaptureGetDescriptors(dev);
                    }
                    UsbCapturePrintDeviceInfo(dev);
                } else {
                    printf("Device %u not found\n", id);
                }
            } else {
                printf("Usage: info <device_id>\n");
            }
        }
        else if (strncmp(command, "attach", 6) == 0) {
            uint32_t id;
            if (sscanf(command + 6, "%u", &id) == 1) {
                AttachRealDevice(ctx, id);
            } else {
                printf("Usage: attach <device_id>\n");
            }
        }
        else if (strncmp(command, "detach", 6) == 0) {
            uint32_t id;
            if (sscanf(command + 6, "%u", &id) == 1) {
                VusbClientDetachDevice(&ctx->Base, id);
            } else {
                printf("Usage: detach <remote_id>\n");
            }
        }
        else if (strcmp(command, "remote") == 0) {
            VusbClientListDevices(&ctx->Base);
        }
        else if (strncmp(command, "sim", 3) == 0) {
            uint16_t vid, pid;
            if (sscanf(command + 3, "%hx %hx", &vid, &pid) == 2) {
                VusbClientAttachSimulatedDevice(&ctx->Base, vid, pid);
            } else {
                printf("Usage: sim <vid> <pid>\n");
            }
        }
        else if (strcmp(command, "ping") == 0) {
            VusbClientPing(&ctx->Base);
        }
        else if (strcmp(command, "quit") == 0 || strcmp(command, "exit") == 0) {
            break;
        }
        else {
            printf("Unknown command: %s\n", command);
        }
    }
}
