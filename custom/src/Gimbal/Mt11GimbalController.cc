/****************************************************************************
 *
 * UniRC CH11/CH12 speed control for the UniPod MT11 gimbal.
 *
 ****************************************************************************/

#include "Mt11GimbalController.h"

#include "Mt11Sdk.h"

#include <QtCore/QtMath>

namespace {

constexpr int kChannelMinimum = 1050;
constexpr int kChannelCenter = 1500;
constexpr int kChannelMaximum = 1950;
constexpr int kChannelDeadband = 25;
constexpr int kReasonableMinimum = 900;
constexpr int kReasonableMaximum = 2100;
constexpr int kInputTimeoutMs = 350;
constexpr int kStopRetryMs = 100;
constexpr int kStopCopies = 3;

} // namespace

Mt11GimbalController::Mt11GimbalController(Mt11Sdk* sdk, QObject* parent)
    : QObject(parent)
    , _sdk(sdk)
{
    Q_ASSERT(_sdk);
    _inputWatchdog.setSingleShot(true);
    _inputWatchdog.setInterval(kInputTimeoutMs);
    _stopRetryTimer.setSingleShot(true);
    _stopRetryTimer.setInterval(kStopRetryMs);
    connect(&_inputWatchdog, &QTimer::timeout, this, &Mt11GimbalController::cancel);
    connect(&_stopRetryTimer, &QTimer::timeout, this, &Mt11GimbalController::_sendStopCopy);
    connect(_sdk, &Mt11Sdk::gimbalRotationFeedbackReceived, this, [this](bool accepted) {
        if (!accepted) {
            cancel();
        }
    });
}

Mt11GimbalController::~Mt11GimbalController()
{
    setAvailable(false);
}

void Mt11GimbalController::setAvailable(bool available)
{
    _available = available;
    if (available) {
        return;
    }

    cancel();
    // Commit retry state before each write: communicationError is synchronous
    // and may re-enter setAvailable(false) through the owning manager.
    while (_stopCopiesRemaining > 0) {
        _sendStopCopy();
    }
    _stopRetryTimer.stop();
}

int Mt11GimbalController::_channelSpeed(qint16 value)
{
    const int offset = qBound(kChannelMinimum, int(value), kChannelMaximum) - kChannelCenter;
    if (qAbs(offset) <= kChannelDeadband) {
        return 0;
    }
    // Preserve the full +/-100 endpoints while making the deadband edge
    // continuous. Any input outside the deadband produces at least speed 1.
    const int magnitude = qMax(1, qRound((qAbs(offset) - kChannelDeadband)
                                        * 100.0 / (kChannelMaximum - kChannelCenter - kChannelDeadband)));
    return offset < 0 ? -magnitude : magnitude;
}

void Mt11GimbalController::updateChannels(qint16 channel11, qint16 channel12)
{
    if (!_available || !_sdk) {
        return;
    }
    if (channel11 < kReasonableMinimum || channel11 > kReasonableMaximum
        || channel12 < kReasonableMinimum || channel12 > kReasonableMaximum) {
        cancel();
        return;
    }

    _inputWatchdog.start();
    const int yawSpeed = _channelSpeed(channel11);
    const int pitchSpeed = _channelSpeed(channel12);
    if (!_armed) {
        if (yawSpeed != 0 || pitchSpeed != 0) {
            return;
        }
        _armed = true;
        // Neutral also clears any movement left in the device by a preceding
        // session. Reconnecting with a deflected control never starts motion.
        _moving = true;
    }
    if (yawSpeed == 0 && pitchSpeed == 0) {
        _stopMotion();
        return;
    }

    // The validated UniRC stream supplies the 20 Hz refresh. No timer ever
    // replays a nonzero command when new channel samples have stopped.
    _stopRetryTimer.stop();
    _stopCopiesRemaining = 0;
    _moving = true;
    if (!_sdk->sendGimbalRotation(yawSpeed, pitchSpeed)) {
        cancel();
    }
}

void Mt11GimbalController::cancel()
{
    _armed = false;
    _inputWatchdog.stop();
    _stopMotion();
}

void Mt11GimbalController::_stopMotion()
{
    if (!_moving) {
        return;
    }
    _moving = false;
    _stopCopiesRemaining = kStopCopies;
    _sendStopCopy();
}

void Mt11GimbalController::_sendStopCopy()
{
    if (_stopCopiesRemaining <= 0) {
        return;
    }
    --_stopCopiesRemaining;
    if (_sdk) {
        (void) _sdk->sendGimbalRotation(0, 0);
    }
    if (_stopCopiesRemaining > 0) {
        _stopRetryTimer.start();
    }
}
