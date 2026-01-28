package com.vusb.client.usb

import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbEndpoint
import android.util.Log
import com.vusb.client.protocol.UrbComplete
import com.vusb.client.protocol.UrbSubmit
import com.vusb.client.protocol.VusbProtocol

/**
 * URB Handler - processes USB Request Blocks received from the server
 * 
 * This class handles:
 * - Parsing URB requests
 * - Routing to appropriate transfer handlers
 * - Building completion responses
 */
class UrbHandler(private val usbManager: UsbDeviceManager) {
    
    companion object {
        private const val TAG = "UrbHandler"
    }
    
    /**
     * Process an incoming URB submit request
     * 
     * @param urbSubmit The URB submit request from the server
     * @return UrbComplete response to send back
     */
    fun processUrb(urbSubmit: UrbSubmit): UrbComplete {
        Log.d(TAG, "Processing URB: id=${urbSubmit.urbId}, function=0x${urbSubmit.function.toString(16)}, " +
                   "endpoint=0x${urbSubmit.endpoint.toString(16)}, direction=${urbSubmit.direction}")
        
        val device = usbManager.getDevice(urbSubmit.deviceId)
        if (device == null) {
            Log.e(TAG, "Device not found: ${urbSubmit.deviceId}")
            return UrbComplete(
                urbId = urbSubmit.urbId,
                status = VusbProtocol.UsbdStatus.ERROR_BUSY,
                actualLength = 0
            )
        }
        
        if (!usbManager.isDeviceOpen(urbSubmit.deviceId)) {
            Log.e(TAG, "Device not open: ${urbSubmit.deviceId}")
            return UrbComplete(
                urbId = urbSubmit.urbId,
                status = VusbProtocol.UsbdStatus.ERROR_BUSY,
                actualLength = 0
            )
        }
        
        return when (urbSubmit.function) {
            VusbProtocol.UrbFunction.CONTROL_TRANSFER,
            VusbProtocol.UrbFunction.GET_DESCRIPTOR_FROM_DEVICE -> {
                handleControlTransfer(device, urbSubmit)
            }
            
            VusbProtocol.UrbFunction.BULK_OR_INTERRUPT_TRANSFER -> {
                handleBulkOrInterruptTransfer(device, urbSubmit)
            }
            
            VusbProtocol.UrbFunction.SELECT_CONFIGURATION -> {
                handleSelectConfiguration(device, urbSubmit)
            }
            
            VusbProtocol.UrbFunction.SELECT_INTERFACE -> {
                handleSelectInterface(device, urbSubmit)
            }
            
            VusbProtocol.UrbFunction.SYNC_RESET_PIPE,
            VusbProtocol.UrbFunction.SYNC_CLEAR_STALL -> {
                handleClearStall(device, urbSubmit)
            }
            
            else -> {
                Log.w(TAG, "Unhandled URB function: 0x${urbSubmit.function.toString(16)}")
                UrbComplete(
                    urbId = urbSubmit.urbId,
                    status = VusbProtocol.UsbdStatus.SUCCESS,
                    actualLength = 0
                )
            }
        }
    }
    
    /**
     * Handle control transfers
     */
    private fun handleControlTransfer(device: UsbDevice, urbSubmit: UrbSubmit): UrbComplete {
        val setup = urbSubmit.setupPacket
        if (setup.size < 8) {
            return UrbComplete(
                urbId = urbSubmit.urbId,
                status = VusbProtocol.UsbdStatus.STALL_PID,
                actualLength = 0
            )
        }
        
        // Parse setup packet
        val bmRequestType = setup[0].toInt() and 0xFF
        val bRequest = setup[1].toInt() and 0xFF
        val wValue = (setup[2].toInt() and 0xFF) or ((setup[3].toInt() and 0xFF) shl 8)
        val wIndex = (setup[4].toInt() and 0xFF) or ((setup[5].toInt() and 0xFF) shl 8)
        val wLength = (setup[6].toInt() and 0xFF) or ((setup[7].toInt() and 0xFF) shl 8)
        
        Log.d(TAG, "Control transfer: bmRequestType=0x${bmRequestType.toString(16)}, " +
                   "bRequest=0x${bRequest.toString(16)}, wValue=0x${wValue.toString(16)}, " +
                   "wIndex=$wIndex, wLength=$wLength")
        
        val isIn = (bmRequestType and 0x80) != 0
        val buffer = if (isIn) {
            ByteArray(wLength.coerceAtMost(4096))
        } else {
            urbSubmit.data
        }
        
        val result = usbManager.controlTransfer(
            urbSubmit.deviceId,
            bmRequestType,
            bRequest,
            wValue,
            wIndex,
            buffer,
            buffer.size,
            5000
        )
        
        return if (result >= 0) {
            Log.d(TAG, "Control transfer success: $result bytes")
            UrbComplete(
                urbId = urbSubmit.urbId,
                status = VusbProtocol.UsbdStatus.SUCCESS,
                actualLength = result,
                data = if (isIn && result > 0) buffer.copyOf(result) else ByteArray(0)
            )
        } else {
            Log.e(TAG, "Control transfer failed: $result")
            UrbComplete(
                urbId = urbSubmit.urbId,
                status = VusbProtocol.UsbdStatus.STALL_PID,
                actualLength = 0
            )
        }
    }
    
    /**
     * Handle bulk or interrupt transfers
     */
    private fun handleBulkOrInterruptTransfer(device: UsbDevice, urbSubmit: UrbSubmit): UrbComplete {
        val endpointAddress = urbSubmit.endpoint.toInt() and 0xFF
        val endpoint = usbManager.findEndpoint(device, endpointAddress)
        
        if (endpoint == null) {
            Log.e(TAG, "Endpoint not found: 0x${endpointAddress.toString(16)}")
            return UrbComplete(
                urbId = urbSubmit.urbId,
                status = VusbProtocol.UsbdStatus.STALL_PID,
                actualLength = 0
            )
        }
        
        val isIn = urbSubmit.direction == VusbProtocol.TRANSFER_DIRECTION_IN
        val buffer = if (isIn) {
            ByteArray(urbSubmit.bufferLength.coerceAtMost(65536))
        } else {
            urbSubmit.data
        }
        
        Log.d(TAG, "Bulk/Interrupt transfer: endpoint=0x${endpointAddress.toString(16)}, " +
                   "direction=${if (isIn) "IN" else "OUT"}, length=${buffer.size}")
        
        val result = usbManager.bulkTransfer(
            urbSubmit.deviceId,
            endpoint,
            buffer,
            buffer.size,
            5000
        )
        
        return if (result >= 0) {
            Log.d(TAG, "Bulk/Interrupt transfer success: $result bytes")
            UrbComplete(
                urbId = urbSubmit.urbId,
                status = VusbProtocol.UsbdStatus.SUCCESS,
                actualLength = result,
                data = if (isIn && result > 0) buffer.copyOf(result) else ByteArray(0)
            )
        } else {
            Log.e(TAG, "Bulk/Interrupt transfer failed: $result")
            UrbComplete(
                urbId = urbSubmit.urbId,
                status = if (result == -1) VusbProtocol.UsbdStatus.ERROR_BUSY 
                         else VusbProtocol.UsbdStatus.STALL_PID,
                actualLength = 0
            )
        }
    }
    
    /**
     * Handle SELECT_CONFIGURATION request
     */
    private fun handleSelectConfiguration(device: UsbDevice, urbSubmit: UrbSubmit): UrbComplete {
        Log.d(TAG, "Select configuration request")
        // Android handles configuration selection automatically
        // Just return success
        return UrbComplete(
            urbId = urbSubmit.urbId,
            status = VusbProtocol.UsbdStatus.SUCCESS,
            actualLength = 0
        )
    }
    
    /**
     * Handle SELECT_INTERFACE request
     */
    private fun handleSelectInterface(device: UsbDevice, urbSubmit: UrbSubmit): UrbComplete {
        Log.d(TAG, "Select interface request")
        // Android handles interface selection through claim
        // Just return success
        return UrbComplete(
            urbId = urbSubmit.urbId,
            status = VusbProtocol.UsbdStatus.SUCCESS,
            actualLength = 0
        )
    }
    
    /**
     * Handle CLEAR_STALL / RESET_PIPE request
     */
    private fun handleClearStall(device: UsbDevice, urbSubmit: UrbSubmit): UrbComplete {
        val endpointAddress = urbSubmit.endpoint.toInt() and 0xFF
        Log.d(TAG, "Clear stall request for endpoint 0x${endpointAddress.toString(16)}")
        
        // Send CLEAR_FEATURE request to clear ENDPOINT_HALT
        val result = usbManager.controlTransfer(
            urbSubmit.deviceId,
            0x02, // USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_ENDPOINT
            0x01, // CLEAR_FEATURE
            0x00, // ENDPOINT_HALT
            endpointAddress,
            null,
            0,
            1000
        )
        
        return UrbComplete(
            urbId = urbSubmit.urbId,
            status = if (result >= 0) VusbProtocol.UsbdStatus.SUCCESS 
                     else VusbProtocol.UsbdStatus.STALL_PID,
            actualLength = 0
        )
    }
}
