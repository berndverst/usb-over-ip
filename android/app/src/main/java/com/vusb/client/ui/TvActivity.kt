package com.vusb.client.ui

import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.ServiceConnection
import android.hardware.usb.UsbDevice
import android.os.Bundle
import android.os.IBinder
import android.view.KeyEvent
import android.widget.Toast
import androidx.fragment.app.FragmentActivity
import androidx.leanback.app.BrowseSupportFragment
import androidx.leanback.widget.*
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.lifecycleScope
import androidx.lifecycle.repeatOnLifecycle
import androidx.preference.PreferenceManager
import com.vusb.client.R
import com.vusb.client.service.UsbForwardingService
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.launch

/**
 * Android TV (Leanback) activity
 * 
 * Provides a D-pad navigable interface for TV devices like NVIDIA Shield
 */
class TvActivity : FragmentActivity() {
    
    companion object {
        private const val TAG = "TvActivity"
        private const val HEADER_DEVICES = 0L
        private const val HEADER_CONNECTION = 1L
        private const val HEADER_SETTINGS = 2L
    }
    
    private var service: UsbForwardingService? = null
    private var serviceBound = false
    
    private lateinit var browseFragment: TvBrowseFragment
    
    private val serviceConnection = object : ServiceConnection {
        override fun onServiceConnected(name: ComponentName?, binder: IBinder?) {
            val localBinder = binder as UsbForwardingService.LocalBinder
            service = localBinder.getService()
            serviceBound = true
            browseFragment.setService(service!!)
            observeServiceState()
        }
        
        override fun onServiceDisconnected(name: ComponentName?) {
            service = null
            serviceBound = false
        }
    }
    
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_tv)
        
        browseFragment = supportFragmentManager.findFragmentById(R.id.browse_fragment) as TvBrowseFragment
    }
    
    override fun onStart() {
        super.onStart()
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
    
    private fun observeServiceState() {
        lifecycleScope.launch {
            repeatOnLifecycle(Lifecycle.State.STARTED) {
                service?.serviceState?.collectLatest { state ->
                    browseFragment.updateConnectionState(state)
                }
            }
        }
        
        lifecycleScope.launch {
            repeatOnLifecycle(Lifecycle.State.STARTED) {
                service?.getUsbManager()?.availableDevices?.collectLatest { devices ->
                    browseFragment.updateDeviceList(devices)
                }
            }
        }
    }
}

/**
 * TV Browse Fragment using Leanback library
 */
class TvBrowseFragment : BrowseSupportFragment() {
    
    private var service: UsbForwardingService? = null
    private var devicesAdapter: ArrayObjectAdapter? = null
    private var connectionAdapter: ArrayObjectAdapter? = null
    
    override fun onActivityCreated(savedInstanceState: Bundle?) {
        super.onActivityCreated(savedInstanceState)
        
        title = "VUSB Client"
        headersState = HEADERS_ENABLED
        isHeadersTransitionOnBackEnabled = true
        brandColor = resources.getColor(R.color.tv_brand_color, null)
        
        setupUI()
        setupListeners()
    }
    
    fun setService(service: UsbForwardingService) {
        this.service = service
    }
    
    private fun setupUI() {
        val rowsAdapter = ArrayObjectAdapter(ListRowPresenter())
        
        // Devices row
        devicesAdapter = ArrayObjectAdapter(DevicePresenter())
        val devicesHeader = HeaderItem(TvActivity.HEADER_DEVICES, "USB Devices")
        rowsAdapter.add(ListRow(devicesHeader, devicesAdapter!!))
        
        // Connection row
        connectionAdapter = ArrayObjectAdapter(ActionPresenter())
        val connectionHeader = HeaderItem(TvActivity.HEADER_CONNECTION, "Connection")
        connectionAdapter?.add(ActionItem("connect", "Connect to Server", "Not connected"))
        connectionAdapter?.add(ActionItem("refresh", "Refresh Devices", "Scan for USB devices"))
        rowsAdapter.add(ListRow(connectionHeader, connectionAdapter!!))
        
        // Settings row
        val settingsAdapter = ArrayObjectAdapter(ActionPresenter())
        val settingsHeader = HeaderItem(TvActivity.HEADER_SETTINGS, "Settings")
        settingsAdapter.add(ActionItem("settings", "Settings", "Configure server and options"))
        rowsAdapter.add(ListRow(settingsHeader, settingsAdapter))
        
        adapter = rowsAdapter
    }
    
    private fun setupListeners() {
        onItemViewClickedListener = OnItemViewClickedListener { _, item, _, _ ->
            when (item) {
                is DeviceItem -> handleDeviceClick(item)
                is ActionItem -> handleActionClick(item)
            }
        }
    }
    
    private fun handleDeviceClick(item: DeviceItem) {
        val context = requireContext()
        
        if (!item.hasPermission) {
            service?.getUsbManager()?.requestPermission(item.device)
            return
        }
        
        lifecycleScope.launch {
            val success = if (item.isAttached) {
                service?.detachDevice(item.device.deviceId) ?: false
            } else {
                service?.attachDevice(item.device) ?: false
            }
            
            val message = when {
                success && item.isAttached -> "Device detached"
                success -> "Device attached"
                else -> "Operation failed"
            }
            Toast.makeText(context, message, Toast.LENGTH_SHORT).show()
        }
    }
    
    private fun handleActionClick(item: ActionItem) {
        when (item.id) {
            "connect" -> toggleConnection()
            "refresh" -> service?.getUsbManager()?.refreshDeviceList()
            "settings" -> startActivity(Intent(context, SettingsActivity::class.java))
        }
    }
    
    private fun toggleConnection() {
        val context = requireContext()
        val currentState = service?.serviceState?.value
        
        if (currentState is UsbForwardingService.ServiceState.Running) {
            service?.stopForwarding()
        } else {
            val prefs = PreferenceManager.getDefaultSharedPreferences(context)
            val address = prefs.getString("server_address", "") ?: ""
            val port = prefs.getInt("server_port", 7575)
            val autoAttach = prefs.getBoolean("auto_attach_devices", true)
            
            if (address.isEmpty()) {
                Toast.makeText(context, "Please configure server address in settings", Toast.LENGTH_LONG).show()
                return
            }
            
            val intent = Intent(context, UsbForwardingService::class.java).apply {
                action = UsbForwardingService.ACTION_START
                putExtra(UsbForwardingService.EXTRA_SERVER_ADDRESS, address)
                putExtra(UsbForwardingService.EXTRA_SERVER_PORT, port)
                putExtra(UsbForwardingService.EXTRA_AUTO_ATTACH, autoAttach)
            }
            
            context.startForegroundService(intent)
        }
    }
    
    fun updateConnectionState(state: UsbForwardingService.ServiceState) {
        connectionAdapter?.let { adapter ->
            val connectItem = adapter.get(0) as? ActionItem ?: return
            
            val newItem = when (state) {
                is UsbForwardingService.ServiceState.Stopped -> {
                    ActionItem("connect", "Connect to Server", "Not connected")
                }
                is UsbForwardingService.ServiceState.Starting -> {
                    ActionItem("connect", "Connecting...", "Please wait")
                }
                is UsbForwardingService.ServiceState.Running -> {
                    ActionItem("connect", "Disconnect", "Connected to ${state.serverAddress}")
                }
                is UsbForwardingService.ServiceState.Error -> {
                    ActionItem("connect", "Connect to Server", "Error: ${state.message}")
                }
            }
            
            adapter.replace(0, newItem)
        }
    }
    
    fun updateDeviceList(devices: List<UsbDevice>) {
        devicesAdapter?.clear()
        
        val attachedIds = service?.getAttachedDevices()?.map { it.deviceId }?.toSet() ?: emptySet()
        val usbManager = service?.getUsbManager()
        
        if (devices.isEmpty()) {
            devicesAdapter?.add(DeviceItem(
                null, 
                "No USB devices found",
                "Connect a USB device to this device",
                isAttached = false,
                hasPermission = false
            ))
        } else {
            devices.forEach { device ->
                val hasPermission = usbManager?.hasPermission(device) ?: false
                val isAttached = attachedIds.contains(device.deviceId)
                
                val status = when {
                    !hasPermission -> "Permission required"
                    isAttached -> "Attached"
                    else -> "Available"
                }
                
                devicesAdapter?.add(DeviceItem(
                    device,
                    device.productName ?: "USB Device",
                    "VID: ${String.format("%04X", device.vendorId)} PID: ${String.format("%04X", device.productId)} - $status",
                    isAttached,
                    hasPermission
                ))
            }
        }
    }
}

// Data classes for TV UI
data class DeviceItem(
    val device: UsbDevice?,
    val title: String,
    val description: String,
    val isAttached: Boolean,
    val hasPermission: Boolean
)

data class ActionItem(
    val id: String,
    val title: String,
    val description: String
)

// Presenters for TV UI
class DevicePresenter : Presenter() {
    override fun onCreateViewHolder(parent: android.view.ViewGroup): ViewHolder {
        val cardView = androidx.leanback.widget.ImageCardView(parent.context).apply {
            isFocusable = true
            isFocusableInTouchMode = true
        }
        return ViewHolder(cardView)
    }
    
    override fun onBindViewHolder(viewHolder: ViewHolder, item: Any) {
        val deviceItem = item as DeviceItem
        val cardView = viewHolder.view as androidx.leanback.widget.ImageCardView
        
        cardView.titleText = deviceItem.title
        cardView.contentText = deviceItem.description
        cardView.setMainImageDimensions(120, 120)
        
        val iconRes = when {
            deviceItem.device == null -> R.drawable.ic_usb
            deviceItem.isAttached -> R.drawable.ic_check
            !deviceItem.hasPermission -> R.drawable.ic_lock
            else -> R.drawable.ic_usb
        }
        cardView.mainImageView.setImageResource(iconRes)
    }
    
    override fun onUnbindViewHolder(viewHolder: ViewHolder) {}
}

class ActionPresenter : Presenter() {
    override fun onCreateViewHolder(parent: android.view.ViewGroup): ViewHolder {
        val cardView = androidx.leanback.widget.ImageCardView(parent.context).apply {
            isFocusable = true
            isFocusableInTouchMode = true
        }
        return ViewHolder(cardView)
    }
    
    override fun onBindViewHolder(viewHolder: ViewHolder, item: Any) {
        val actionItem = item as ActionItem
        val cardView = viewHolder.view as androidx.leanback.widget.ImageCardView
        
        cardView.titleText = actionItem.title
        cardView.contentText = actionItem.description
        cardView.setMainImageDimensions(120, 120)
        
        val iconRes = when (actionItem.id) {
            "connect" -> R.drawable.ic_cloud
            "refresh" -> R.drawable.ic_refresh
            "settings" -> R.drawable.ic_settings
            else -> R.drawable.ic_usb
        }
        cardView.mainImageView.setImageResource(iconRes)
    }
    
    override fun onUnbindViewHolder(viewHolder: ViewHolder) {}
}
