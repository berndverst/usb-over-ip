package com.vusb.client

import android.app.Application
import android.util.Log

/**
 * Application class for VUSB Client
 * 
 * Handles global application initialization
 */
class VusbApplication : Application() {
    
    companion object {
        private const val TAG = "VusbApplication"
        
        @Volatile
        private var instance: VusbApplication? = null
        
        fun getInstance(): VusbApplication {
            return instance ?: throw IllegalStateException("Application not initialized")
        }
    }
    
    override fun onCreate() {
        super.onCreate()
        instance = this
        Log.d(TAG, "VUSB Client Application started")
    }
}
