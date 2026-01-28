/**
 * Test utility for Virtual USB driver
 * 
 * Tests the driver IOCTL interface without network involvement.
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#include "../protocol/vusb_protocol.h"
#include "../protocol/vusb_ioctl.h"

HANDLE OpenDriver(void);
void TestGetVersion(HANDLE driver);
void TestPluginDevice(HANDLE driver);
void TestDeviceList(HANDLE driver);
void TestUnplugDevice(HANDLE driver, ULONG deviceId);
void TestStatistics(HANDLE driver);

int main(int argc, char* argv[])
{
    HANDLE driver;
    ULONG testDeviceId = 0;

    printf("Virtual USB Driver Test Utility\n");
    printf("================================\n\n");

    driver = OpenDriver();
    if (driver == INVALID_HANDLE_VALUE) {
        printf("Failed to open driver. Is it installed?\n");
        printf("Run: vusb_install install vusb.inf\n");
        return 1;
    }

    printf("Driver opened successfully.\n\n");

    /* Run tests */
    TestGetVersion(driver);
    
    printf("\n--- Plugin Device Test ---\n");
    TestPluginDevice(driver);
    
    printf("\n--- Device List Test ---\n");
    TestDeviceList(driver);
    
    printf("\n--- Statistics Test ---\n");
    TestStatistics(driver);
    
    /* Interactive mode */
    printf("\n--- Interactive Mode ---\n");
    printf("Commands: plugin, unplug <id>, list, stats, quit\n\n");
    
    char command[256];
    while (1) {
        printf("> ");
        if (!fgets(command, sizeof(command), stdin)) break;
        command[strcspn(command, "\r\n")] = 0;
        
        if (strcmp(command, "plugin") == 0) {
            TestPluginDevice(driver);
        } else if (strncmp(command, "unplug", 6) == 0) {
            ULONG id = 0;
            if (sscanf(command + 6, "%lu", &id) == 1) {
                TestUnplugDevice(driver, id);
            } else {
                printf("Usage: unplug <device_id>\n");
            }
        } else if (strcmp(command, "list") == 0) {
            TestDeviceList(driver);
        } else if (strcmp(command, "stats") == 0) {
            TestStatistics(driver);
        } else if (strcmp(command, "quit") == 0) {
            break;
        } else if (strlen(command) > 0) {
            printf("Unknown command: %s\n", command);
        }
    }

    CloseHandle(driver);
    printf("\nDriver closed.\n");
    return 0;
}

HANDLE OpenDriver(void)
{
    return CreateFileW(
        L"\\\\.\\VirtualUSB",
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        0,
        NULL
    );
}

void TestGetVersion(HANDLE driver)
{
    VUSB_VERSION_INFO version;
    DWORD bytesReturned;

    printf("Testing IOCTL_VUSB_GET_VERSION...\n");

    if (DeviceIoControl(driver, IOCTL_VUSB_GET_VERSION,
                        NULL, 0, &version, sizeof(version),
                        &bytesReturned, NULL)) {
        printf("  Driver Version: %u.%u\n", 
               (version.DriverVersion >> 16) & 0xFFFF,
               version.DriverVersion & 0xFFFF);
        printf("  Protocol Version: 0x%04X\n", version.ProtocolVersion);
        printf("  Max Devices: %u\n", version.MaxDevices);
        printf("  Capabilities: 0x%08X\n", version.Capabilities);
    } else {
        printf("  FAILED: Error %lu\n", GetLastError());
    }
}

void TestPluginDevice(HANDLE driver)
{
    /* Build a test device */
    static ULONG nextId = 1;
    
    /* Request buffer: VUSB_PLUGIN_REQUEST + descriptors */
    BYTE requestBuffer[512];
    VUSB_PLUGIN_REQUEST* request = (VUSB_PLUGIN_REQUEST*)requestBuffer;
    VUSB_PLUGIN_RESPONSE response;
    DWORD bytesReturned;
    
    memset(requestBuffer, 0, sizeof(requestBuffer));
    
    /* Fill device info */
    request->DeviceInfo.DeviceId = nextId++;
    request->DeviceInfo.VendorId = 0x1234;
    request->DeviceInfo.ProductId = 0x5678;
    request->DeviceInfo.DeviceClass = 0xFF;
    request->DeviceInfo.Speed = VUSB_SPEED_HIGH;
    request->DeviceInfo.NumConfigurations = 1;
    strcpy(request->DeviceInfo.Manufacturer, "Test Manufacturer");
    strcpy(request->DeviceInfo.Product, "Test USB Device");
    strcpy(request->DeviceInfo.SerialNumber, "TEST001");
    
    /* Build minimal device descriptor */
    BYTE* desc = (BYTE*)(request + 1);
    int offset = 0;
    
    desc[offset++] = 18;        /* bLength */
    desc[offset++] = 1;         /* bDescriptorType = Device */
    desc[offset++] = 0x00;      /* bcdUSB low */
    desc[offset++] = 0x02;      /* bcdUSB high (2.0) */
    desc[offset++] = 0xFF;      /* bDeviceClass */
    desc[offset++] = 0x00;      /* bDeviceSubClass */
    desc[offset++] = 0x00;      /* bDeviceProtocol */
    desc[offset++] = 64;        /* bMaxPacketSize0 */
    desc[offset++] = 0x34;      /* idVendor low */
    desc[offset++] = 0x12;      /* idVendor high */
    desc[offset++] = 0x78;      /* idProduct low */
    desc[offset++] = 0x56;      /* idProduct high */
    desc[offset++] = 0x00;      /* bcdDevice low */
    desc[offset++] = 0x01;      /* bcdDevice high */
    desc[offset++] = 1;         /* iManufacturer */
    desc[offset++] = 2;         /* iProduct */
    desc[offset++] = 3;         /* iSerialNumber */
    desc[offset++] = 1;         /* bNumConfigurations */
    
    request->DescriptorLength = offset;
    
    printf("Plugging in test device (VID:1234 PID:5678)...\n");
    
    if (DeviceIoControl(driver, IOCTL_VUSB_PLUGIN_DEVICE,
                        requestBuffer, sizeof(VUSB_PLUGIN_REQUEST) + offset,
                        &response, sizeof(response),
                        &bytesReturned, NULL)) {
        printf("  Status: %s\n", response.Status == VUSB_STATUS_SUCCESS ? "Success" : "Failed");
        printf("  Device ID: %u\n", response.DeviceId);
        printf("  Port: %u\n", response.PortNumber);
    } else {
        printf("  FAILED: Error %lu\n", GetLastError());
    }
}

void TestDeviceList(HANDLE driver)
{
    VUSB_DEVICE_LIST deviceList;
    DWORD bytesReturned;
    
    printf("Querying device list...\n");
    
    if (DeviceIoControl(driver, IOCTL_VUSB_GET_DEVICE_LIST,
                        NULL, 0, &deviceList, sizeof(deviceList),
                        &bytesReturned, NULL)) {
        printf("  Device count: %u\n", deviceList.DeviceCount);
        
        for (ULONG i = 0; i < deviceList.DeviceCount; i++) {
            VUSB_DEVICE_ENTRY* entry = &deviceList.Devices[i];
            printf("  [%u] ID=%u Port=%u State=%u VID:%04X PID:%04X %s\n",
                   i, entry->DeviceId, entry->PortNumber, entry->State,
                   entry->DeviceInfo.VendorId, entry->DeviceInfo.ProductId,
                   entry->DeviceInfo.Product);
        }
    } else {
        printf("  FAILED: Error %lu\n", GetLastError());
    }
}

void TestUnplugDevice(HANDLE driver, ULONG deviceId)
{
    VUSB_UNPLUG_REQUEST request;
    DWORD bytesReturned;
    
    request.DeviceId = deviceId;
    
    printf("Unplugging device %u...\n", deviceId);
    
    if (DeviceIoControl(driver, IOCTL_VUSB_UNPLUG_DEVICE,
                        &request, sizeof(request),
                        NULL, 0,
                        &bytesReturned, NULL)) {
        printf("  Success\n");
    } else {
        printf("  FAILED: Error %lu\n", GetLastError());
    }
}

void TestStatistics(HANDLE driver)
{
    VUSB_STATISTICS stats;
    DWORD bytesReturned;
    
    printf("Querying statistics...\n");
    
    if (DeviceIoControl(driver, IOCTL_VUSB_GET_STATISTICS,
                        NULL, 0, &stats, sizeof(stats),
                        &bytesReturned, NULL)) {
        printf("  Active Devices: %u\n", stats.ActiveDevices);
        printf("  Pending URBs: %u\n", stats.PendingUrbs);
        printf("  Total URBs Submitted: %llu\n", stats.TotalUrbsSubmitted);
        printf("  Total URBs Completed: %llu\n", stats.TotalUrbsCompleted);
        printf("  Total URBs Canceled: %llu\n", stats.TotalUrbsCanceled);
        printf("  Total Bytes In: %llu\n", stats.TotalBytesIn);
        printf("  Total Bytes Out: %llu\n", stats.TotalBytesOut);
        printf("  Total Errors: %llu\n", stats.TotalErrors);
    } else {
        printf("  FAILED: Error %lu\n", GetLastError());
    }
}
