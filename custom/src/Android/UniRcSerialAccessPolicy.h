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

enum class PassiveCounterDecision {
    StableWithCounters,
    StableWithoutCounters,
    ActivityDetected,
    AvailabilityChanged,
};

bool requiresBluetoothOff(const QString &devicePath);
bool isSdkSafeIdleBaudPair(qint64 inputBaud, qint64 outputBaud);
PassiveCounterDecision evaluatePassiveCounters(
    bool beforeAvailable,
    bool afterAvailable,
    qint64 rxDelta,
    qint64 txDelta,
    qint64 errorDelta);
Decision evaluate(const QString &devicePath,
                  BluetoothState bluetoothState,
                  qint64 fullOffStableMs,
                  qint64 requiredStableMs);

} // namespace UniRcSerialAccessPolicy
