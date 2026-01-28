package com.vusb.client.ui

import android.Manifest
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.ServiceConnection
import android.content.pm.PackageManager
import android.hardware.usb.UsbDevice
import android.os.Build
import android.os.Bundle
import android.os.IBinder
import android.view.Menu
import android.view.MenuItem
import android.view.View
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.lifecycleScope
import androidx.lifecycle.repeatOnLifecycle
import androidx.preference.PreferenceManager
import androidx.recyclerview.widget.LinearLayoutManager
import com.vusb.client.R
import com.vusb.client.databinding.ActivityMainBinding
import com.vusb.client.network.VusbNetworkClient
import com.vusb.client.service.UsbForwardingService
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.launch

/**
 * Main activity for the VUSB Client
 * 
 * Features:
 * - Display available USB devices
 * - Connect to VUSB server
 * - Attach/detach devices
 * - View connection status
 */
class MainActivity : AppCompatActivity() {
    
    companion object {
        private const val TAG = "MainActivity"
    }
    
    private lateinit var binding: ActivityMainBinding
    private lateinit var deviceAdapter: UsbDeviceAdapter
    
    private var service: UsbForwardingService? = null
    private var serviceBound = false
    
    private val serviceConnection = object : ServiceConnection {
        override fun onServiceConnected(name: ComponentName?, binder: IBinder?) {
            val localBinder = binder as UsbForwardingService.LocalBinder
            service = localBinder.getService()
            serviceBound = true
            observeServiceState()
            refreshDeviceList()
        }
        
        override fun onServiceDisconnected(name: ComponentName?) {
            service = null
            serviceBound = false
        }
    }
    
    // Permission request
    private val notificationPermissionLauncher = registerForActivityResult(
        ActivityResultContracts.RequestPermission()
    ) { isGranted ->
        if (!isGranted) {
            Toast.makeText(this, "Notification permission required for foreground service", Toast.LENGTH_LONG).show()
        }
    }
    
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)
        
        setSupportActionBar(binding.toolbar)
        
        setupViews()
        requestNotificationPermission()
        loadSettings()
    }
    
    override fun onStart() {
        super.onStart()
        // Bind to service
        Intent(this, UsbForwardingService::class.java).also { intent ->
            bindService(intent, serviceConnection, Context.BIND_AUTO_CREATE)
        }
    }
    
    override fun onStop() {
        super.onStop()
        if (serviceBound) {
            unbindService(serviceConnection)
            serviceBound = false
        }
    }
    
    override fun onCreateOptionsMenu(menu: Menu): Boolean {
        menuInflater.inflate(R.menu.main_menu, menu)
        return true
    }
    
    override fun onOptionsItemSelected(item: MenuItem): Boolean {
        return when (item.itemId) {
            R.id.action_settings -> {
                startActivity(Intent(this, SettingsActivity::class.java))
                true
            }
            R.id.action_refresh -> {
                refreshDeviceList()
                true
            }
            else -> super.onOptionsItemSelected(item)
        }
    }
    
    private fun setupViews() {
        // Setup device list
        deviceAdapter = UsbDeviceAdapter(
            onDeviceClick = { device, isAttached ->
                if (isAttached) {
                    detachDevice(device)
                } else {
                    attachDevice(device)
                }
            },
            onRequestPermission = { device ->
                service?.getUsbManager()?.requestPermission(device)
            }
        )
        
        binding.recyclerDevices.apply {
            layoutManager = LinearLayoutManager(this@MainActivity)
            adapter = deviceAdapter
        }
        
        // Connect button
        binding.btnConnect.setOnClickListener {
            if (service?.serviceState?.value is UsbForwardingService.ServiceState.Running) {
                disconnectFromServer()
            } else {
                connectToServer()
            }
        }
        
        // Refresh button
        binding.btnRefresh.setOnClickListener {
            refreshDeviceList()
        }
        
        // Attach all button
        binding.btnAttachAll.setOnClickListener {
            attachAllDevices()
        }
    }
    
    private fun loadSettings() {
        val prefs = PreferenceManager.getDefaultSharedPreferences(this)
        binding.editServerAddress.setText(prefs.getString("server_address", ""))
        binding.editServerPort.setText(prefs.getInt("server_port", 7575).toString())
    }
    
    private fun saveSettings() {
        val prefs = PreferenceManager.getDefaultSharedPreferences(this)
        prefs.edit().apply {
            putString("server_address", binding.editServerAddress.text.toString())
            putInt("server_port", binding.editServerPort.text.toString().toIntOrNull() ?: 7575)
            apply()
        }
    }
    
    private fun requestNotificationPermission() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            if (ContextCompat.checkSelfPermission(this, Manifest.permission.POST_NOTIFICATIONS) 
                != PackageManager.PERMISSION_GRANTED) {
                notificationPermissionLauncher.launch(Manifest.permission.POST_NOTIFICATIONS)
            }
        }
    }
    
    private fun observeServiceState() {
        lifecycleScope.launch {
            repeatOnLifecycle(Lifecycle.State.STARTED) {
                service?.serviceState?.collectLatest { state ->
                    updateUiState(state)
                }
            }
        }
        
        // Observe USB permission grants
        lifecycleScope.launch {
            repeatOnLifecycle(Lifecycle.State.STARTED) {
                service?.getUsbManager()?.permissionGranted?.collectLatest { result ->
                    result?.let { (device, granted) ->
                        if (granted) {
                            Toast.makeText(this@MainActivity, 
                                "Permission granted for ${device.productName ?: device.deviceName}", 
                                Toast.LENGTH_SHORT).show()
                            refreshDeviceList()
                        }
                    }
                }
            }
        }
        
        // Observe device list changes
        lifecycleScope.launch {
            repeatOnLifecycle(Lifecycle.State.STARTED) {
                service?.getUsbManager()?.availableDevices?.collectLatest { devices ->
                    updateDeviceList(devices)
                }
            }
        }
    }
    
    private fun updateUiState(state: UsbForwardingService.ServiceState) {
        when (state) {
            is UsbForwardingService.ServiceState.Stopped -> {
                binding.statusText.text = "Disconnected"
                binding.statusIndicator.setBackgroundResource(R.drawable.status_disconnected)
                binding.btnConnect.text = "Connect"
                binding.btnConnect.isEnabled = true
                binding.btnAttachAll.isEnabled = false
            }
            is UsbForwardingService.ServiceState.Starting -> {
                binding.statusText.text = "Connecting..."
                binding.statusIndicator.setBackgroundResource(R.drawable.status_connecting)
                binding.btnConnect.text = "Connecting..."
                binding.btnConnect.isEnabled = false
                binding.btnAttachAll.isEnabled = false
            }
            is UsbForwardingService.ServiceState.Running -> {
                binding.statusText.text = "Connected to ${state.serverAddress} (${state.attachedDevices} devices)"
                binding.statusIndicator.setBackgroundResource(R.drawable.status_connected)
                binding.btnConnect.text = "Disconnect"
                binding.btnConnect.isEnabled = true
                binding.btnAttachAll.isEnabled = true
            }
            is UsbForwardingService.ServiceState.Error -> {
                binding.statusText.text = "Error: ${state.message}"
                binding.statusIndicator.setBackgroundResource(R.drawable.status_error)
                binding.btnConnect.text = "Connect"
                binding.btnConnect.isEnabled = true
                binding.btnAttachAll.isEnabled = false
            }
        }
        
        refreshDeviceList()
    }
    
    private fun updateDeviceList(devices: List<UsbDevice>) {
        val attachedIds = service?.getAttachedDevices()?.map { it.deviceId }?.toSet() ?: emptySet()
        val usbManager = service?.getUsbManager()
        
        val items = devices.map { device ->
            UsbDeviceItem(
                device = device,
                isAttached = attachedIds.contains(device.deviceId),
                hasPermission = usbManager?.hasPermission(device) ?: false
            )
        }
        
        deviceAdapter.submitList(items)
        
        binding.emptyView.visibility = if (items.isEmpty()) View.VISIBLE else View.GONE
    }
    
    private fun refreshDeviceList() {
        service?.getUsbManager()?.refreshDeviceList()
    }
    
    private fun connectToServer() {
        val address = binding.editServerAddress.text.toString().trim()
        val port = binding.editServerPort.text.toString().toIntOrNull() ?: 7575
        
        if (address.isEmpty()) {
            Toast.makeText(this, "Please enter server address", Toast.LENGTH_SHORT).show()
            return
        }
        
        saveSettings()
        
        val prefs = PreferenceManager.getDefaultSharedPreferences(this)
        val autoAttach = prefs.getBoolean("auto_attach_devices", true)
        
        val intent = Intent(this, UsbForwardingService::class.java).apply {
            action = UsbForwardingService.ACTION_START
            putExtra(UsbForwardingService.EXTRA_SERVER_ADDRESS, address)
            putExtra(UsbForwardingService.EXTRA_SERVER_PORT, port)
            putExtra(UsbForwardingService.EXTRA_AUTO_ATTACH, autoAttach)
        }
        
        startForegroundService(intent)
    }
    
    private fun disconnectFromServer() {
        service?.stopForwarding()
    }
    
    private fun attachDevice(device: UsbDevice) {
        val usbManager = service?.getUsbManager() ?: return
        
        if (!usbManager.hasPermission(device)) {
            usbManager.requestPermission(device)
            return
        }
        
        lifecycleScope.launch {
            val success = service?.attachDevice(device) ?: false
            if (success) {
                Toast.makeText(this@MainActivity, "Device attached", Toast.LENGTH_SHORT).show()
            } else {
                Toast.makeText(this@MainActivity, "Failed to attach device", Toast.LENGTH_SHORT).show()
            }
            refreshDeviceList()
        }
    }
    
    private fun detachDevice(device: UsbDevice) {
        lifecycleScope.launch {
            val success = service?.detachDevice(device.deviceId) ?: false
            if (success) {
                Toast.makeText(this@MainActivity, "Device detached", Toast.LENGTH_SHORT).show()
            } else {
                Toast.makeText(this@MainActivity, "Failed to detach device", Toast.LENGTH_SHORT).show()
            }
            refreshDeviceList()
        }
    }
    
    private fun attachAllDevices() {
        lifecycleScope.launch {
            val devices = service?.getUsbManager()?.getDevices() ?: return@launch
            var attached = 0
            
            for (device in devices) {
                if (service?.attachDevice(device) == true) {
                    attached++
                }
            }
            
            Toast.makeText(this@MainActivity, "Attached $attached devices", Toast.LENGTH_SHORT).show()
            refreshDeviceList()
        }
    }
}

/**
 * Data class for USB device list items
 */
data class UsbDeviceItem(
    val device: UsbDevice,
    val isAttached: Boolean,
    val hasPermission: Boolean
)
