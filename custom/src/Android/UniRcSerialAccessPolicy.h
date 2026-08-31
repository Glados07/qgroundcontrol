#pragma once

#include <QtCore/QString>
#include <QtCore/QtGlobal>

namespace UniRcSerialAccessPolicy {

// Values mirror QGCCustomBluetoothState.getStateForUniRc().
enum class BluetoothState {
    Unknown = 0,
    FullyOff = 1,
    ClassicActive = 2,
    BleActive = 3,
    ScanAlwaysEnabled = 4,
    PermissionRequired = 5,
};

enum class Decision {
    Allow,
    BlockBluetoothClassicActive,
    BlockBluetoothBleActive,
    BlockBluetoothScanAlwaysEnabled,
    BlockBluetoothPermissionRequired,
    BlockBluetoothUnknown,
    WaitForBluetoothRelease,
};

bool requiresBluetoothOff(const QString &devicePath);
Decision evaluate(const QString &devicePath,
                  BluetoothState bluetoothState,
                  qint64 fullOffStableMs,
                  qint64 requiredStableMs);

} // namespace UniRcSerialAccessPolicy
