package org.mavlink.qgroundcontrol;

import android.app.PendingIntent;
import android.bluetooth.BluetoothDevice;
import android.content.*;
import android.hardware.usb.*;
import android.os.Build;
import android.os.Process;
import android.os.SystemClock;

import com.hoho.android.usbserial.driver.*;
import com.hoho.android.usbserial.util.*;

import java.io.IOException;
import java.util.*;
import java.util.concurrent.*;

public class QGCUsbSerialManager {
    // QGC_CUSTOM_ANDROID_USB_SERIAL_MANAGER_V1
    private static final String TAG = "QGCUsbSerial-Custom";
    private static final String CUSTOM_MANAGER_VERSION = "custom-usb-v1";
    private static final String ACTION_USB_PERMISSION = "org.mavlink.qgroundcontrol.action.USB_PERMISSION";
    private static final int BAD_DEVICE_ID = 0;
    private static final int READ_BUF_SIZE = 2048;
    private static final int USB_CDC_SUBCLASS_ACM = 2;
    private static final long USB_PERMISSION_RETRY_MS = 15000L;
    private static final Object MANAGER_LOCK = new Object();

    private static UsbManager usbManager;
    private static PendingIntent usbPermissionIntent;
    private static UsbSerialProber usbSerialProber;
    private static Context applicationContext;
    private static boolean receiverRegistered;
    private static String lastUsbTopologySignature;
    private static int lastReportedSerialDeviceCount = -1;

    // Discovery state and open-port resources deliberately have separate lifetimes. A port can be
    // closed and reopened while its driver remains discoverable.
    private static final List<UsbSerialDriver> drivers = new CopyOnWriteArrayList<>();
    private static final ConcurrentHashMap<Integer, UsbDeviceResources> deviceResourcesMap = new ConcurrentHashMap<>();
    private static final ConcurrentHashMap<Integer, Long> pendingPermissionRequests = new ConcurrentHashMap<>();
    private static final Set<Integer> deniedPermissionDeviceIds = Collections.newSetFromMap(new ConcurrentHashMap<>());

    // Native methods
    // Kept for JNI registration compatibility. Custom detach handling deliberately does not call
    // it with a raw QSerialPortPrivate pointer from an Android thread; QGC's SerialWorker detects
    // the removed port through availableDevicesInfo() and closes it on its owning Qt thread.
    private static native void nativeDeviceHasDisconnected(final long classPtr);
    public static native void nativeDeviceException(final long classPtr, final String message);
    public static native void nativeDeviceNewData(final long classPtr, final byte[] data);
    private static native void nativeUpdateAvailableJoysticks();

    /**
     * Encapsulates all resources associated with a USB device.
     */
    private static class UsbDeviceResources {
        UsbSerialDriver driver;
        SerialInputOutputManager ioManager;
        QGCSerialListener listener;
        int fileDescriptor;
        long classPtr;

        UsbDeviceResources(UsbSerialDriver driver) {
            this.driver = driver;
            this.fileDescriptor = -1;
        }
    }

    /**
     * Initializes the UsbSerialManager. Should be called once, typically from QGCActivity.onCreate().
     *
     * @param context The application context.
     */
    public static void initialize(Context context) {
        if (context == null) {
            QGCLogger.e(TAG, "initialize called with a null Context");
            return;
        }

        synchronized (MANAGER_LOCK) {
            Context appContext = context.getApplicationContext();
            applicationContext = (appContext != null) ? appContext : context;

            usbManager = (UsbManager) applicationContext.getSystemService(Context.USB_SERVICE);
            if (usbManager == null) {
                QGCLogger.e(TAG, "Failed to get UsbManager");
                return;
            }

            usbSerialProber = UsbSerialProber.getDefaultProber();
            setupUsbPermissionIntent(applicationContext);
            registerUsbReceiver(applicationContext);
            updateCurrentDriversLocked();
            QGCLogger.i(TAG, "Initialized " + CUSTOM_MANAGER_VERSION + " with " + drivers.size() + " serial driver(s)");
        }
    }

    /**
     * Cleans up resources by unregistering the BroadcastReceiver.
     * Should be called when the manager is no longer needed, typically from QGCActivity.onDestroy().
     */
    public static void cleanup(Context context) {
        synchronized (MANAGER_LOCK) {
            for (Integer deviceId : new ArrayList<>(deviceResourcesMap.keySet())) {
                releaseDeviceResources(deviceId);
            }

            if (receiverRegistered && applicationContext != null) {
                try {
                    applicationContext.unregisterReceiver(usbReceiver);
                    QGCLogger.i(TAG, "BroadcastReceiver unregistered successfully.");
                } catch (final IllegalArgumentException e) {
                    QGCLogger.w(TAG, "Receiver was already unregistered: " + e.getMessage());
                }
            }

            receiverRegistered = false;
            pendingPermissionRequests.clear();
            deniedPermissionDeviceIds.clear();
            deviceResourcesMap.clear();
            drivers.clear();
            usbPermissionIntent = null;
            usbSerialProber = null;
            usbManager = null;
            applicationContext = null;
            lastUsbTopologySignature = null;
            lastReportedSerialDeviceCount = -1;
            QGCLogger.i(TAG, "Cleaned up " + CUSTOM_MANAGER_VERSION);
        }
    }

    /**
     * Sets up the PendingIntent for USB permission requests.
     *
     * @param context The application context.
     */
    private static void setupUsbPermissionIntent(Context context) {
        int intentFlags = PendingIntent.FLAG_UPDATE_CURRENT;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            intentFlags |= PendingIntent.FLAG_IMMUTABLE;
        }
        Intent permissionIntent = new Intent(ACTION_USB_PERMISSION).setPackage(context.getPackageName());
        usbPermissionIntent = PendingIntent.getBroadcast(context, 0, permissionIntent, intentFlags);
    }

    /**
     * Registers the BroadcastReceiver to listen for USB-related events.
     *
     * @param context The application context.
     */
    private static void registerUsbReceiver(Context context) {
        if (receiverRegistered) {
            return;
        }

        IntentFilter filter = new IntentFilter();
        filter.addAction(UsbManager.ACTION_USB_DEVICE_ATTACHED);
        filter.addAction(UsbManager.ACTION_USB_DEVICE_DETACHED);
        filter.addAction(ACTION_USB_PERMISSION);
        // TODO: Move bluetooth handling back to QGCActivity
        filter.addAction(BluetoothDevice.ACTION_ACL_CONNECTED);
        filter.addAction(BluetoothDevice.ACTION_ACL_DISCONNECTED);

        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                int flags = Context.RECEIVER_NOT_EXPORTED;
                context.registerReceiver(usbReceiver, filter, flags);
            } else {
                context.registerReceiver(usbReceiver, filter);
            }

            receiverRegistered = true;
            QGCLogger.i(TAG, "BroadcastReceiver registered successfully.");
        } catch (Exception e) {
            receiverRegistered = false;
            QGCLogger.e(TAG, "Failed to register BroadcastReceiver", e);
        }
    }

    /**
     * BroadcastReceiver to handle USB events.
     */
    private static final BroadcastReceiver usbReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            final String action = intent.getAction();
            QGCLogger.i(TAG, "BroadcastReceiver USB action " + action);

            if (action == null) {
                return;
            }

            synchronized (MANAGER_LOCK) {
                switch (action) {
                    case ACTION_USB_PERMISSION:
                        handleUsbPermission(intent);
                        break;
                    case UsbManager.ACTION_USB_DEVICE_DETACHED:
                        handleUsbDeviceDetached(intent);
                        break;
                    case UsbManager.ACTION_USB_DEVICE_ATTACHED:
                        handleUsbDeviceAttached(intent);
                        break;
                    default:
                        break;
                }
            }

            try {
                nativeUpdateAvailableJoysticks();
            } catch (final Exception ex) {
                QGCLogger.e(TAG, "Exception nativeUpdateAvailableJoysticks()", ex);
            }
        }
    };

    /**
     * Handles USB permission results.
     *
     * @param intent The intent containing permission data.
     */
    private static void handleUsbPermission(final Intent intent) {
        UsbDevice device = getUsbDevice(intent);
        if (device != null) {
            int deviceId = device.getDeviceId();
            pendingPermissionRequests.remove(deviceId);
            if (intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false)) {
                deniedPermissionDeviceIds.remove(deviceId);
                QGCLogger.i(TAG, "Permission granted to " + device.getDeviceName());
                addOrUpdateDevice(device);
            } else {
                deniedPermissionDeviceIds.add(deviceId);
                QGCLogger.w(TAG, "Permission denied for " + device.getDeviceName());
                UsbDeviceResources resources = deviceResourcesMap.get(deviceId);
                if (resources != null && resources.classPtr != 0) {
                    nativeDeviceException(resources.classPtr, "USB Permission Denied");
                }
            }
        }
    }

    /**
     * Handles USB device detachment events.
     *
     * @param intent The intent containing device data.
     */
    private static void handleUsbDeviceDetached(final Intent intent) {
        UsbDevice device = getUsbDevice(intent);
        if (device != null) {
            int deviceId = device.getDeviceId();
            releaseDeviceResources(deviceId);
            removeDriverByDeviceId(deviceId);
            pendingPermissionRequests.remove(deviceId);
            deniedPermissionDeviceIds.remove(deviceId);
            QGCLogger.i(TAG, "Device detached and state removed: " + describeDevice(device));
        }
    }

    /**
     * Handles USB device detachment events.
     *
     * @param intent The intent containing device data.
     */
    private static void handleUsbDeviceAttached(final Intent intent) {
        UsbDevice device = getUsbDevice(intent);
        if (device != null) {
            QGCLogger.i(TAG, "Device attached: " + describeDevice(device));
            int deviceId = device.getDeviceId();
            if (findDriverByDeviceId(deviceId) != null) {
                // An attach event is authoritative even when a vendor framework reused the same
                // numeric deviceId without delivering the previous detach event.
                releaseDeviceResources(deviceId);
                removeDriverByDeviceId(deviceId);
                pendingPermissionRequests.remove(deviceId);
                deniedPermissionDeviceIds.remove(deviceId);
                QGCLogger.i(TAG, "Reset previous same-ID state before attach for device ID " + deviceId);
            }
            addOrUpdateDevice(device);
        }
    }

    /**
     * Adds a new device or updates an existing one.
     *
     * @param device The UsbDevice to add or update.
     */
    private static void addOrUpdateDevice(UsbDevice device) {
        updateCurrentDrivers();
        UsbSerialDriver driver = findDriverByDeviceId(device.getDeviceId());
        if (driver != null) {
            if (usbManager.hasPermission(device)) {
                QGCLogger.i(TAG, "Already have permission to use device " + device.getDeviceName());
            } else {
                requestUsbPermission(device);
            }
        } else {
            QGCLogger.w(TAG, "Attached USB device has no supported serial interface: " + describeDevice(device));
        }
    }

    @SuppressWarnings("deprecation")
    private static UsbDevice getUsbDevice(final Intent intent) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            return intent.getParcelableExtra(UsbManager.EXTRA_DEVICE, UsbDevice.class);
        }
        return intent.getParcelableExtra(UsbManager.EXTRA_DEVICE);
    }

    private static void requestUsbPermission(final UsbDevice device) {
        if (usbManager == null || usbPermissionIntent == null || device == null) {
            QGCLogger.w(TAG, "Cannot request USB permission before the manager is initialized");
            return;
        }

        if (usbManager.hasPermission(device)) {
            pendingPermissionRequests.remove(device.getDeviceId());
            deniedPermissionDeviceIds.remove(device.getDeviceId());
            return;
        }

        int deviceId = device.getDeviceId();
        if (deniedPermissionDeviceIds.contains(deviceId)) {
            return;
        }

        long now = SystemClock.elapsedRealtime();
        Long lastRequestTime = pendingPermissionRequests.get(deviceId);
        if (lastRequestTime != null && (now - lastRequestTime) < USB_PERMISSION_RETRY_MS) {
            return;
        }
        pendingPermissionRequests.put(deviceId, now);

        try {
            QGCLogger.i(TAG, "Requesting permission for " + describeDevice(device));
            usbManager.requestPermission(device, usbPermissionIntent);
        } catch (final RuntimeException e) {
            pendingPermissionRequests.remove(deviceId);
            QGCLogger.e(TAG, "Failed to request USB permission for " + describeDevice(device), e);
        }
    }

    /**
     * Checks if a device name is valid (i.e., exists in the current driver list).
     *
     * @param name The device name to check.
     * @return True if valid, false otherwise.
     */
    public static boolean isDeviceNameValid(final String name) {
        return (name != null) && drivers.stream()
                .anyMatch(driver -> name.equals(driver.getDevice().getDeviceName()));
    }

    /**
     * Checks if a device name is currently open.
     *
     * @param name The device name to check.
     * @return True if open, false otherwise.
     */
    public static boolean isDeviceNameOpen(final String name) {
        int deviceId = getDeviceId(name);
        UsbSerialPort port = findPortByDeviceId(deviceId);
        return (port != null && port.isOpen());
    }

    /**
     * Retrieves the device ID for a given device name.
     *
     * @param deviceName The device name.
     * @return The device ID, or BAD_DEVICE_ID if not found.
     */
    public static int getDeviceId(final String deviceName) {
        UsbSerialDriver driver = findDriverByDeviceName(deviceName);
        if (driver == null) {
            QGCLogger.w(TAG, "Attempt to get ID of unknown device " + deviceName);
            return BAD_DEVICE_ID;
        }

        UsbDevice device = driver.getDevice();
        return device.getDeviceId();
    }

    /**
     * Retrieves the native device handle (file descriptor).
     *
     * @param deviceId The device ID.
     * @return The device handle, or -1 if not found.
     */
    public static int getDeviceHandle(final int deviceId) {
        UsbDeviceResources resources = deviceResourcesMap.get(deviceId);
        return (resources != null) ? resources.fileDescriptor : -1;
    }

    /**
     * Updates the current list of USB serial drivers by scanning connected devices.
     *
     * @param deviceId The device ID to update, or -1 to update all.
     */
    private static void updateCurrentDrivers() {
        synchronized (MANAGER_LOCK) {
            updateCurrentDriversLocked();
        }
    }

    private static void updateCurrentDriversLocked() {
        if (usbManager == null || usbSerialProber == null) {
            return;
        }

        final List<UsbSerialDriver> currentDrivers = probeCurrentDrivers();
        removeStaleDrivers(currentDrivers);
        addNewDrivers(currentDrivers);
    }

    /**
     * Uses the library's normal VID/PID table first, then adds a conservative CDC-ACM fallback.
     * Some flight controllers expose standards-compliant CDC interfaces with a vendor-specific
     * VID/PID that is not in usb-serial-for-android's default table.
     */
    private static List<UsbSerialDriver> probeCurrentDrivers() {
        final List<UsbSerialDriver> currentDrivers = new ArrayList<>();
        final Set<Integer> detectedDeviceIds = new HashSet<>();

        try {
            for (UsbSerialDriver driver : usbSerialProber.findAllDrivers(usbManager)) {
                currentDrivers.add(driver);
                detectedDeviceIds.add(driver.getDevice().getDeviceId());
            }
        } catch (final RuntimeException e) {
            QGCLogger.e(TAG, "Default USB serial probe failed", e);
        }

        for (UsbDevice device : usbManager.getDeviceList().values()) {
            if (detectedDeviceIds.contains(device.getDeviceId()) || !hasCdcAcmInterfaces(device)) {
                continue;
            }

            try {
                UsbSerialDriver fallbackDriver = new CdcAcmSerialDriver(device);
                if (!fallbackDriver.getPorts().isEmpty()) {
                    currentDrivers.add(fallbackDriver);
                    detectedDeviceIds.add(device.getDeviceId());
                    QGCLogger.i(TAG, "Using generic CDC-ACM fallback for " + describeDevice(device));
                }
            } catch (final RuntimeException e) {
                QGCLogger.w(TAG, "CDC-ACM fallback rejected " + describeDevice(device) + ": " + e.getMessage());
            }
        }

        logUsbTopologyWhenChanged(currentDrivers);
        return currentDrivers;
    }

    private static void logUsbTopologyWhenChanged(final List<UsbSerialDriver> currentDrivers) {
        final Collection<UsbDevice> visibleDevices = usbManager.getDeviceList().values();
        final Set<Integer> matchedDeviceIds = new HashSet<>();
        final List<String> signatureParts = new ArrayList<>();
        for (UsbSerialDriver driver : currentDrivers) {
            matchedDeviceIds.add(driver.getDevice().getDeviceId());
        }
        for (UsbDevice device : visibleDevices) {
            signatureParts.add(device.getDeviceId() + "/" + device.getVendorId() + "/" + device.getProductId()
                    + "/" + matchedDeviceIds.contains(device.getDeviceId()) + "/" + usbManager.hasPermission(device));
        }
        Collections.sort(signatureParts);
        final String signature = signatureParts.toString();
        if (signature.equals(lastUsbTopologySignature)) {
            return;
        }
        lastUsbTopologySignature = signature;

        QGCLogger.i(TAG, "USB topology changed: Android Host sees " + visibleDevices.size()
                + " device(s), " + matchedDeviceIds.size() + " serial driver(s)");
        if (visibleDevices.isEmpty()) {
            QGCLogger.w(TAG, "Android USB Host sees no device; check OTG host mode, the data cable, and the remote USB port role");
            return;
        }

        for (UsbDevice device : visibleDevices) {
            if (!matchedDeviceIds.contains(device.getDeviceId())) {
                QGCLogger.w(TAG, "USB device visible but no serial driver matched: "
                        + describeDevice(device) + " interfaces=" + describeInterfaces(device));
            }
        }
    }

    private static String describeInterfaces(final UsbDevice device) {
        final List<String> interfaceDescriptions = new ArrayList<>();
        for (int index = 0; index < device.getInterfaceCount(); ++index) {
            UsbInterface usbInterface = device.getInterface(index);
            interfaceDescriptions.add(usbInterface.getInterfaceClass() + "/"
                    + usbInterface.getInterfaceSubclass() + "/" + usbInterface.getInterfaceProtocol());
        }
        return interfaceDescriptions.toString();
    }

    private static boolean hasCdcAcmInterfaces(final UsbDevice device) {
        boolean hasCommunicationInterface = (device.getDeviceClass() == UsbConstants.USB_CLASS_COMM
                && device.getDeviceSubclass() == USB_CDC_SUBCLASS_ACM);
        boolean hasDataInterface = false;
        for (int index = 0; index < device.getInterfaceCount(); ++index) {
            UsbInterface usbInterface = device.getInterface(index);
            int interfaceClass = usbInterface.getInterfaceClass();
            hasCommunicationInterface |= (interfaceClass == UsbConstants.USB_CLASS_COMM
                    && usbInterface.getInterfaceSubclass() == USB_CDC_SUBCLASS_ACM);
            hasDataInterface |= (interfaceClass == UsbConstants.USB_CLASS_CDC_DATA);
        }
        return hasCommunicationInterface && hasDataInterface;
    }

    /**
     * Removes drivers that are no longer connected.
     *
     * @param currentDrivers The list of currently connected drivers.
     */
    private static void removeStaleDrivers(final List<UsbSerialDriver> currentDrivers) {
        final Set<Integer> currentDeviceIds = new HashSet<>();
        for (UsbSerialDriver currentDriver : currentDrivers) {
            currentDeviceIds.add(currentDriver.getDevice().getDeviceId());
        }

        for (UsbSerialDriver existingDriver : new ArrayList<>(drivers)) {
            int deviceId = existingDriver.getDevice().getDeviceId();
            if (!currentDeviceIds.contains(deviceId)) {
                releaseDeviceResources(deviceId);
                removeDriverByDeviceId(deviceId);
                pendingPermissionRequests.remove(deviceId);
                deniedPermissionDeviceIds.remove(deviceId);
                QGCLogger.i(TAG, "Removed stale serial driver for device ID " + deviceId);
            }
        }
    }

    /**
     * Adds new drivers that are not already in the driver list.
     *
     * @param currentDrivers The list of currently connected drivers.
     */
    private static void addNewDrivers(final List<UsbSerialDriver> currentDrivers) {
        for (UsbSerialDriver newDriver : currentDrivers) {
            int deviceId = newDriver.getDevice().getDeviceId();
            UsbSerialDriver existingDriver = findDriverByDeviceId(deviceId);
            if (existingDriver == null) {
                addDriver(newDriver);
            } else if (!samePhysicalDeviceIdentity(existingDriver.getDevice(), newDriver.getDevice())) {
                // Some vendor Android builds reuse a deviceId after a missed detach broadcast.
                // A changed path/VID/PID means the old port object must never be reused.
                releaseDeviceResources(deviceId);
                removeDriverByDeviceId(deviceId);
                pendingPermissionRequests.remove(deviceId);
                deniedPermissionDeviceIds.remove(deviceId);
                addDriver(newDriver);
            } else if (!deviceResourcesMap.containsKey(deviceId)) {
                // The default prober returns fresh driver objects on every scan. When the port is
                // closed, retain the newest object so a same-ID re-enumeration cannot strand an
                // obsolete UsbDevice. Never replace the driver backing an open port.
                replaceDriverByDeviceId(deviceId, newDriver);
            }

            if (usbManager.hasPermission(newDriver.getDevice())) {
                pendingPermissionRequests.remove(deviceId);
                deniedPermissionDeviceIds.remove(deviceId);
            } else {
                requestUsbPermission(newDriver.getDevice());
            }
        }
    }

    private static boolean samePhysicalDeviceIdentity(final UsbDevice first, final UsbDevice second) {
        return first.getVendorId() == second.getVendorId()
                && first.getProductId() == second.getProductId()
                && Objects.equals(first.getDeviceName(), second.getDeviceName());
    }

    private static void replaceDriverByDeviceId(final int deviceId, final UsbSerialDriver replacement) {
        for (int index = 0; index < drivers.size(); ++index) {
            if (drivers.get(index).getDevice().getDeviceId() == deviceId) {
                drivers.set(index, replacement);
                return;
            }
        }
    }

    /**
     * Adds a new USB serial driver to the driver list and requests permission if needed.
     *
     * @param newDriver The UsbSerialDriver to add.
     */
    private static void addDriver(final UsbSerialDriver newDriver) {
        UsbDevice device = newDriver.getDevice();
        drivers.add(newDriver);
        QGCLogger.i(TAG, "Discovered " + newDriver.getClass().getSimpleName() + " for " + describeDevice(device));

        if (!usbManager.hasPermission(device)) {
            requestUsbPermission(device);
        }
        logManagerState("Driver discovered");
    }

    private static void removeDriverByDeviceId(final int deviceId) {
        for (UsbSerialDriver driver : new ArrayList<>(drivers)) {
            if (driver.getDevice().getDeviceId() == deviceId) {
                drivers.remove(driver);
            }
        }
    }

    /**
     * Finds a USB serial driver by its device ID.
     *
     * @param deviceId The device ID.
     * @return The corresponding UsbSerialDriver or null if not found.
     */
    private static UsbSerialDriver findDriverByDeviceId(final int deviceId) {
        for (UsbSerialDriver driver : drivers) {
            if (driver.getDevice().getDeviceId() == deviceId) {
                return driver;
            }
        }
        return null;
    }

    /**
     * Finds a USB serial driver by its device name.
     *
     * @param deviceName The device name.
     * @return The corresponding UsbSerialDriver or null if not found.
     */
    private static UsbSerialDriver findDriverByDeviceName(final String deviceName) {
        if (deviceName == null) {
            return null;
        }
        for (UsbSerialDriver driver : drivers) {
            if (deviceName.equals(driver.getDevice().getDeviceName())) {
                return driver;
            }
        }
        return null;
    }

    /**
     * Finds a USB serial port by its device ID.
     *
     * @param deviceId The device ID.
     * @return The corresponding UsbSerialPort or null if not found.
     */
    private static UsbSerialPort findPortByDeviceId(final int deviceId) {
        final int portIndex = 0;

        if (deviceId == BAD_DEVICE_ID) {
            QGCLogger.w(TAG, "Finding port failed for invalid Device ID " + deviceId);
            return null;
        }

        UsbDeviceResources resources = deviceResourcesMap.get(deviceId);
        UsbSerialDriver driver = (resources != null) ? resources.driver : findDriverByDeviceId(deviceId);
        if (driver == null) {
            QGCLogger.w(TAG, "No driver found on device ID " + deviceId);
            return null;
        }

        List<UsbSerialPort> ports = driver.getPorts();
        if (ports.isEmpty()) {
            QGCLogger.w(TAG, "No ports available on device ID " + deviceId);
            return null;
        }

        if (portIndex < 0 || portIndex >= ports.size()) {
            QGCLogger.w(TAG, "Invalid port index " + portIndex + " for device ID " + deviceId);
            return null;
        }

        return ports.get(portIndex);
    }

    /**
     * Retrieves information about all available USB serial devices.
     *
     * @return An array of device information strings or null if no devices are available.
     */
    public static String[] availableDevicesInfo() {
        final String[] result;
        synchronized (MANAGER_LOCK) {
            if (usbManager == null) {
                result = null;
            } else {
                updateCurrentDriversLocked();

                final List<String> deviceInfoList = new ArrayList<>();
                final Set<Integer> reportedDeviceIds = new HashSet<>();

                for (final UsbSerialDriver driver : drivers) {
                    final UsbDevice device = driver.getDevice();
                    if (!usbManager.hasPermission(device)) {
                        continue;
                    }
                    try {
                        if (driver.getPorts().isEmpty() || !reportedDeviceIds.add(device.getDeviceId())) {
                            continue;
                        }
                        final String deviceInfo = formatDeviceInfo(device);
                        deviceInfoList.add(deviceInfo);
                    } catch (final RuntimeException e) {
                        QGCLogger.e(TAG, "Ignoring broken USB serial driver for " + describeDevice(device), e);
                    }
                }

                if (lastReportedSerialDeviceCount != deviceInfoList.size()) {
                    lastReportedSerialDeviceCount = deviceInfoList.size();
                    QGCLogger.i(TAG, "Reporting " + deviceInfoList.size() + " authorized USB serial device(s) to QGC");
                }
                result = deviceInfoList.toArray(new String[0]);
            }
        }
        return result;
    }

    /**
     * Formats device information into a standardized string.
     *
     * @param device The UsbDevice to format.
     * @return A formatted string containing device information.
     */
    private static String formatDeviceInfo(final UsbDevice device) {
        String productName = "";
        String manufacturerName = "";
        String serialNumber = "";
        try {
            productName = safeDeviceInfoField(device.getProductName());
            manufacturerName = safeDeviceInfoField(device.getManufacturerName());
            if (usbManager != null && usbManager.hasPermission(device)) {
                serialNumber = safeDeviceInfoField(device.getSerialNumber());
            }
        } catch (final SecurityException e) {
            QGCLogger.w(TAG, "USB descriptor access needs permission for " + describeDevice(device));
        } catch (final RuntimeException e) {
            QGCLogger.w(TAG, "Could not read USB descriptors for " + describeDevice(device) + ": " + e.getMessage());
        }

        return safeDeviceInfoField(device.getDeviceName()) + ":"
                + productName + ":"
                + manufacturerName + ":"
                + serialNumber + ":"
                + device.getProductId() + ":"
                + device.getVendorId();
    }

    private static String safeDeviceInfoField(final String value) {
        return (value == null) ? "" : value.replace(':', ' ');
    }

    private static String describeDevice(final UsbDevice device) {
        if (device == null) {
            return "null USB device";
        }
        return device.getDeviceName() + " [VID=0x" + Integer.toHexString(device.getVendorId())
                + ", PID=0x" + Integer.toHexString(device.getProductId())
                + ", ID=" + device.getDeviceId() + "]";
    }

    /**
     * Opens a USB serial device.
     *
     * @param deviceName The name of the device to open.
     * @param classPtr   A native pointer associated with the device.
     * @return The device ID if successful, or BAD_DEVICE_ID if failed.
     */
    public static int open(final String deviceName, final long classPtr) {
        try {
            synchronized (MANAGER_LOCK) {
                if (usbManager == null) {
                    QGCLogger.w(TAG, "Attempt to open USB serial before manager initialization");
                    nativeDeviceException(classPtr, "USB serial manager is not initialized");
                    return BAD_DEVICE_ID;
                }

                updateCurrentDriversLocked();
                UsbSerialDriver driver = findDriverByDeviceName(deviceName);
                if (driver == null) {
                    QGCLogger.w(TAG, "Attempt to open unknown serial device " + deviceName);
                    nativeDeviceException(classPtr, "Unknown USB serial device: " + deviceName);
                    return BAD_DEVICE_ID;
                }

                UsbDevice device = driver.getDevice();
                int deviceId = device.getDeviceId();
                if (!usbManager.hasPermission(device)) {
                    requestUsbPermission(device);
                    nativeDeviceException(classPtr, "USB permission requested for device: " + deviceName);
                    return BAD_DEVICE_ID;
                }

                List<UsbSerialPort> ports = driver.getPorts();
                if (ports.isEmpty()) {
                    QGCLogger.w(TAG, "No ports available on device " + deviceName);
                    nativeDeviceException(classPtr, "No USB serial ports on device: " + deviceName);
                    return BAD_DEVICE_ID;
                }

                UsbDeviceResources previousResources = deviceResourcesMap.get(deviceId);
                UsbSerialPort previousPort = (previousResources != null && !previousResources.driver.getPorts().isEmpty())
                        ? previousResources.driver.getPorts().get(0) : null;
                if (previousPort != null && previousPort.isOpen()) {
                    QGCLogger.w(TAG, "USB serial device is already open: " + deviceName);
                    nativeDeviceException(classPtr, "USB serial device is already open: " + deviceName);
                    return BAD_DEVICE_ID;
                }
                releaseDeviceResources(deviceId);

                UsbDeviceResources resources = new UsbDeviceResources(driver);
                resources.classPtr = classPtr;
                deviceResourcesMap.put(deviceId, resources);

                UsbSerialPort port = ports.get(0);
                if (!openDriver(port, device, deviceId, classPtr)) {
                    releaseDeviceResources(deviceId);
                    QGCLogger.e(TAG, "Failed to open driver for device " + deviceName);
                    return BAD_DEVICE_ID;
                }

                if (!createIoManager(deviceId, port, classPtr)) {
                    releaseDeviceResources(deviceId);
                    nativeDeviceException(classPtr, "Failed to create USB serial I/O manager for: " + deviceName);
                    return BAD_DEVICE_ID;
                }

                QGCLogger.i(TAG, "USB serial port opened with hardware ID " + describeDevice(device));
                logManagerState("Port opened");
                return deviceId;
            }
        } catch (final RuntimeException e) {
            QGCLogger.e(TAG, "Unexpected USB serial open failure for " + deviceName, e);
            nativeDeviceException(classPtr, "Unexpected USB serial open failure: " + e.getMessage());
            return BAD_DEVICE_ID;
        }
    }

    /**
     * Opens the driver for a specific USB serial port.
     *
     * @param port     The UsbSerialPort to open.
     * @param device   The UsbDevice associated with the port.
     * @param deviceId The device ID.
     * @param classPtr A native pointer associated with the device.
     * @return True if successful, false otherwise.
     */
    private static boolean openDriver(final UsbSerialPort port, final UsbDevice device, final int deviceId, final long classPtr) {
        if (port == null) {
            nativeDeviceException(classPtr, "USB serial port is null for device: " + device.getDeviceName());
            return false;
        }

        UsbDeviceConnection connection;
        try {
            connection = usbManager.openDevice(device);
        } catch (final RuntimeException e) {
            QGCLogger.e(TAG, "Android rejected openDevice for " + describeDevice(device), e);
            nativeDeviceException(classPtr, "Android rejected USB open: " + e.getMessage());
            return false;
        }
        if (connection == null) {
            QGCLogger.w(TAG, "No USB device connection for " + describeDevice(device));
            nativeDeviceException(classPtr, "No USB device connection for device: " + device.getDeviceName());
            return false;
        }

        try {
            port.open(connection);
        } catch (final IOException | RuntimeException ex) {
            QGCLogger.e(TAG, "Error opening driver for device " + device.getDeviceName(), ex);
            nativeDeviceException(classPtr, "Error opening driver: " + ex.getMessage());
            connection.close();
            return false;
        }

        UsbDeviceResources resources = deviceResourcesMap.get(deviceId);
        if (resources != null) {
            resources.fileDescriptor = connection.getFileDescriptor();
        }

        // QSerialPortPrivate applies the requested baud/data/stop/parity settings immediately
        // after open() returns. Avoid forcing an intermediate 9600 configuration here because
        // some CDC firmwares reject it and then never reach the real MAVLink baud rate.
        QGCLogger.d(TAG, "Port driver opened for device ID " + deviceId);
        return true;
    }

    /**
     * Creates and initializes the SerialInputOutputManager for a device.
     */
    private static boolean createIoManager(final int deviceId, final UsbSerialPort port, final long classPtr) {
        UsbDeviceResources resources = deviceResourcesMap.get(deviceId);
        if (resources == null) {
            QGCLogger.w(TAG, "No resources found for device ID " + deviceId);
            return false;
        }

        if (resources.ioManager != null) {
            QGCLogger.i(TAG, "IO Manager already exists for device ID " + deviceId);
            return true;
        }

        try {
            UsbEndpoint readEndpoint = port.getReadEndpoint();
            if (readEndpoint == null) {
                QGCLogger.e(TAG, "USB serial port has no read endpoint for device ID " + deviceId);
                return false;
            }

            QGCSerialListener listener = new QGCSerialListener(classPtr);
            SerialInputOutputManager ioManager = new SerialInputOutputManager(port, listener);
            ioManager.setReadBufferSize(Math.max(readEndpoint.getMaxPacketSize(), READ_BUF_SIZE));

            QGCLogger.d(TAG, "Read Buffer Size: " + ioManager.getReadBufferSize());
            QGCLogger.d(TAG, "Write Buffer Size: " + ioManager.getWriteBufferSize());
            ioManager.setReadTimeout(0);
            ioManager.setWriteTimeout(0);
            ioManager.setThreadPriority(Process.THREAD_PRIORITY_URGENT_AUDIO);

            resources.listener = listener;
            resources.ioManager = ioManager;
        } catch (final RuntimeException e) {
            QGCLogger.e(TAG, "IO Manager configuration error:", e);
            return false;
        }

        QGCLogger.d(TAG, "Serial I/O Manager created for device ID " + deviceId);
        return true;
    }

    /**
     * Starts the SerialInputOutputManager for a specific device.
     */
    public static boolean startIoManager(final int deviceId) {
        UsbDeviceResources resources = deviceResourcesMap.get(deviceId);
        SerialInputOutputManager ioManager = (resources != null) ? resources.ioManager : null;
        if (ioManager == null) {
            QGCLogger.w(TAG, "IO Manager not found for device ID " + deviceId);
            return false;
        }

        try {
            if (ioManager.getState() == SerialInputOutputManager.State.RUNNING) {
                return true;
            }
            ioManager.start();
            QGCLogger.d(TAG, "Serial I/O Manager started for device ID " + deviceId);
            return true;
        } catch (final RuntimeException e) {
            QGCLogger.e(TAG, "IO Manager start exception:", e);
            return false;
        }
    }

    /**
     * Stops the SerialInputOutputManager for a specific device.
     */
    public static boolean stopIoManager(final int deviceId) {
        UsbDeviceResources resources = deviceResourcesMap.get(deviceId);
        SerialInputOutputManager ioManager = (resources != null) ? resources.ioManager : null;
        if (ioManager == null) {
            return false;
        }

        try {
            SerialInputOutputManager.State ioState = ioManager.getState();
            if (ioState == SerialInputOutputManager.State.STOPPED || ioState == SerialInputOutputManager.State.STOPPING) {
                return true;
            }
            ioManager.stop();
            QGCLogger.d(TAG, "Serial I/O Manager stopped for device ID " + deviceId);
            return true;
        } catch (final RuntimeException e) {
            QGCLogger.e(TAG, "IO Manager stop exception:", e);
            return false;
        }
    }

    /**
     * Checks if the SerialInputOutputManager is running for a specific device.
     *
     * @param deviceId The device ID.
     * @return True if running, false otherwise.
     */
    public static boolean ioManagerRunning(final int deviceId) {
        UsbDeviceResources resources = deviceResourcesMap.get(deviceId);
        SerialInputOutputManager ioManager = (resources != null) ? resources.ioManager : null;
        if (ioManager == null) {
            return false;
        }

        try {
            return (ioManager.getState() == SerialInputOutputManager.State.RUNNING);
        } catch (final RuntimeException e) {
            QGCLogger.e(TAG, "IO Manager state exception:", e);
            return false;
        }
    }

    /**
     * Closes the USB serial device.
     *
     * @param deviceId The device ID.
     * @return True if successful, false otherwise.
     */
    public static boolean close(int deviceId) {
        synchronized (MANAGER_LOCK) {
            return releaseDeviceResources(deviceId);
        }
    }

    /**
     * Releases only the resources of an open port. The discovery driver intentionally stays in
     * {@link #drivers}, so QGC can close and reopen the same physical connection without replugging.
     */
    private static boolean releaseDeviceResources(final int deviceId) {
        UsbDeviceResources resources = deviceResourcesMap.remove(deviceId);
        if (resources == null) {
            return true;
        }

        boolean success = true;
        if (resources.listener != null) {
            resources.listener.invalidate();
        }
        if (resources.ioManager != null) {
            try {
                SerialInputOutputManager.State state = resources.ioManager.getState();
                if (state != SerialInputOutputManager.State.STOPPED
                        && state != SerialInputOutputManager.State.STOPPING) {
                    resources.ioManager.stop();
                }
            } catch (final RuntimeException e) {
                success = false;
                QGCLogger.e(TAG, "Error stopping I/O manager for device ID " + deviceId, e);
            }
        }

        try {
            UsbSerialPort port = null;
            if (resources.driver != null && !resources.driver.getPorts().isEmpty()) {
                port = resources.driver.getPorts().get(0);
            }
            if (port != null && port.isOpen()) {
                port.close();
            }
        } catch (final IOException | RuntimeException e) {
            success = false;
            QGCLogger.e(TAG, "Error closing USB serial port for device ID " + deviceId, e);
        }

        resources.ioManager = null;
        resources.listener = null;
        resources.fileDescriptor = -1;
        resources.classPtr = 0;
        QGCLogger.i(TAG, "Released open USB serial resources for device ID " + deviceId);
        logManagerState("Port resources released");
        return success;
    }

    private static void logManagerState(final String event) {
        QGCLogger.i(TAG, event + ": drivers=" + drivers.size()
                + ", openResources=" + deviceResourcesMap.size()
                + ", pendingPermissions=" + pendingPermissionRequests.size());
    }

    /**
     * Writes data to the USB serial device.
     *
     * @param deviceId    The device ID.
     * @param data        The byte array of data to write.
     * @param length      The number of bytes to write.
     * @param timeoutMSec The timeout in milliseconds.
     * @return The number of bytes written, or -1 if failed.
     */
    public static int write(final int deviceId, final byte[] data, final int length, final int timeoutMSec) {
        UsbSerialPort port = findPortByDeviceId(deviceId);
        if (port == null) {
            QGCLogger.w(TAG, "Attempted to write to a null port for device ID " + deviceId);
            return -1;
        }

        if (!port.isOpen()) {
            QGCLogger.w(TAG, "Attempted to write to a closed port for device ID " + deviceId);
            return -1;
        }

        try {
            port.write(data, length, timeoutMSec);
            return length;
        } catch (final SerialTimeoutException e) {
            QGCLogger.e(TAG, "Write timeout occurred", e);
            return -1;
        } catch (final IOException e) {
            QGCLogger.e(TAG, "Error writing data", e);
            return -1;
        }
    }

    /**
     * Writes data asynchronously to the USB serial device.
     *
     * @param deviceId    The device ID.
     * @param data        The byte array of data to write.
     * @param timeoutMSec The timeout in milliseconds.
     * @return The number of bytes written, or -1 if failed.
     */
    public static int writeAsync(final int deviceId, final byte[] data, final int timeoutMSec) {
        UsbDeviceResources resources = deviceResourcesMap.get(deviceId);
        SerialInputOutputManager ioManager = (resources != null) ? resources.ioManager : null;
        if (ioManager == null) {
            QGCLogger.w(TAG, "IO Manager not found for device ID " + deviceId);
            return -1;
        }

        try {
            if (ioManager.getReadTimeout() == 0) {
                QGCLogger.w(TAG, "Read Timeout is 0 for writeAsync");
            }
            ioManager.setWriteTimeout(timeoutMSec);
            ioManager.writeAsync(data);
            return data.length;
        } catch (final RuntimeException e) {
            QGCLogger.e(TAG, "Asynchronous write failed for device ID " + deviceId, e);
            return -1;
        }
    }

    /**
     * Reads data from the USB serial device.
     *
     * @param deviceId The device ID.
     * @param length   The number of bytes to read.
     * @param timeoutMs The timeout in milliseconds.
     * @return A byte array containing the read data.
     */
    public static byte[] read(final int deviceId, final int length, final int timeoutMs) {
        if (timeoutMs < 500) {
            QGCLogger.w(TAG, "Read with timeout less than recommended minimum of 200-500ms");
        }

        UsbSerialPort port = findPortByDeviceId(deviceId);
        if (port == null) {
            QGCLogger.w(TAG, "Attempted to read from a null port for device ID " + deviceId);
            return new byte[]{};
        }

        if (!port.isOpen()) {
            QGCLogger.w(TAG, "Attempted to read from a closed port for device ID " + deviceId);
            return new byte[]{};
        }

        byte[] buffer = new byte[length];
        int bytesRead = 0;

        try {
            bytesRead = port.read(buffer, timeoutMs);
        } catch (final IOException e) {
            QGCLogger.e(TAG, "Error reading data", e);
        }

        if (bytesRead < length) {
            return Arrays.copyOf(buffer, bytesRead);
        }

        return buffer;
    }

    /**
     * Sets the parameters on an open USB serial port.
     *
     * @param deviceId The device ID.
     * @param baudRate The baud rate (e.g., 9600, 115200).
     * @param dataBits The number of data bits (5, 6, 7, 8).
     * @param stopBits The number of stop bits (1, 2).
     * @param parity   The parity setting (0: None, 1: Odd, 2: Even).
     * @return True if successful, false otherwise.
     */
    public static boolean setParameters(final int deviceId, final int baudRate, final int dataBits, final int stopBits, final int parity) {
        UsbSerialPort port = findPortByDeviceId(deviceId);
        if (port == null) {
            QGCLogger.w(TAG, "Attempted to set parameters to a null port for device ID " + deviceId);
            return false;
        }

        if (!port.isOpen()) {
            QGCLogger.w(TAG, "Attempted to set parameters on a closed port for device ID " + deviceId);
            return false;
        }

        try {
            port.setParameters(baudRate, dataBits, stopBits, parity);
        } catch (final UnsupportedOperationException e) {
            QGCLogger.w(TAG, "Error setting parameters" + ": " + e);
            return false;
        } catch (final IOException e) {
            QGCLogger.e(TAG, "Error setting parameters", e);
            return false;
        }

        return true;
    }

    private static boolean getControlLine(int deviceId, UsbSerialPort.ControlLine controlLine) {
        UsbSerialPort port = findPortByDeviceId(deviceId);
        if (port == null) {
            QGCLogger.w(TAG, "Attempted to get " + controlLine + " from a null port for device ID " + deviceId);
            return false;
        }

        if (!isControlLineSupported(port, controlLine)) {
            QGCLogger.w(TAG, "Getting " + controlLine + " Not Supported");
            return false;
        }

        try {
            switch (controlLine) {
                case CD:
                    return port.getCD();
                case CTS:
                    return port.getCTS();
                case DSR:
                    return port.getDSR();
                case DTR:
                    return port.getDTR();
                case RI:
                    return port.getRI();
                case RTS:
                    return port.getRTS();
                default:
                    return false;
            }
        } catch (final UnsupportedOperationException e) {
            QGCLogger.w(TAG, "Error getting " + controlLine + ": " + e);
            return false;
        }  catch (final IOException e) {
            QGCLogger.e(TAG, "Error getting " + controlLine, e);
            return false;
        }
    }

    private static boolean setControlLine(int deviceId, UsbSerialPort.ControlLine controlLine, boolean on) {
        UsbSerialPort port = findPortByDeviceId(deviceId);
        if (port == null) {
            QGCLogger.w(TAG, "Attempted to set " + controlLine + " on a null port for device ID " + deviceId);
            return false;
        }

        if (!isControlLineSupported(port, controlLine)) {
            QGCLogger.e(TAG, "Setting " + controlLine + " Not Supported");
            return false;
        }

        try {
            switch (controlLine) {
                case DTR:
                    port.setDTR(on);
                    break;
                case RTS:
                    port.setRTS(on);
                    break;
                default:
                    QGCLogger.w(TAG, "Setting " + controlLine + " is not supported via this method.");
                    return false;
            }
        } catch (final UnsupportedOperationException e) {
            QGCLogger.w(TAG, "Error setting " + controlLine + ": " + e);
            return false;
        } catch (final IOException e) {
            QGCLogger.e(TAG, "Error setting " + controlLine, e);
            return false;
        }

        return true;
    }

    /**
     * Checks if a specific control line is supported by the device.
     *
     * @param port        The UsbSerialPort.
     * @param controlLine The control line to check.
     * @return True if supported, false otherwise.
     */
    private static boolean isControlLineSupported(final UsbSerialPort port, final UsbSerialPort.ControlLine controlLine) {
        EnumSet<UsbSerialPort.ControlLine> supportedControlLines;

        try {
            supportedControlLines = port.getSupportedControlLines();
        } catch (final IOException e) {
            QGCLogger.e(TAG, "Error getting supported control lines", e);
            return false;
        }

        return supportedControlLines.contains(controlLine);
    }

    /**
     * Retrieves the carrier detect (CD) flag from the device.
     *
     * @param deviceId The device ID.
     * @return True if CD is active, false otherwise.
     */
    public static boolean getCarrierDetect(final int deviceId) {
        return getControlLine(deviceId, UsbSerialPort.ControlLine.CD);
    }

    /**
     * Retrieves the clear to send (CTS) flag from the device.
     *
     * @param deviceId The device ID.
     * @return True if CTS is active, false otherwise.
     */
    public static boolean getClearToSend(final int deviceId) {
        return getControlLine(deviceId, UsbSerialPort.ControlLine.CTS);
    }

    /**
     * Retrieves the data set ready (DSR) flag from the device.
     *
     * @param deviceId The device ID.
     * @return True if DSR is active, false otherwise.
     */
    public static boolean getDataSetReady(final int deviceId) {
        return getControlLine(deviceId, UsbSerialPort.ControlLine.DSR);
    }

    /**
     * Retrieves the data terminal ready (DTR) flag from the device.
     *
     * @param deviceId The device ID.
     * @return True if DTR is active, false otherwise.
     */
    public static boolean getDataTerminalReady(final int deviceId) {
        return getControlLine(deviceId, UsbSerialPort.ControlLine.DTR);
    }

    /**
     * Sets the data terminal ready (DTR) flag on the device.
     *
     * @param deviceId The device ID.
     * @param on       True to set DTR, false to clear.
     * @return True if successful, false otherwise.
     */
    public static boolean setDataTerminalReady(final int deviceId, final boolean on) {
        return setControlLine(deviceId, UsbSerialPort.ControlLine.DTR, on);
    }

    /**
     * Retrieves the request to send (RTS) flag from the device.
     *
     * @param deviceId The device ID.
     * @return True if RTS is active, false otherwise.
     */
    public static boolean getRequestToSend(final int deviceId) {
        return getControlLine(deviceId, UsbSerialPort.ControlLine.RTS);
    }

    /**
     * Sets the request to send (RTS) flag on the device.
     *
     * @param deviceId The device ID.
     * @param on       True to set RTS, false to clear.
     * @return True if successful, false otherwise.
     */
    public static boolean setRequestToSend(final int deviceId, final boolean on) {
        return setControlLine(deviceId, UsbSerialPort.ControlLine.RTS, on);
    }

    /**
     * Retrieves the ring indicator (RI) flag from the device.
     *
     * @param deviceId The device ID.
     * @return True if RI is active, false otherwise.
     */
    public static boolean getRingIndicator(final int deviceId) {
        return getControlLine(deviceId, UsbSerialPort.ControlLine.RI);
    }

    /**
     * Retrieves the supported control lines from the device.
     *
     * @param deviceId The device ID.
     * @return An array of control line ordinals.
     */
    public static int[] getControlLines(final int deviceId) {
        UsbSerialPort port = findPortByDeviceId(deviceId);
        if (port == null || !port.isOpen()) {
            QGCLogger.w(TAG, "Attempted to get control lines from a null or closed port for device ID " + deviceId);
            return new int[]{};
        }

        EnumSet<UsbSerialPort.ControlLine> currentControlLines;

        try {
            currentControlLines = port.getControlLines();
        } catch (final UnsupportedOperationException e) {
            QGCLogger.w(TAG, "Error getting control lines: " + e);
            return new int[]{};
        } catch (final IOException e) {
            QGCLogger.e(TAG, "Error getting control lines", e);
            return new int[]{};
        }

        int[] lines = currentControlLines.stream().mapToInt(UsbSerialPort.ControlLine::ordinal).toArray();
        return lines;
    }

    /**
     * Retrieves the current flow control setting from the device.
     *
     * @param deviceId The device ID.
     * @return The flow control ordinal, or 0 if not supported.
     */
    public static int getFlowControl(final int deviceId) {
        UsbSerialPort port = findPortByDeviceId(deviceId);
        if (port == null || !port.isOpen()) {
            QGCLogger.w(TAG, "Attempted to get flow control from a null or closed port for device ID " + deviceId);
            return 0;
        }

        EnumSet<UsbSerialPort.FlowControl> supportedFlowControl = port.getSupportedFlowControl();
        if (supportedFlowControl.isEmpty()) {
            QGCLogger.e(TAG, "Flow Control Not Supported");
            return 0;
        }

        UsbSerialPort.FlowControl flowControl = port.getFlowControl();
        return flowControl.ordinal();
    }

    /**
     * Sets the flow control setting on the device.
     *
     * @param deviceId    The device ID.
     * @param flowControl The flow control ordinal.
     * @return True if successful, false otherwise.
     */
    public static boolean setFlowControl(final int deviceId, final int flowControl) {
        if (getFlowControl(deviceId) == flowControl) {
            return true;
        }

        if (flowControl < 0 || flowControl >= UsbSerialPort.FlowControl.values().length) {
            QGCLogger.w(TAG, "Invalid flow control ordinal " + flowControl);
            return false;
        }

        UsbSerialPort.FlowControl flowControlEnum = UsbSerialPort.FlowControl.values()[flowControl];
        UsbSerialPort port = findPortByDeviceId(deviceId);

        if (port == null || !port.isOpen()) {
            QGCLogger.w(TAG, "Attempted to set flow control on a null or closed port for device ID " + deviceId);
            return false;
        }

        EnumSet<UsbSerialPort.FlowControl> supportedFlowControl = port.getSupportedFlowControl();
        if (!supportedFlowControl.contains(flowControlEnum)) {
            QGCLogger.e(TAG, "Setting Flow Control Not Supported");
            return false;
        }

        try {
            port.setFlowControl(flowControlEnum);
        } catch (final UnsupportedOperationException e) {
            QGCLogger.w(TAG, "Error setting Flow Control: " + e);
            return false;
        } catch (final IOException e) {
            QGCLogger.e(TAG, "Error setting Flow Control", e);
            return false;
        }

        return true;
    }

    /**
     * Sets the break condition on the device.
     *
     * @param deviceId The device ID.
     * @param on       True to set break, false to clear break.
     * @return True if successful, false otherwise.
     */
    public static boolean setBreak(final int deviceId, final boolean on) {
        UsbSerialPort port = findPortByDeviceId(deviceId);
        if (port == null || !port.isOpen()) {
            QGCLogger.w(TAG, "Attempted to set break on a null or closed port for device ID " + deviceId);
            return false;
        }

        try {
            port.setBreak(on);
        } catch (final UnsupportedOperationException e) {
            QGCLogger.w(TAG, "Error setting break condition: " + e);
            return false;
        } catch (final IOException e) {
            QGCLogger.e(TAG, "Error setting break condition", e);
            return false;
        }

        return true;
    }

    /**
     * Purges the hardware buffers on the device.
     *
     * @param deviceId The device ID.
     * @param input    True to purge the input buffer.
     * @param output   True to purge the output buffer.
     * @return True if successful, false otherwise.
     */
    public static boolean purgeBuffers(final int deviceId, final boolean input, final boolean output) {
        UsbSerialPort port = findPortByDeviceId(deviceId);
        if (port == null || !port.isOpen()) {
            QGCLogger.w(TAG, "Attempted to purge buffers on a null or closed port for device ID " + deviceId);
            return false;
        }

        try {
            port.purgeHwBuffers(input, output);
        } catch (final UnsupportedOperationException e) {
            QGCLogger.w(TAG, "Error purging buffers: " + e);
            return false;
        } catch (final IOException e) {
            QGCLogger.e(TAG, "Error purging buffers", e);
            return false;
        }

        return true;
    }

    /**
     * Inner class to handle serial data callbacks.
     */
    private static class QGCSerialListener implements SerialInputOutputManager.Listener {
        private long classPtr;

        public QGCSerialListener(long classPtr) {
            this.classPtr = classPtr;
        }

        public synchronized void invalidate() {
            classPtr = 0;
        }

        @Override
        public synchronized void onRunError(Exception e) {
            QGCLogger.e(TAG, "Runner stopped.", e);
            if (classPtr != 0) {
                nativeDeviceException(classPtr, "Runner stopped: " + e.getMessage());
            }
        }

        @Override
        public synchronized void onNewData(final byte[] data) {
            if (classPtr == 0) {
                return;
            }
            if (isValidData(data)) {
                nativeDeviceNewData(classPtr, data);
            } else {
                QGCLogger.w(TAG, "Invalid data received: " + Arrays.toString(data));
            }
        }

        private boolean isValidData(byte[] data) {
            return ((data != null) && (data.length > 0));
        }
    }
}
