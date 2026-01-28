/**
 * Driver installation utility
 * 
 * Command-line tool to install, uninstall, and manage the Virtual USB driver.
 */

#include <windows.h>
#include <setupapi.h>
#include <newdev.h>
#include <cfgmgr32.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "newdev.lib")

#define VUSB_HARDWARE_ID L"Root\\VirtualUSB"
#define VUSB_INF_FILE    L"vusb.inf"

void PrintUsage(void);
int InstallDriver(const wchar_t* infPath);
int UninstallDriver(void);
int StartDriver(void);
int StopDriver(void);
int QueryStatus(void);

int wmain(int argc, wchar_t* argv[])
{
    printf("Virtual USB Driver Utility v1.0\n");
    printf("================================\n\n");

    if (argc < 2) {
        PrintUsage();
        return 1;
    }

    if (_wcsicmp(argv[1], L"install") == 0) {
        const wchar_t* infPath = (argc > 2) ? argv[2] : VUSB_INF_FILE;
        return InstallDriver(infPath);
    }
    else if (_wcsicmp(argv[1], L"uninstall") == 0) {
        return UninstallDriver();
    }
    else if (_wcsicmp(argv[1], L"start") == 0) {
        return StartDriver();
    }
    else if (_wcsicmp(argv[1], L"stop") == 0) {
        return StopDriver();
    }
    else if (_wcsicmp(argv[1], L"status") == 0) {
        return QueryStatus();
    }
    else {
        PrintUsage();
        return 1;
    }
}

void PrintUsage(void)
{
    printf("Usage: vusb_install <command> [options]\n\n");
    printf("Commands:\n");
    printf("  install [inf_path]  Install the driver (requires admin)\n");
    printf("  uninstall           Uninstall the driver (requires admin)\n");
    printf("  start               Start the driver service\n");
    printf("  stop                Stop the driver service\n");
    printf("  status              Query driver status\n");
}

int InstallDriver(const wchar_t* infPath)
{
    GUID classGuid;
    wchar_t className[MAX_CLASS_NAME_LEN];
    HDEVINFO deviceInfoSet;
    SP_DEVINFO_DATA deviceInfoData;
    BOOL rebootRequired = FALSE;
    DWORD error;

    printf("Installing driver from: %ls\n", infPath);

    /* Get class info from INF */
    if (!SetupDiGetINFClassW(infPath, &classGuid, className, MAX_CLASS_NAME_LEN, NULL)) {
        error = GetLastError();
        printf("Error: Failed to get INF class (error %lu)\n", error);
        return (int)error;
    }

    printf("Class: %ls\n", className);

    /* Create device info set */
    deviceInfoSet = SetupDiCreateDeviceInfoList(&classGuid, NULL);
    if (deviceInfoSet == INVALID_HANDLE_VALUE) {
        error = GetLastError();
        printf("Error: Failed to create device info list (error %lu)\n", error);
        return (int)error;
    }

    /* Create device info */
    deviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
    if (!SetupDiCreateDeviceInfoW(deviceInfoSet, className, &classGuid, NULL, 
                                   NULL, DICD_GENERATE_ID, &deviceInfoData)) {
        error = GetLastError();
        printf("Error: Failed to create device info (error %lu)\n", error);
        SetupDiDestroyDeviceInfoList(deviceInfoSet);
        return (int)error;
    }

    /* Set hardware ID */
    if (!SetupDiSetDeviceRegistryPropertyW(deviceInfoSet, &deviceInfoData,
                                            SPDRP_HARDWAREID, 
                                            (BYTE*)VUSB_HARDWARE_ID,
                                            (DWORD)((wcslen(VUSB_HARDWARE_ID) + 2) * sizeof(wchar_t)))) {
        error = GetLastError();
        printf("Error: Failed to set hardware ID (error %lu)\n", error);
        SetupDiDestroyDeviceInfoList(deviceInfoSet);
        return (int)error;
    }

    /* Call class installer */
    if (!SetupDiCallClassInstaller(DIF_REGISTERDEVICE, deviceInfoSet, &deviceInfoData)) {
        error = GetLastError();
        printf("Error: Failed to register device (error %lu)\n", error);
        SetupDiDestroyDeviceInfoList(deviceInfoSet);
        return (int)error;
    }

    /* Update driver */
    if (!UpdateDriverForPlugAndPlayDevicesW(NULL, VUSB_HARDWARE_ID, infPath,
                                             INSTALLFLAG_FORCE, &rebootRequired)) {
        error = GetLastError();
        printf("Error: Failed to install driver (error %lu)\n", error);
        /* Try to unregister on failure */
        SetupDiCallClassInstaller(DIF_REMOVE, deviceInfoSet, &deviceInfoData);
        SetupDiDestroyDeviceInfoList(deviceInfoSet);
        return (int)error;
    }

    SetupDiDestroyDeviceInfoList(deviceInfoSet);

    printf("Driver installed successfully!\n");
    if (rebootRequired) {
        printf("Note: A reboot is required to complete installation.\n");
    }

    return 0;
}

int UninstallDriver(void)
{
    HDEVINFO deviceInfoSet;
    SP_DEVINFO_DATA deviceInfoData;
    DWORD index;
    BOOL found = FALSE;
    DWORD error;

    printf("Uninstalling driver...\n");

    /* Get device info set for all devices */
    deviceInfoSet = SetupDiGetClassDevsW(NULL, L"ROOT", NULL, DIGCF_ALLCLASSES);
    if (deviceInfoSet == INVALID_HANDLE_VALUE) {
        error = GetLastError();
        printf("Error: Failed to get device list (error %lu)\n", error);
        return (int)error;
    }

    /* Enumerate devices */
    deviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
    for (index = 0; SetupDiEnumDeviceInfo(deviceInfoSet, index, &deviceInfoData); index++) {
        wchar_t hardwareId[256];
        DWORD size;

        if (SetupDiGetDeviceRegistryPropertyW(deviceInfoSet, &deviceInfoData,
                                               SPDRP_HARDWAREID, NULL,
                                               (BYTE*)hardwareId, sizeof(hardwareId), &size)) {
            if (_wcsicmp(hardwareId, VUSB_HARDWARE_ID) == 0) {
                /* Found our device - remove it */
                if (SetupDiCallClassInstaller(DIF_REMOVE, deviceInfoSet, &deviceInfoData)) {
                    printf("Device removed.\n");
                    found = TRUE;
                } else {
                    error = GetLastError();
                    printf("Warning: Failed to remove device (error %lu)\n", error);
                }
            }
        }
    }

    SetupDiDestroyDeviceInfoList(deviceInfoSet);

    if (found) {
        printf("Driver uninstalled successfully.\n");
    } else {
        printf("No Virtual USB devices found.\n");
    }

    return 0;
}

int StartDriver(void)
{
    SC_HANDLE scManager, service;
    DWORD error;

    scManager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!scManager) {
        error = GetLastError();
        printf("Error: Failed to open service manager (error %lu)\n", error);
        return (int)error;
    }

    service = OpenServiceW(scManager, L"VirtualUSB", SERVICE_START);
    if (!service) {
        error = GetLastError();
        printf("Error: Failed to open service (error %lu)\n", error);
        CloseServiceHandle(scManager);
        return (int)error;
    }

    if (!StartService(service, 0, NULL)) {
        error = GetLastError();
        if (error == ERROR_SERVICE_ALREADY_RUNNING) {
            printf("Driver is already running.\n");
        } else {
            printf("Error: Failed to start service (error %lu)\n", error);
            CloseServiceHandle(service);
            CloseServiceHandle(scManager);
            return (int)error;
        }
    } else {
        printf("Driver started.\n");
    }

    CloseServiceHandle(service);
    CloseServiceHandle(scManager);
    return 0;
}

int StopDriver(void)
{
    SC_HANDLE scManager, service;
    SERVICE_STATUS status;
    DWORD error;

    scManager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!scManager) {
        error = GetLastError();
        printf("Error: Failed to open service manager (error %lu)\n", error);
        return (int)error;
    }

    service = OpenServiceW(scManager, L"VirtualUSB", SERVICE_STOP | SERVICE_QUERY_STATUS);
    if (!service) {
        error = GetLastError();
        printf("Error: Failed to open service (error %lu)\n", error);
        CloseServiceHandle(scManager);
        return (int)error;
    }

    if (!ControlService(service, SERVICE_CONTROL_STOP, &status)) {
        error = GetLastError();
        if (error == ERROR_SERVICE_NOT_ACTIVE) {
            printf("Driver is not running.\n");
        } else {
            printf("Error: Failed to stop service (error %lu)\n", error);
            CloseServiceHandle(service);
            CloseServiceHandle(scManager);
            return (int)error;
        }
    } else {
        printf("Driver stopped.\n");
    }

    CloseServiceHandle(service);
    CloseServiceHandle(scManager);
    return 0;
}

int QueryStatus(void)
{
    SC_HANDLE scManager, service;
    SERVICE_STATUS_PROCESS status;
    DWORD bytesNeeded;
    DWORD error;

    scManager = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT);
    if (!scManager) {
        error = GetLastError();
        printf("Error: Failed to open service manager (error %lu)\n", error);
        return (int)error;
    }

    service = OpenServiceW(scManager, L"VirtualUSB", SERVICE_QUERY_STATUS);
    if (!service) {
        error = GetLastError();
        if (error == ERROR_SERVICE_DOES_NOT_EXIST) {
            printf("Driver is not installed.\n");
        } else {
            printf("Error: Failed to open service (error %lu)\n", error);
        }
        CloseServiceHandle(scManager);
        return (error == ERROR_SERVICE_DOES_NOT_EXIST) ? 0 : (int)error;
    }

    if (!QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO,
                               (BYTE*)&status, sizeof(status), &bytesNeeded)) {
        error = GetLastError();
        printf("Error: Failed to query service status (error %lu)\n", error);
        CloseServiceHandle(service);
        CloseServiceHandle(scManager);
        return (int)error;
    }

    printf("Driver Status:\n");
    printf("  State: ");
    switch (status.dwCurrentState) {
        case SERVICE_STOPPED:          printf("Stopped\n"); break;
        case SERVICE_START_PENDING:    printf("Starting...\n"); break;
        case SERVICE_STOP_PENDING:     printf("Stopping...\n"); break;
        case SERVICE_RUNNING:          printf("Running\n"); break;
        case SERVICE_CONTINUE_PENDING: printf("Continuing...\n"); break;
        case SERVICE_PAUSE_PENDING:    printf("Pausing...\n"); break;
        case SERVICE_PAUSED:           printf("Paused\n"); break;
        default:                       printf("Unknown (%lu)\n", status.dwCurrentState); break;
    }
    printf("  PID: %lu\n", status.dwProcessId);

    CloseServiceHandle(service);
    CloseServiceHandle(scManager);
    return 0;
}
