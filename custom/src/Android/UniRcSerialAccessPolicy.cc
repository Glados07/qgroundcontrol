#include "UniRcSerialAccessPolicy.h"

namespace UniRcSerialAccessPolicy {

bool requiresBluetoothOff(const QString &devicePath)
{
    return devicePath == QStringLiteral("/dev/ttyHS0");
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
