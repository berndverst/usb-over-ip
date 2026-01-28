package com.vusb.client.ui

import android.hardware.usb.UsbDevice
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import android.widget.ImageView
import android.widget.TextView
import androidx.recyclerview.widget.DiffUtil
import androidx.recyclerview.widget.ListAdapter
import androidx.recyclerview.widget.RecyclerView
import com.vusb.client.R

/**
 * RecyclerView adapter for USB devices
 */
class UsbDeviceAdapter(
    private val onDeviceClick: (UsbDevice, Boolean) -> Unit,
    private val onRequestPermission: (UsbDevice) -> Unit
) : ListAdapter<UsbDeviceItem, UsbDeviceAdapter.DeviceViewHolder>(DeviceDiffCallback()) {
    
    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): DeviceViewHolder {
        val view = LayoutInflater.from(parent.context)
            .inflate(R.layout.item_usb_device, parent, false)
        return DeviceViewHolder(view)
    }
    
    override fun onBindViewHolder(holder: DeviceViewHolder, position: Int) {
        holder.bind(getItem(position))
    }
    
    inner class DeviceViewHolder(itemView: View) : RecyclerView.ViewHolder(itemView) {
        private val iconView: ImageView = itemView.findViewById(R.id.deviceIcon)
        private val nameText: TextView = itemView.findViewById(R.id.deviceName)
        private val infoText: TextView = itemView.findViewById(R.id.deviceInfo)
        private val statusText: TextView = itemView.findViewById(R.id.deviceStatus)
        private val actionButton: Button = itemView.findViewById(R.id.btnAction)
        
        fun bind(item: UsbDeviceItem) {
            val device = item.device
            
            // Device name
            nameText.text = device.productName ?: "USB Device"
            
            // Device info
            val vidPid = String.format("VID: %04X  PID: %04X", device.vendorId, device.productId)
            val classInfo = "Class: ${getDeviceClassName(device.deviceClass)}"
            infoText.text = "$vidPid\n$classInfo"
            
            // Icon based on device class
            iconView.setImageResource(getDeviceIcon(device.deviceClass))
            
            // Status and action
            when {
                !item.hasPermission -> {
                    statusText.text = "No permission"
                    statusText.setTextColor(itemView.context.getColor(R.color.status_warning))
                    actionButton.text = "Grant"
                    actionButton.setOnClickListener { onRequestPermission(device) }
                }
                item.isAttached -> {
                    statusText.text = "Attached"
                    statusText.setTextColor(itemView.context.getColor(R.color.status_connected))
                    actionButton.text = "Detach"
                    actionButton.setOnClickListener { onDeviceClick(device, true) }
                }
                else -> {
                    statusText.text = "Available"
                    statusText.setTextColor(itemView.context.getColor(R.color.status_available))
                    actionButton.text = "Attach"
                    actionButton.setOnClickListener { onDeviceClick(device, false) }
                }
            }
        }
        
        private fun getDeviceClassName(deviceClass: Int): String {
            return when (deviceClass) {
                0 -> "Composite"
                1 -> "Audio"
                2 -> "CDC/Communications"
                3 -> "HID"
                5 -> "Physical"
                6 -> "Image"
                7 -> "Printer"
                8 -> "Mass Storage"
                9 -> "Hub"
                10 -> "CDC-Data"
                11 -> "Smart Card"
                13 -> "Content Security"
                14 -> "Video"
                15 -> "Personal Healthcare"
                16 -> "Audio/Video"
                220 -> "Diagnostic"
                224 -> "Wireless Controller"
                239 -> "Miscellaneous"
                254 -> "Application Specific"
                255 -> "Vendor Specific"
                else -> "Unknown ($deviceClass)"
            }
        }
        
        private fun getDeviceIcon(deviceClass: Int): Int {
            return when (deviceClass) {
                3 -> R.drawable.ic_keyboard // HID
                8 -> R.drawable.ic_storage // Mass Storage
                9 -> R.drawable.ic_hub // Hub
                14 -> R.drawable.ic_videocam // Video
                1, 16 -> R.drawable.ic_headset // Audio
                else -> R.drawable.ic_usb // Default
            }
        }
    }
    
    class DeviceDiffCallback : DiffUtil.ItemCallback<UsbDeviceItem>() {
        override fun areItemsTheSame(oldItem: UsbDeviceItem, newItem: UsbDeviceItem): Boolean {
            return oldItem.device.deviceId == newItem.device.deviceId
        }
        
        override fun areContentsTheSame(oldItem: UsbDeviceItem, newItem: UsbDeviceItem): Boolean {
            return oldItem == newItem
        }
    }
}
