# Add project specific ProGuard rules here.
# By default, the flags in this file are appended to flags specified
# in the Android SDK tools.

# Keep protocol classes for serialization
-keep class com.vusb.client.protocol.** { *; }

# Keep data classes
-keepclassmembers class * {
    @kotlin.Metadata *;
}

# Keep USB related classes
-keep class android.hardware.usb.** { *; }

# Coroutines
-keepnames class kotlinx.coroutines.internal.MainDispatcherFactory {}
-keepnames class kotlinx.coroutines.CoroutineExceptionHandler {}

# Leanback
-keep class androidx.leanback.** { *; }
