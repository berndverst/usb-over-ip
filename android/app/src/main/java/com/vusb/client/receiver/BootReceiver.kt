package com.vusb.client.receiver

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.util.Log
import androidx.preference.PreferenceManager
import com.vusb.client.service.UsbForwardingService

/**
 * Boot receiver - starts the forwarding service on device boot if configured
 * 
 * This enables the VUSB client to start automatically on:
 * - Android phones/tablets
 * - Android TV devices (NVIDIA Shield, etc.)
 */
class BootReceiver : BroadcastReceiver() {
    
    companion object {
        private const val TAG = "BootReceiver"
    }
    
    override fun onReceive(context: Context, intent: Intent) {
        if (intent.action in listOf(
            Intent.ACTION_BOOT_COMPLETED,
            "android.intent.action.QUICKBOOT_POWERON",
            Intent.ACTION_LOCKED_BOOT_COMPLETED
        )) {
            Log.d(TAG, "Boot completed, checking auto-start settings")
            
            val prefs = PreferenceManager.getDefaultSharedPreferences(context)
            val autoStart = prefs.getBoolean("auto_start_on_boot", false)
            val serverAddress = prefs.getString("server_address", "") ?: ""
            val serverPort = prefs.getInt("server_port", 7575)
            val autoAttach = prefs.getBoolean("auto_attach_devices", true)
            
            if (autoStart && serverAddress.isNotEmpty()) {
                Log.d(TAG, "Auto-starting service to $serverAddress:$serverPort")
                
                val serviceIntent = Intent(context, UsbForwardingService::class.java).apply {
                    action = UsbForwardingService.ACTION_START
                    putExtra(UsbForwardingService.EXTRA_SERVER_ADDRESS, serverAddress)
                    putExtra(UsbForwardingService.EXTRA_SERVER_PORT, serverPort)
                    putExtra(UsbForwardingService.EXTRA_AUTO_ATTACH, autoAttach)
                }
                
                context.startForegroundService(serviceIntent)
            } else {
                Log.d(TAG, "Auto-start disabled or no server configured")
            }
        }
    }
}
