#include "UniRcSerialAccessPolicy.h"

namespace UniRcSerialAccessPolicy {

bool requiresBluetoothOff(const QString &devicePath)
{
    return devicePath == QStringLiteral("/dev/ttyHS0");
}

Decision evaluate(const QString &devicePath,
                  BluetoothState bluetoothState,
                  bool offWasPreviouslyObserved)
{
    if (!requiresBluetoothOff(devicePath)) {
        return Decision::Allow;
    }
    if (bluetoothState == BluetoothState::OffObserved) {
        return offWasPreviouslyObserved
            ? Decision::Allow
            : Decision::WaitForBluetoothRelease;
    }
    return bluetoothState == BluetoothState::OnOrTransition
        ? Decision::BlockBluetoothActive
        : Decision::BlockBluetoothUnknown;
}

} // namespace UniRcSerialAccessPolicy
