package org.mavlink.qgroundcontrol;

final class QGCCustomBluetoothStatePolicy {
    private QGCCustomBluetoothStatePolicy() {
    }

    static boolean canReportFullyOff(
        final boolean probeFailed,
        final boolean adapterOff,
        final boolean scannerRead,
        final boolean scannerAvailable,
        final int bluetoothOnValue,
        final int scanAlwaysValue,
        final String scanAlwaysDescription) {
        final boolean scanAlwaysOffOrUnavailable = scanAlwaysValue == 0
            || (scanAlwaysValue == Integer.MIN_VALUE
                && "missing".equals(scanAlwaysDescription));
        return !probeFailed
            && adapterOff
            && scannerRead
            && !scannerAvailable
            && bluetoothOnValue == 0
            && scanAlwaysOffOrUnavailable;
    }
}
