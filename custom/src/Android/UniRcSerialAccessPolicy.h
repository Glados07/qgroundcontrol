#pragma once

#include <QtCore/QString>

namespace UniRcSerialAccessPolicy {

// Values mirror QGCCustomBluetoothState.getStateForUniRc().
enum class BluetoothState {
    Unknown = 0,
    OffObserved = 1,
    OnOrTransition = 2,
};

enum class Decision {
    Allow,
    BlockBluetoothActive,
    BlockBluetoothUnknown,
    WaitForBluetoothRelease,
};

bool requiresBluetoothOff(const QString &devicePath);
Decision evaluate(const QString &devicePath,
                  BluetoothState bluetoothState,
                  bool offWasPreviouslyObserved = false);

} // namespace UniRcSerialAccessPolicy
