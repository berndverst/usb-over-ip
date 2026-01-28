/**
 * Virtual USB Controller Driver
 * 
 * Main driver entry point and initialization for the virtual USB host controller.
 * This driver creates a virtual USB root hub that can have virtual devices
 * plugged in via IOCTL from user-mode applications.
 */

#include <ntddk.h>
#include <wdf.h>
#include <usb.h>
#include <usbioctl.h>
#include <initguid.h>

#include "vusb_driver.h"
#include "../protocol/vusb_ioctl.h"

/* Driver globals */
WDFDRIVER g_Driver = NULL;

/* Forward declarations */
EVT_WDF_DRIVER_DEVICE_ADD       VusbEvtDeviceAdd;
EVT_WDF_DRIVER_UNLOAD           VusbEvtDriverUnload;
EVT_WDF_OBJECT_CONTEXT_CLEANUP  VusbEvtDriverContextCleanup;
EVT_WDF_DEVICE_PREPARE_HARDWARE VusbEvtDevicePrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE VusbEvtDeviceReleaseHardware;
EVT_WDF_DEVICE_D0_ENTRY         VusbEvtDeviceD0Entry;
EVT_WDF_DEVICE_D0_EXIT          VusbEvtDeviceD0Exit;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL VusbEvtIoDeviceControl;

/**
 * DriverEntry - Driver entry point
 */
NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT  DriverObject,
    _In_ PUNICODE_STRING RegistryPath
)
{
    NTSTATUS status;
    WDF_DRIVER_CONFIG config;
    WDF_OBJECT_ATTRIBUTES attributes;

    /* Initialize tracing (if using WPP) */
    WPP_INIT_TRACING(DriverObject, RegistryPath);

    KdPrint(("VirtualUSB: DriverEntry - Start\n"));

    /* Initialize driver configuration */
    WDF_DRIVER_CONFIG_INIT(&config, VusbEvtDeviceAdd);
    config.EvtDriverUnload = VusbEvtDriverUnload;

    /* Set driver context cleanup callback */
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.EvtCleanupCallback = VusbEvtDriverContextCleanup;

    /* Create the driver object */
    status = WdfDriverCreate(
        DriverObject,
        RegistryPath,
        &attributes,
        &config,
        &g_Driver
    );

    if (!NT_SUCCESS(status)) {
        KdPrint(("VirtualUSB: WdfDriverCreate failed - 0x%x\n", status));
        WPP_CLEANUP(DriverObject);
        return status;
    }

    KdPrint(("VirtualUSB: DriverEntry - Success\n"));
    return STATUS_SUCCESS;
}

/**
 * VusbEvtDriverUnload - Driver unload callback
 */
VOID
VusbEvtDriverUnload(
    _In_ WDFDRIVER Driver
)
{
    UNREFERENCED_PARAMETER(Driver);

    KdPrint(("VirtualUSB: Driver unloading\n"));
    
    /* Cleanup tracing */
    WPP_CLEANUP(WdfDriverWdmGetDriverObject(Driver));
}

/**
 * VusbEvtDriverContextCleanup - Driver context cleanup callback
 */
VOID
VusbEvtDriverContextCleanup(
    _In_ WDFOBJECT Driver
)
{
    UNREFERENCED_PARAMETER(Driver);
    KdPrint(("VirtualUSB: Driver context cleanup\n"));
}

/**
 * VusbEvtDeviceAdd - Called when PnP manager detects a new device
 */
NTSTATUS
VusbEvtDeviceAdd(
    _In_ WDFDRIVER       Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit
)
{
    NTSTATUS status;
    WDFDEVICE device;
    PVUSB_DEVICE_CONTEXT deviceContext;
    WDF_OBJECT_ATTRIBUTES deviceAttributes;
    WDF_PNPPOWER_EVENT_CALLBACKS pnpPowerCallbacks;
    WDF_IO_QUEUE_CONFIG queueConfig;
    WDFQUEUE queue;
    DECLARE_CONST_UNICODE_STRING(deviceName, VUSB_DEVICE_NAME);
    DECLARE_CONST_UNICODE_STRING(symbolicLink, VUSB_SYMBOLIC_NAME);

    UNREFERENCED_PARAMETER(Driver);

    KdPrint(("VirtualUSB: EvtDeviceAdd\n"));

    /* Set device type */
    WdfDeviceInitSetDeviceType(DeviceInit, FILE_DEVICE_BUS_EXTENDER);

    /* Set exclusive access */
    WdfDeviceInitSetExclusive(DeviceInit, FALSE);

    /* Set I/O type to buffered */
    WdfDeviceInitSetIoType(DeviceInit, WdfDeviceIoBuffered);

    /* Assign device name */
    status = WdfDeviceInitAssignName(DeviceInit, &deviceName);
    if (!NT_SUCCESS(status)) {
        KdPrint(("VirtualUSB: WdfDeviceInitAssignName failed - 0x%x\n", status));
        return status;
    }

    /* Set up PnP and power callbacks */
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpPowerCallbacks);
    pnpPowerCallbacks.EvtDevicePrepareHardware = VusbEvtDevicePrepareHardware;
    pnpPowerCallbacks.EvtDeviceReleaseHardware = VusbEvtDeviceReleaseHardware;
    pnpPowerCallbacks.EvtDeviceD0Entry = VusbEvtDeviceD0Entry;
    pnpPowerCallbacks.EvtDeviceD0Exit = VusbEvtDeviceD0Exit;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpPowerCallbacks);

    /* Specify device context type */
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&deviceAttributes, VUSB_DEVICE_CONTEXT);
    deviceAttributes.EvtCleanupCallback = VusbEvtDeviceContextCleanup;

    /* Create the device */
    status = WdfDeviceCreate(&DeviceInit, &deviceAttributes, &device);
    if (!NT_SUCCESS(status)) {
        KdPrint(("VirtualUSB: WdfDeviceCreate failed - 0x%x\n", status));
        return status;
    }

    /* Get device context */
    deviceContext = VusbGetDeviceContext(device);
    RtlZeroMemory(deviceContext, sizeof(VUSB_DEVICE_CONTEXT));
    deviceContext->Device = device;
    deviceContext->MaxDevices = VUSB_MAX_DEVICES;
    KeInitializeSpinLock(&deviceContext->DeviceListLock);
    KeInitializeSpinLock(&deviceContext->UrbQueueLock);
    InitializeListHead(&deviceContext->PendingUrbList);

    /* Create symbolic link */
    status = WdfDeviceCreateSymbolicLink(device, &symbolicLink);
    if (!NT_SUCCESS(status)) {
        KdPrint(("VirtualUSB: WdfDeviceCreateSymbolicLink failed - 0x%x\n", status));
        return status;
    }

    /* Create device interface */
    status = WdfDeviceCreateDeviceInterface(
        device,
        &GUID_DEVINTERFACE_VUSB_CONTROLLER,
        NULL
    );
    if (!NT_SUCCESS(status)) {
        KdPrint(("VirtualUSB: WdfDeviceCreateDeviceInterface failed - 0x%x\n", status));
        return status;
    }

    /* Create default I/O queue for device control requests */
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig, WdfIoQueueDispatchSequential);
    queueConfig.EvtIoDeviceControl = VusbEvtIoDeviceControl;

    status = WdfIoQueueCreate(device, &queueConfig, WDF_NO_OBJECT_ATTRIBUTES, &queue);
    if (!NT_SUCCESS(status)) {
        KdPrint(("VirtualUSB: WdfIoQueueCreate failed - 0x%x\n", status));
        return status;
    }

    /* Create manual queue for pending URB requests */
    WDF_IO_QUEUE_CONFIG_INIT(&queueConfig, WdfIoQueueDispatchManual);
    status = WdfIoQueueCreate(device, &queueConfig, WDF_NO_OBJECT_ATTRIBUTES, 
                              &deviceContext->PendingUrbQueue);
    if (!NT_SUCCESS(status)) {
        KdPrint(("VirtualUSB: Failed to create pending URB queue - 0x%x\n", status));
        return status;
    }

    KdPrint(("VirtualUSB: Device created successfully\n"));
    return STATUS_SUCCESS;
}

/**
 * VusbEvtDeviceContextCleanup - Device context cleanup
 */
VOID
VusbEvtDeviceContextCleanup(
    _In_ WDFOBJECT Device
)
{
    PVUSB_DEVICE_CONTEXT deviceContext;

    KdPrint(("VirtualUSB: Device context cleanup\n"));

    deviceContext = VusbGetDeviceContext(Device);

    /* Cleanup any remaining virtual devices */
    VusbCleanupAllDevices(deviceContext);
}

/**
 * VusbEvtDevicePrepareHardware - Prepare hardware callback
 */
NTSTATUS
VusbEvtDevicePrepareHardware(
    _In_ WDFDEVICE Device,
    _In_ WDFCMRESLIST ResourcesRaw,
    _In_ WDFCMRESLIST ResourcesTranslated
)
{
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(ResourcesRaw);
    UNREFERENCED_PARAMETER(ResourcesTranslated);

    KdPrint(("VirtualUSB: PrepareHardware\n"));
    return STATUS_SUCCESS;
}

/**
 * VusbEvtDeviceReleaseHardware - Release hardware callback
 */
NTSTATUS
VusbEvtDeviceReleaseHardware(
    _In_ WDFDEVICE Device,
    _In_ WDFCMRESLIST ResourcesTranslated
)
{
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(ResourcesTranslated);

    KdPrint(("VirtualUSB: ReleaseHardware\n"));
    return STATUS_SUCCESS;
}

/**
 * VusbEvtDeviceD0Entry - Device entering D0 (working) state
 */
NTSTATUS
VusbEvtDeviceD0Entry(
    _In_ WDFDEVICE Device,
    _In_ WDF_POWER_DEVICE_STATE PreviousState
)
{
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(PreviousState);

    KdPrint(("VirtualUSB: D0Entry from state %d\n", PreviousState));
    return STATUS_SUCCESS;
}

/**
 * VusbEvtDeviceD0Exit - Device leaving D0 state
 */
NTSTATUS
VusbEvtDeviceD0Exit(
    _In_ WDFDEVICE Device,
    _In_ WDF_POWER_DEVICE_STATE TargetState
)
{
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(TargetState);

    KdPrint(("VirtualUSB: D0Exit to state %d\n", TargetState));
    return STATUS_SUCCESS;
}

/**
 * VusbEvtIoDeviceControl - Handle IOCTL requests
 */
VOID
VusbEvtIoDeviceControl(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode
)
{
    NTSTATUS status = STATUS_SUCCESS;
    WDFDEVICE device;
    PVUSB_DEVICE_CONTEXT deviceContext;
    size_t bytesReturned = 0;

    device = WdfIoQueueGetDevice(Queue);
    deviceContext = VusbGetDeviceContext(device);

    KdPrint(("VirtualUSB: IoDeviceControl - Code 0x%x\n", IoControlCode));

    switch (IoControlCode) {
    case IOCTL_VUSB_GET_VERSION:
        status = VusbHandleGetVersion(deviceContext, Request, OutputBufferLength, &bytesReturned);
        break;

    case IOCTL_VUSB_PLUGIN_DEVICE:
        status = VusbHandlePluginDevice(deviceContext, Request, InputBufferLength, 
                                        OutputBufferLength, &bytesReturned);
        break;

    case IOCTL_VUSB_UNPLUG_DEVICE:
        status = VusbHandleUnplugDevice(deviceContext, Request, InputBufferLength);
        break;

    case IOCTL_VUSB_GET_DEVICE_LIST:
        status = VusbHandleGetDeviceList(deviceContext, Request, OutputBufferLength, &bytesReturned);
        break;

    case IOCTL_VUSB_GET_PENDING_URB:
        status = VusbHandleGetPendingUrb(deviceContext, Request, OutputBufferLength, &bytesReturned);
        if (status == STATUS_PENDING) {
            return; /* Request will be completed later */
        }
        break;

    case IOCTL_VUSB_COMPLETE_URB:
        status = VusbHandleCompleteUrb(deviceContext, Request, InputBufferLength);
        break;

    case IOCTL_VUSB_CANCEL_URB:
        status = VusbHandleCancelUrb(deviceContext, Request, InputBufferLength);
        break;

    case IOCTL_VUSB_GET_STATISTICS:
        status = VusbHandleGetStatistics(deviceContext, Request, OutputBufferLength, &bytesReturned);
        break;

    case IOCTL_VUSB_RESET_DEVICE:
        status = VusbHandleResetDevice(deviceContext, Request, InputBufferLength);
        break;

    default:
        KdPrint(("VirtualUSB: Unknown IOCTL 0x%x\n", IoControlCode));
        status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

    WdfRequestCompleteWithInformation(Request, status, bytesReturned);
}
