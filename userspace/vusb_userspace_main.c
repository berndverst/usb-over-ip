/**
 * Virtual USB Userspace Server - Main Entry Point
 * 
 * A combined server and userspace driver that handles USB device forwarding
 * without requiring a kernel driver. This is useful for:
 * 
 * - Testing and development without driver installation
 * - Cross-platform compatibility (can be ported to other OSes)
 * - Debugging USB traffic
 * - Application-level USB gadget emulation
 * 
 * Note: For full system-level USB device presentation, the kernel driver
 * is still required. This userspace implementation provides:
 * - Full protocol handling compatible with clients
 * - Device simulation for testing
 * - USB traffic capture
 * - Custom gadget emulation through callbacks
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <conio.h>

#include "vusb_userspace.h"
#include "../protocol/vusb_protocol.h"

/* Global context for signal handler */
static VUSB_US_CONTEXT g_Context = {0};

/**
 * Signal handler for graceful shutdown
 */
static BOOL WINAPI ConsoleHandler(DWORD signal)
{
    if (signal == CTRL_C_EVENT || signal == CTRL_BREAK_EVENT) {
        printf("\n\nShutting down...\n");
        VusbUsStop(&g_Context);
        return TRUE;
    }
    return FALSE;
}

/**
 * Print usage information
 */
static void PrintUsage(const char* progname)
{
    printf("Virtual USB Userspace Server v1.0\n");
    printf("\n");
    printf("Usage: %s [options]\n", progname);
    printf("\n");
    printf("Options:\n");
    printf("  --port <port>        Listen port (default: %d)\n", VUSB_DEFAULT_PORT);
    printf("  --max-clients <n>    Maximum clients (default: %d)\n", VUSB_US_MAX_CLIENTS);
    printf("  --max-devices <n>    Maximum devices (default: %d)\n", VUSB_US_MAX_DEVICES);
    printf("  --simulation         Enable device simulation mode\n");
    printf("  --verbose            Enable verbose logging\n");
    printf("  --capture <file>     Capture USB traffic to file\n");
    printf("  --help, -h           Show this help\n");
    printf("\n");
    printf("Description:\n");
    printf("  This is a userspace implementation of the Virtual USB server.\n");
    printf("  It does not require a kernel driver and can be used for:\n");
    printf("    - Testing client applications\n");
    printf("    - Debugging USB traffic\n");
    printf("    - Custom USB gadget emulation\n");
    printf("\n");
    printf("  For full system-level USB device presentation, use the\n");
    printf("  kernel driver with vusb_server instead.\n");
    printf("\n");
}

/**
 * Print status command help
 */
static void PrintInteractiveHelp(void)
{
    printf("\nInteractive Commands (press key):\n");
    printf("  h - Show this help\n");
    printf("  s - Show statistics\n");
    printf("  d - List devices\n");
    printf("  c - List clients\n");
    printf("  q - Quit\n");
    printf("\n");
}

/**
 * Print statistics
 */
static void PrintStats(PVUSB_US_CONTEXT ctx)
{
    VUSB_STATISTICS stats;
    VusbUsGetStats(ctx, &stats);
    
    printf("\n=== Server Statistics ===\n");
    printf("  Active devices:    %u\n", stats.ActiveDevices);
    printf("  Pending URBs:      %u\n", stats.PendingUrbs);
    printf("  URBs submitted:    %llu\n", stats.TotalUrbsSubmitted);
    printf("  URBs completed:    %llu\n", stats.TotalUrbsCompleted);
    printf("  Bytes in:          %llu\n", stats.TotalBytesIn);
    printf("  Bytes out:         %llu\n", stats.TotalBytesOut);
    printf("=========================\n\n");
}

/**
 * Print devices
 */
static void PrintDevices(PVUSB_US_CONTEXT ctx)
{
    VUSB_DEVICE_INFO devices[VUSB_US_MAX_DEVICES];
    int count = VusbUsListDevices(ctx, devices, VUSB_US_MAX_DEVICES);
    
    printf("\n=== Connected Devices (%d) ===\n", count);
    for (int i = 0; i < count; i++) {
        printf("  [%u] %04X:%04X - %s %s\n",
               devices[i].DeviceId,
               devices[i].VendorId, devices[i].ProductId,
               devices[i].Manufacturer, devices[i].Product);
    }
    if (count == 0) {
        printf("  (none)\n");
    }
    printf("==============================\n\n");
}

/**
 * Client list callback
 */
static void PrintClientCallback(PVUSB_US_CLIENT client, void* userdata)
{
    UNREFERENCED_PARAMETER(userdata);
    printf("  [%u] %s - %s (devices: %d)\n",
           client->SessionId, client->AddressString,
           client->ClientName[0] ? client->ClientName : "(unnamed)",
           client->DeviceCount);
}

/**
 * Print clients
 */
static void PrintClients(PVUSB_US_CONTEXT ctx)
{
    printf("\n=== Connected Clients ===\n");
    VusbUsListClients(ctx, PrintClientCallback, NULL);
    printf("=========================\n\n");
}

/**
 * Interactive console thread
 */
static DWORD WINAPI ConsoleThread(LPVOID param)
{
    PVUSB_US_CONTEXT ctx = (PVUSB_US_CONTEXT)param;
    
    PrintInteractiveHelp();
    
    while (ctx->Running) {
        /* Check for console input */
        if (_kbhit()) {
            int ch = _getch();
            switch (ch) {
            case 'h':
            case 'H':
            case '?':
                PrintInteractiveHelp();
                break;
                
            case 's':
            case 'S':
                PrintStats(ctx);
                break;
                
            case 'd':
            case 'D':
                PrintDevices(ctx);
                break;
                
            case 'c':
            case 'C':
                PrintClients(ctx);
                break;
                
            case 'q':
            case 'Q':
                printf("\nQuitting...\n");
                VusbUsStop(ctx);
                break;
            }
        }
        
        Sleep(100);
    }
    
    return 0;
}

/**
 * Main entry point
 */
int main(int argc, char* argv[])
{
    VUSB_US_CONFIG config = {0};
    int result;
    BOOL enableConsole = TRUE;
    
    /* Set defaults */
    config.Port = VUSB_DEFAULT_PORT;
    config.MaxClients = VUSB_US_MAX_CLIENTS;
    config.MaxDevices = VUSB_US_MAX_DEVICES;
    config.EnableSimulation = FALSE;
    config.EnableLogging = FALSE;
    config.EnableCapture = FALSE;
    
    /* Parse command line */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            config.Port = (uint16_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--max-clients") == 0 && i + 1 < argc) {
            config.MaxClients = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--max-devices") == 0 && i + 1 < argc) {
            config.MaxDevices = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--simulation") == 0) {
            config.EnableSimulation = TRUE;
        } else if (strcmp(argv[i], "--verbose") == 0) {
            config.EnableLogging = TRUE;
        } else if (strcmp(argv[i], "--capture") == 0 && i + 1 < argc) {
            config.EnableCapture = TRUE;
            strncpy(config.CaptureFile, argv[++i], MAX_PATH - 1);
        } else if (strcmp(argv[i], "--no-console") == 0) {
            enableConsole = FALSE;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            PrintUsage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            PrintUsage(argv[0]);
            return 1;
        }
    }
    
    /* Set up signal handler */
    SetConsoleCtrlHandler(ConsoleHandler, TRUE);
    
    /* Initialize */
    result = VusbUsInit(&g_Context, &config);
    if (result != 0) {
        fprintf(stderr, "Failed to initialize server: %d\n", result);
        return 1;
    }
    
    /* Start capture if requested */
    if (config.EnableCapture && config.CaptureFile[0]) {
        VusbUsStartCapture(&g_Context, config.CaptureFile);
    }
    
    /* Start interactive console thread */
    HANDLE consoleThread = NULL;
    if (enableConsole) {
        consoleThread = CreateThread(NULL, 0, ConsoleThread, &g_Context, 0, NULL);
    }
    
    /* Run server (blocking) */
    result = VusbUsRun(&g_Context);
    
    /* Cleanup */
    if (consoleThread) {
        WaitForSingleObject(consoleThread, 1000);
        CloseHandle(consoleThread);
    }
    
    VusbUsCleanup(&g_Context);
    
    printf("Server stopped.\n");
    return result;
}
