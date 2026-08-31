package org.mavlink.qgroundcontrol;

import android.Manifest;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothManager;
import android.bluetooth.le.BluetoothLeScanner;
import android.content.Context;
import android.content.pm.PackageManager;
import android.os.Build;
import android.provider.Settings;

/** QGC_CUSTOM_ANDROID_BLUETOOTH_STATE_V2
 * Supplies a fail-closed Bluetooth preflight for the UniRC UART2 bridge.
 */
public final class QGCCustomBluetoothState {
    public static final int STATE_UNKNOWN = 0;
    public static final int STATE_FULLY_OFF = 1;
    public static final int STATE_CLASSIC_ACTIVE = 2;
    public static final int STATE_BLE_ACTIVE = 3;
    public static final int STATE_SCAN_ALWAYS_ENABLED = 4;
    public static final int STATE_PERMISSION_REQUIRED = 5;

    private static final int PERMISSION_REQUEST_CODE = 0x5542;
    // Hidden framework states used by Android when only the BLE controller is active.
    private static final int ADAPTER_STATE_BLE_TURNING_ON = 14;
    private static final int ADAPTER_STATE_BLE_ON = 15;
    private static final int ADAPTER_STATE_BLE_TURNING_OFF = 16;
    private static final Object PERMISSION_REQUEST_LOCK = new Object();

    private static volatile String sLastDiagnostics =
        "result=Unknown;reason=not_checked";
    private static boolean sPermissionRequestAttempted = false;

    private QGCCustomBluetoothState() {
    }

    /**
     * Returns a conservative Bluetooth state for deciding whether /dev/ttyHS0
     * may be opened. FullyOff is returned only when every available signal
     * explicitly reports an inactive state.
     */
    @SuppressWarnings("deprecation")
    public static int getStateForUniRc() {
        final QGCActivity activity = QGCActivity.getInstance();
        if (activity == null) {
            return finish(STATE_UNKNOWN, "activity=null");
        }

        final StringBuilder diagnostics = new StringBuilder(192);
        diagnostics.append("api=").append(Build.VERSION.SDK_INT);

        final SettingValue bluetoothOn = readGlobalSetting(activity, "bluetooth_on");
        final SettingValue scanAlways = readGlobalSetting(
            activity, "ble_scan_always_enabled");
        diagnostics.append(";bluetooth_on=").append(bluetoothOn.description);
        diagnostics.append(";ble_scan_always_enabled=").append(scanAlways.description);

        boolean permissionRequired = false;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S
            && activity.checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT)
                != PackageManager.PERMISSION_GRANTED) {
            permissionRequired = true;
            diagnostics.append(";connect_permission=missing");
        } else {
            diagnostics.append(";connect_permission=")
                .append(Build.VERSION.SDK_INT >= Build.VERSION_CODES.S
                    ? "granted" : "not_required");
        }

        int adapterState = Integer.MIN_VALUE;
        boolean adapterRead = false;
        boolean scannerRead = false;
        boolean scannerAvailable = false;
        boolean probeFailed = false;

        if (!permissionRequired) {
            try {
                final BluetoothManager manager = (BluetoothManager)
                    activity.getSystemService(Context.BLUETOOTH_SERVICE);
                final BluetoothAdapter adapter = manager != null ? manager.getAdapter() : null;
                if (adapter == null) {
                    diagnostics.append(";adapter=null;le_scanner=unread");
                    probeFailed = true;
                } else {
                    adapterState = adapter.getState();
                    adapterRead = true;
                    diagnostics.append(";adapter=").append(adapterStateName(adapterState));

                    final BluetoothLeScanner scanner = adapter.getBluetoothLeScanner();
                    scannerRead = true;
                    scannerAvailable = scanner != null;
                    diagnostics.append(";le_scanner=")
                        .append(scannerAvailable ? "available" : "null");
                }
            } catch (final SecurityException exception) {
                permissionRequired = Build.VERSION.SDK_INT >= Build.VERSION_CODES.S;
                probeFailed = !permissionRequired;
                diagnostics.append(";probe_security_exception=")
                    .append(exception.getClass().getSimpleName());
            } catch (final RuntimeException exception) {
                probeFailed = true;
                diagnostics.append(";probe_exception=")
                    .append(exception.getClass().getSimpleName());
            }
        } else {
            diagnostics.append(";adapter=unread;le_scanner=unread");
        }

        final boolean classicActive = adapterRead
            && (adapterState == BluetoothAdapter.STATE_TURNING_ON
                || adapterState == BluetoothAdapter.STATE_ON
                || adapterState == BluetoothAdapter.STATE_TURNING_OFF);
        final boolean bleAdapterActive = adapterRead
            && (adapterState == ADAPTER_STATE_BLE_TURNING_ON
                || adapterState == ADAPTER_STATE_BLE_ON
                || adapterState == ADAPTER_STATE_BLE_TURNING_OFF);
        final boolean adapterStateKnown = adapterRead
            && (adapterState == BluetoothAdapter.STATE_OFF
                || classicActive
                || bleAdapterActive);
        if (adapterRead && !adapterStateKnown) {
            probeFailed = true;
        }

        final int state;
        if (classicActive
            || bluetoothOn.isEnabled()) {
            state = STATE_CLASSIC_ACTIVE;
        } else if (bleAdapterActive || (scannerRead && scannerAvailable)) {
            state = STATE_BLE_ACTIVE;
        } else if (scanAlways.isEnabled()) {
            state = STATE_SCAN_ALWAYS_ENABLED;
        } else if (permissionRequired) {
            state = STATE_PERMISSION_REQUIRED;
        } else if (!probeFailed
            && adapterRead
            && adapterState == BluetoothAdapter.STATE_OFF
            && scannerRead
            && !scannerAvailable
            && bluetoothOn.isDisabled()
            && scanAlways.isDisabled()) {
            state = STATE_FULLY_OFF;
        } else {
            state = STATE_UNKNOWN;
        }

        diagnostics.append(";result=").append(stateName(state));
        sLastDiagnostics = diagnostics.toString();
        return state;
    }

    /** Returns the diagnostics produced by the most recent readable probe/request. */
    public static String getDiagnosticsForUniRc() {
        return sLastDiagnostics;
    }

    /**
     * Requests BLUETOOTH_CONNECT on the Android UI thread. Automatic requests
     * are limited to once per process so a denial cannot create a popup loop.
     */
    public static void requestBluetoothConnectPermissionForUniRc() {
        final QGCActivity activity = QGCActivity.getInstance();
        if (activity == null) {
            sLastDiagnostics = "permission_request=failed;reason=activity_null";
            return;
        }
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.S
            || activity.checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT)
                == PackageManager.PERMISSION_GRANTED) {
            sLastDiagnostics = "permission_request=not_needed";
            return;
        }

        synchronized (PERMISSION_REQUEST_LOCK) {
            if (sPermissionRequestAttempted) {
                sLastDiagnostics = "permission_request=already_attempted";
                return;
            }
            sPermissionRequestAttempted = true;
        }

        try {
            activity.runOnUiThread(new Runnable() {
                @Override
                public void run() {
                    try {
                        if (activity.checkSelfPermission(
                                Manifest.permission.BLUETOOTH_CONNECT)
                            != PackageManager.PERMISSION_GRANTED) {
                            activity.requestPermissions(
                                new String[] { Manifest.permission.BLUETOOTH_CONNECT },
                                PERMISSION_REQUEST_CODE);
                            sLastDiagnostics = "permission_request=shown";
                        } else {
                            sLastDiagnostics = "permission_request=already_granted";
                        }
                    } catch (final RuntimeException exception) {
                        sLastDiagnostics = "permission_request=failed;exception="
                            + exception.getClass().getSimpleName();
                    }
                }
            });
            sLastDiagnostics = "permission_request=scheduled";
        } catch (final RuntimeException exception) {
            sLastDiagnostics = "permission_request=failed;exception="
                + exception.getClass().getSimpleName();
        }
    }

    private static int finish(final int state, final String details) {
        sLastDiagnostics = details + ";result=" + stateName(state);
        return state;
    }

    private static SettingValue readGlobalSetting(
        final QGCActivity activity, final String key) {
        try {
            final int value = Settings.Global.getInt(
                activity.getContentResolver(), key, Integer.MIN_VALUE);
            if (value == Integer.MIN_VALUE) {
                return new SettingValue(Integer.MIN_VALUE, "missing");
            }
            return new SettingValue(value, Integer.toString(value));
        } catch (final RuntimeException exception) {
            return new SettingValue(
                Integer.MIN_VALUE,
                "error:" + exception.getClass().getSimpleName());
        }
    }

    private static String adapterStateName(final int state) {
        switch (state) {
        case BluetoothAdapter.STATE_OFF:
            return "OFF";
        case BluetoothAdapter.STATE_TURNING_ON:
            return "TURNING_ON";
        case BluetoothAdapter.STATE_ON:
            return "ON";
        case BluetoothAdapter.STATE_TURNING_OFF:
            return "TURNING_OFF";
        case ADAPTER_STATE_BLE_TURNING_ON:
            return "BLE_TURNING_ON";
        case ADAPTER_STATE_BLE_ON:
            return "BLE_ON";
        case ADAPTER_STATE_BLE_TURNING_OFF:
            return "BLE_TURNING_OFF";
        default:
            return "OTHER(" + state + ")";
        }
    }

    private static String stateName(final int state) {
        switch (state) {
        case STATE_FULLY_OFF:
            return "FullyOff";
        case STATE_CLASSIC_ACTIVE:
            return "ClassicActive";
        case STATE_BLE_ACTIVE:
            return "BleActive";
        case STATE_SCAN_ALWAYS_ENABLED:
            return "ScanAlwaysEnabled";
        case STATE_PERMISSION_REQUIRED:
            return "PermissionRequired";
        default:
            return "Unknown";
        }
    }

    private static final class SettingValue {
        final int value;
        final String description;

        SettingValue(final int value, final String description) {
            this.value = value;
            this.description = description;
        }

        boolean isDisabled() {
            return value == 0;
        }

        boolean isEnabled() {
            return value > 0;
        }
    }
}
