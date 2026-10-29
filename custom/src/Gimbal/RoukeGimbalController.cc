/****************************************************************************
 * Fresh input is required for every nonzero speed refresh. No motion backlog.
 ****************************************************************************/
#include "RoukeGimbalController.h"
#include "RoukePayloadController.h"

#include <cmath>

namespace {
constexpr int kSendIntervalMs = 50;
constexpr int kInputTimeoutMs = 350;
constexpr int kStopRepeats = 2; // Three zero-speed frames including the immediate stop.
}

RoukeGimbalController::RoukeGimbalController(RoukePayloadController *controller, QObject *parent)
    : QObject(parent), _controller(controller)
{
    Q_ASSERT(_controller);
    _sendTimer.setInterval(kSendIntervalMs);
    _sendTimer.setTimerType(Qt::PreciseTimer);
    _inputWatchdog.setSingleShot(true);
    _inputWatchdog.setInterval(kInputTimeoutMs);
    connect(&_sendTimer, &QTimer::timeout, this, &RoukeGimbalController::_tick);
    connect(&_inputWatchdog, &QTimer::timeout, this, &RoukeGimbalController::cancel);
    connect(_controller, &RoukePayloadController::gimbalCenterFinished, this, [this](bool) {
        // A command ACK does not establish arrival. Require a fresh neutral
        // input after completion/failure before manual movement can resume.
        _axesArmed = false;
    });
    connect(_controller, &RoukePayloadController::gimbalControlError,
            this, &RoukeGimbalController::cancel, Qt::QueuedConnection);
}

RoukeGimbalController::~RoukeGimbalController()
{
    _stop(false);
}

void RoukeGimbalController::setConfiguration(int yawLimit, int pitchLimit,
                                            bool yawReversed, bool pitchReversed)
{
    cancel();
    _yawLimit = qBound(1, yawLimit, 10000);
    _pitchLimit = qBound(1, pitchLimit, 10000);
    _yawReversed = yawReversed;
    _pitchReversed = pitchReversed;
}

bool RoukeGimbalController::_reasonable(qint16 value) { return value >= 900 && value <= 2100; }
bool RoukeGimbalController::_neutral(qint16 value) { return value >= 1400 && value <= 1600; }

int RoukeGimbalController::_speed(qint16 value, int limit, bool reversed)
{
    if (_neutral(value)) return 0;
    const int offset = qBound(1050, int(value), 1950) - 1500;
    const int magnitude = qMax(1, qRound((std::abs(offset) - 100) * limit / 350.0));
    const int sign = offset < 0 ? -1 : 1;
    return magnitude * sign * (reversed ? -1 : 1);
}

void RoukeGimbalController::updateChannels(qint16 channel11, qint16 channel12, qint16 channel14)
{
    if (!_controller->available() || !_reasonable(channel11)
        || !_reasonable(channel12) || !_reasonable(channel14)) {
        cancel();
        return;
    }
    _inputAge.restart();
    _inputWatchdog.start();

    const bool pressed = channel14 >= 1750;
    const bool released = channel14 <= 1250;
    bool centerEdge = false;
    if (!_buttonArmed) {
        if (released) _buttonArmed = true;
    } else if (released) {
        _buttonPressed = false;
    } else if (pressed && !_buttonPressed) {
        _buttonPressed = true;
        centerEdge = true;
    }
    if (centerEdge) {
        (void) centerGimbal();
        return;
    }
    if (_controller->gimbalCenterPending()) return;
    if (!_axesArmed) {
        if (_neutral(channel11) && _neutral(channel12)) _axesArmed = true;
        return;
    }

    _yawSpeed = _speed(channel11, _yawLimit, _yawReversed);
    _pitchSpeed = _speed(channel12, _pitchLimit, _pitchReversed);
    if (!_yawSpeed && !_pitchSpeed) {
        _stop(true);
    } else {
        _stopRepeats = 0;
        if (!_sendTimer.isActive()) {
            _tick();
            if (_motionSent) _sendTimer.start();
        }
    }
}

void RoukeGimbalController::_tick()
{
    if (_stopRepeats > 0) {
        --_stopRepeats;
        if (_controller->available()) (void) _controller->sendGimbalSpeed(0, 0);
        if (!_stopRepeats) _sendTimer.stop();
        return;
    }
    if (!_controller->available() || !_inputAge.isValid()
        || _inputAge.elapsed() >= kInputTimeoutMs || _controller->gimbalCenterPending()) {
        cancel();
        return;
    }
    if (!_yawSpeed && !_pitchSpeed) {
        _sendTimer.stop();
        return;
    }
    // Even a failed multi-packet write may have delivered part of the command.
    _motionSent = true;
    if (!_controller->sendGimbalSpeed(_yawSpeed, _pitchSpeed)) cancel();
}

void RoukeGimbalController::_stop(bool repeat)
{
    _yawSpeed = 0;
    _pitchSpeed = 0;
    if (_motionSent) {
        _motionSent = false;
        if (_controller->available()) {
            (void) _controller->sendGimbalSpeed(0, 0);
            _stopRepeats = repeat ? kStopRepeats : 0;
        }
    }
    if (repeat && _stopRepeats > 0) {
        if (!_sendTimer.isActive()) _sendTimer.start();
    }
    else {
        _stopRepeats = 0;
        _sendTimer.stop();
    }
}

void RoukeGimbalController::cancel()
{
    _axesArmed = false;
    _buttonArmed = false;
    _buttonPressed = false;
    _inputWatchdog.stop();
    _inputAge.invalidate();
    _stop(true);
}

bool RoukeGimbalController::centerGimbal()
{
    _axesArmed = false;
    // No delayed zero-speed packet after centering: it could interrupt the
    // manufacturer's position command. Stop first, then center once.
    _stop(false);
    return _controller->centerGimbal();
}
