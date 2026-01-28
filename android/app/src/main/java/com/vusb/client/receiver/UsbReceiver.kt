package com.vusb.client.receiver

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbManager
import android.os.Build
import android.util.Log
import androidx.preference.PreferenceManager
import com.vusb.client.service.UsbForwardingService

/**
 * USB device attach/detach receiver
 * 
 * Handles USB device connection events and can:
 * - Auto-attach newly connected devices
 * - Auto-detach disconnected devices
 */
class UsbReceiver : BroadcastReceiver() {
    
    companion object {
        private const val TAG = "UsbReceiver"
    }
    
    override fun onReceive(context: Context, intent: Intent) {
        val device = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            intent.getParcelableExtra(UsbManager.EXTRA_DEVICE, UsbDevice::class.java)
        } else {
            @Suppress("DEPRECATION")
            intent.getParcelableExtra(UsbManager.EXTRA_DEVICE)
        }
        
        when (intent.action) {
            UsbManager.ACTION_USB_DEVICE_ATTACHED -> {
                device?.let { handleDeviceAttached(context, it) }
            }
            UsbManager.ACTION_USB_DEVICE_DETACHED -> {
                device?.let { handleDeviceDetached(context, it) }
            }
        }
    }
    
    private fun handleDeviceAttached(context: Context, device: UsbDevice) {
        Log.d(TAG, "USB device attached: ${device.deviceName} (VID=${device.vendorId}, PID=${device.productId})")
        
        val prefs = PreferenceManager.getDefaultSharedPreferences(context)
        val autoAttach = prefs.getBoolean("auto_attach_new_devices", true)
        
        if (autoAttach) {
            // Tell service to attach the device
            val serviceIntent = Intent(context, UsbForwardingService::class.java).apply {
                action = UsbForwardingService.ACTION_ATTACH_DEVICE
                putExtra(UsbForwardingService.EXTRA_DEVICE_ID, device.deviceId)
            }
            
            try {
                context.startService(serviceIntent)
            } catch (e: Exception) {
                Log.w(TAG, "Could not start service to attach device", e)
            }
        }
    }
    
    private fun handleDeviceDetached(context: Context, device: UsbDevice) {
        Log.d(TAG, "USB device detached: ${device.deviceName}")
        
        // Tell service to detach the device
        val serviceIntent = Intent(context, UsbForwardingService::class.java).apply {
            action = UsbForwardingService.ACTION_DETACH_DEVICE
            putExtra(UsbForwardingService.EXTRA_DEVICE_ID, device.deviceId)
        }
        
        try {
            context.startService(serviceIntent)
        } catch (e: Exception) {
            Log.w(TAG, "Could not start service to detach device", e)
        }
    }
}
