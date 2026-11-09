/****************************************************************************
 * Fresh input is required for every nonzero speed refresh. No motion backlog.
 ****************************************************************************/
#include "RoukeGimbalController.h"
#include "RoukePayloadController.h"
#include "QGCLoggingCategory.h"

#include <cmath>

Q_LOGGING_CATEGORY(RoukeGimbalTraceLog, "gcs.custom.gimbal.rouke.channels", QtInfoMsg)

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
        || !_reasonable(channel12)) {
        cancel();
        _traceChannels(channel11, channel12, channel14,
                       _controller->available() ? "invalid-axes" : "endpoint-unavailable");
        return;
    }
    _inputAge.restart();
    _inputWatchdog.start();

    const bool pressed = channel14 >= 1750;
    const bool released = channel14 <= 1250;
    bool centerEdge = false;
    if (!_reasonable(channel14)) {
        // The optional centering button must not disable two valid axes.
        // A recovered button needs a new release before it can trigger center.
        _buttonArmed = false;
        _buttonPressed = false;
    } else if (!_buttonArmed) {
        if (released) _buttonArmed = true;
    } else if (released) {
        _buttonPressed = false;
    } else if (pressed && !_buttonPressed) {
        _buttonPressed = true;
        centerEdge = true;
    }
    if (centerEdge) {
        _traceChannels(channel11, channel12, channel14, "center-edge");
        (void) centerGimbal();
        return;
    }
    if (_controller->gimbalCenterPending()) {
        _traceChannels(channel11, channel12, channel14, "center-pending");
        return;
    }
    if (!_axesArmed) {
        if (_neutral(channel11) && _neutral(channel12)) _axesArmed = true;
        _traceChannels(channel11, channel12, channel14, _axesArmed ? "neutral-armed" : "await-neutral");
        return;
    }

    _yawSpeed = _speed(channel11, _yawLimit, _yawReversed);
    _pitchSpeed = _speed(channel12, _pitchLimit, _pitchReversed);
    _traceChannels(channel11, channel12, channel14, (!_yawSpeed && !_pitchSpeed) ? "neutral-stop" : "motion");
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

void RoukeGimbalController::_traceChannels(qint16 channel11, qint16 channel12, qint16 channel14,
                                          const char *gate)
{
    if (!RoukeGimbalTraceLog().isDebugEnabled()) return;
    // Analog movement may change every 50 ms. Sample it once a second, while
    // preserving gate transitions such as neutral unlock, stop and centering.
    if (_traceGate == gate && _traceElapsed.isValid() && !_traceElapsed.hasExpired(1000)) return;
    _traceGate = gate;
    _traceElapsed.start();
    qCDebug(RoukeGimbalTraceLog) << "ROUKE_RC_INPUT CH11" << channel11 << "CH12" << channel12
        << "CH14" << channel14 << "gate" << gate << "available" << _controller->available()
        << "axesArmed" << _axesArmed << "centerButtonValid" << _reasonable(channel14)
        << "buttonArmed" << _buttonArmed << "buttonPressed" << _buttonPressed
        << "centerPending" << _controller->gimbalCenterPending()
        << "targetYaw" << _yawSpeed << "targetPitch" << _pitchSpeed
        << "yawLimit" << _yawLimit << "pitchLimit" << _pitchLimit
        << "yawReversed" << _yawReversed << "pitchReversed" << _pitchReversed;
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
    if (!_controller->sendGimbalSpeed(_yawSpeed, _pitchSpeed)) {
        qCDebug(RoukeGimbalTraceLog) << "ROUKE_RC_SEND_FAILED targetYaw" << _yawSpeed << "targetPitch" << _pitchSpeed;
        cancel();
    }
}

void RoukeGimbalController::_stop(bool repeat)
{
    _yawSpeed = 0;
    _pitchSpeed = 0;
    if (_motionSent) {
        qCDebug(RoukeGimbalTraceLog) << "ROUKE_RC_STOP available" << _controller->available() << "repeat" << repeat;
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
    if (_axesArmed || _motionSent || _inputAge.isValid()) {
        qCDebug(RoukeGimbalTraceLog) << "ROUKE_RC_CANCEL inputAgeMs"
            << (_inputAge.isValid() ? _inputAge.elapsed() : -1)
            << "available" << _controller->available() << "motionSent" << _motionSent
            << "centerPending" << _controller->gimbalCenterPending();
    }
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
    const bool submitted = _controller->centerGimbal();
    qCDebug(RoukeGimbalTraceLog) << "ROUKE_RC_CENTER submitted" << submitted;
    return submitted;
}
