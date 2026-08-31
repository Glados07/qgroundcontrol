#include "UniRcSerialAccessPolicy.h"

namespace UniRcSerialAccessPolicy {

bool requiresBluetoothOff(const QString &devicePath)
{
    return devicePath == QStringLiteral("/dev/ttyHS0");
}

bool isSdkSafeIdleBaudPair(qint64 inputBaud, qint64 outputBaud)
{
    if (inputBaud != outputBaud) {
        return false;
    }

    return inputBaud == 9600
        || inputBaud == 38400
        || inputBaud == 115200;
}

PassiveCounterDecision evaluatePassiveCounters(
    bool beforeAvailable,
    bool afterAvailable,
    qint64 rxDelta,
    qint64 txDelta,
    qint64 errorDelta)
{
    if (beforeAvailable != afterAvailable) {
        return PassiveCounterDecision::AvailabilityChanged;
    }
    if (!beforeAvailable) {
        return PassiveCounterDecision::StableWithoutCounters;
    }
    if (rxDelta != 0 || txDelta != 0 || errorDelta != 0) {
        return PassiveCounterDecision::ActivityDetected;
    }
    return PassiveCounterDecision::StableWithCounters;
}

Decision evaluate(const QString &devicePath,
                  BluetoothState bluetoothState,
                  qint64 fullOffStableMs,
                  qint64 requiredStableMs)
{
    if (!requiresBluetoothOff(devicePath)) {
        return Decision::Allow;
    }

    switch (bluetoothState) {
    case BluetoothState::FullyOff:
        return fullOffStableMs >= requiredStableMs
            ? Decision::Allow
            : Decision::WaitForBluetoothRelease;
    case BluetoothState::ClassicActive:
        return Decision::BlockBluetoothClassicActive;
    case BluetoothState::BleActive:
        return Decision::BlockBluetoothBleActive;
    case BluetoothState::ScanAlwaysEnabled:
        return Decision::BlockBluetoothScanAlwaysEnabled;
    case BluetoothState::PermissionRequired:
        return Decision::BlockBluetoothPermissionRequired;
    case BluetoothState::Unknown:
        return Decision::BlockBluetoothUnknown;
    }

    return Decision::BlockBluetoothUnknown;
}

} // namespace UniRcSerialAccessPolicy
