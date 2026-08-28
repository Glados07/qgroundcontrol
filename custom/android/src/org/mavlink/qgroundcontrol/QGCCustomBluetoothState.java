package org.mavlink.qgroundcontrol;

import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothManager;
import android.content.Context;
import android.content.pm.PackageManager;
import android.os.Build;
import android.provider.Settings;

/** QGC_CUSTOM_ANDROID_BLUETOOTH_STATE_V1
 * Supplies the conservative Bluetooth preflight for the UniRC UART2 bridge.
 */
public final class QGCCustomBluetoothState {
    public static final int STATE_UNKNOWN = 0;
    public static final int STATE_OFF_OBSERVED = 1;
    public static final int STATE_ON_OR_TRANSITION = 2;

    private static final String BLUETOOTH_CONNECT_PERMISSION =
        "android.permission.BLUETOOTH_CONNECT";

    private QGCCustomBluetoothState() {
    }

    @SuppressWarnings("deprecation")
    public static int getStateForUniRc() {
        final QGCActivity activity = QGCActivity.getInstance();
        if (activity == null) {
            return STATE_UNKNOWN;
        }

        boolean adapterReportedOff = false;
        if (canReadAdapterState(activity)) {
            try {
                final BluetoothManager manager = (BluetoothManager)
                    activity.getSystemService(Context.BLUETOOTH_SERVICE);
                final BluetoothAdapter adapter =
                    manager != null ? manager.getAdapter() : null;
                if (adapter != null) {
                    if (adapter.getState() != BluetoothAdapter.STATE_OFF) {
                        return STATE_ON_OR_TRANSITION;
                    }
                    adapterReportedOff = true;
                }
            } catch (final RuntimeException ignored) {
                // Fall through to the read-only system setting.
            }
        }

        try {
            final int enabled = Settings.Global.getInt(
                activity.getContentResolver(), "bluetooth_on", -1);
            if (enabled == 0) {
                return STATE_OFF_OBSERVED;
            }
            if (enabled > 0) {
                return STATE_ON_OR_TRANSITION;
            }
        } catch (final RuntimeException ignored) {
            // The native caller treats an unknown result as unsafe.
        }
        return adapterReportedOff ? STATE_OFF_OBSERVED : STATE_UNKNOWN;
    }

    private static boolean canReadAdapterState(final QGCActivity activity) {
        return Build.VERSION.SDK_INT < 31
            || activity.checkSelfPermission(BLUETOOTH_CONNECT_PERMISSION)
                == PackageManager.PERMISSION_GRANTED;
    }
}
