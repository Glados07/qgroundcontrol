package org.mavlink.qgroundcontrol;

public final class QGCCustomBluetoothStatePolicyTest {
    private static final int MISSING = Integer.MIN_VALUE;

    private QGCCustomBluetoothStatePolicyTest() {
    }

    public static void main(final String[] args) {
        expect(true, fullyOff(0, 0, "0"), "explicitly disabled scan setting");
        expect(true, fullyOff(0, MISSING, "missing"), "missing vendor setting");
        expect(false, fullyOff(0, MISSING, "error:SecurityException"),
            "setting read error");
        expect(false, fullyOff(0, 1, "1"), "always-on scanning enabled");
        expect(false, fullyOff(MISSING, 0, "0"), "missing bluetooth_on");
        expect(false, fullyOff(1, MISSING, "missing"), "Bluetooth enabled");
        expect(false, fullyOffWithSignals(false, true, false, 0, MISSING, "missing"),
            "adapter not off");
        expect(false, fullyOffWithSignals(true, false, true, 0, MISSING, "missing"),
            "scanner unread");
        expect(false, fullyOffWithSignals(true, true, true, 0, MISSING, "missing"),
            "BLE scanner available");
        expect(false, QGCCustomBluetoothStatePolicy.canReportFullyOff(
            true, true, true, false, 0, MISSING, "missing"), "probe failure");
    }

    private static boolean fullyOff(
        final int bluetoothOnValue,
        final int scanAlwaysValue,
        final String scanAlwaysDescription) {
        return fullyOffWithSignals(
            true, true, false, bluetoothOnValue, scanAlwaysValue, scanAlwaysDescription);
    }

    private static boolean fullyOffWithSignals(
        final boolean adapterOff,
        final boolean scannerRead,
        final boolean scannerAvailable,
        final int bluetoothOnValue,
        final int scanAlwaysValue,
        final String scanAlwaysDescription) {
        return QGCCustomBluetoothStatePolicy.canReportFullyOff(
            false,
            adapterOff,
            scannerRead,
            scannerAvailable,
            bluetoothOnValue,
            scanAlwaysValue,
            scanAlwaysDescription);
    }

    private static void expect(
        final boolean expected,
        final boolean actual,
        final String scenario) {
        if (expected != actual) {
            throw new AssertionError(scenario + ": expected " + expected + ", got " + actual);
        }
    }
}
