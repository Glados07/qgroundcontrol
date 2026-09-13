/****************************************************************************
 * Confirmed mode synchronization and explicit user-requested mode changes.
 * No quaternion/heading conversion or position setpoints.
 ****************************************************************************/
#include "GimbalModeController.h"

#include "Fact.h"
#include "Gimbal.h"
#include "GimbalController.h"
#include <GimbalControlManager.h>
#include "MultiVehicleManager.h"
#include "QmlObjectListModel.h"
#include "Vehicle.h"
#include "VehicleLinkManager.h"

#include <QtCore/QLoggingCategory>
#include <utility>

Q_LOGGING_CATEGORY(GimbalModeLog, "qgc.custom.gimbal.mode")

namespace {
constexpr qint64 kModeTimeoutMs = 3500;
constexpr qint64 kQueryIntervalMs = 2000;
constexpr qint64 kCommandAckTimeoutMs = 5000;
constexpr qint64 kCommandFeedbackTimeoutMs = 5000;
constexpr qint64 kCommandSettleMs = 400;
}

GimbalModeController::GimbalModeController(GimbalControlManager *camera, QObject *parent)
    : QObject(parent), _camera(camera)
{
    _clock.start();
    _timer.setInterval(250);
    connect(&_timer, &QTimer::timeout, this, &GimbalModeController::_poll);
    if (_camera) {
        connect(_camera, &GimbalControlManager::gimbalModeReceived,
                this, &GimbalModeController::_handleSdkMode);
        connect(_camera, &GimbalControlManager::gimbalModeEndpointChanged,
                this, &GimbalModeController::_reset);
        connect(_camera, &GimbalControlManager::enabledChanged,
                this, &GimbalModeController::_reset);
    }
    auto *manager = MultiVehicleManager::instance();
    connect(manager, &MultiVehicleManager::activeVehicleChanged,
            this, &GimbalModeController::_bindVehicle);
    connect(manager->vehicles(), &QmlObjectListModel::countChanged, this, [this]() {
        _reset();
    });
    _bindVehicle(manager->activeVehicle());
    _timer.start();
}

GimbalModeController::~GimbalModeController()
{
    if (_camera) {
        _camera->cancelGimbalModeRequest();
    }
    _disconnect(_gimbalConnections);
    _disconnect(_vehicleConnections);
}

QObject *GimbalModeController::gimbal() const { return _gimbal.data(); }

void GimbalModeController::_disconnect(QList<QMetaObject::Connection> &connections)
{
    for (const auto &connection : std::as_const(connections)) {
        disconnect(connection);
    }
    connections.clear();
}

void GimbalModeController::_bindVehicle(Vehicle *vehicle)
{
    _disconnect(_gimbalConnections);
    _disconnect(_vehicleConnections);
    _gimbal.clear();
    _vehicle = vehicle;
    _controller = vehicle ? vehicle->gimbalController() : nullptr;
    if (_vehicle) {
        _vehicleConnections << connect(_vehicle, &QObject::destroyed, this,
                                       [this]() { _bindVehicle(nullptr); });
        _vehicleConnections << connect(_vehicle, &Vehicle::mavlinkMessageReceived,
                                       this, &GimbalModeController::_handleMessage);
        _vehicleConnections << connect(_vehicle, &Vehicle::mavCommandResult,
                                       this, &GimbalModeController::_handleCommandResult);
        if (auto *links = _vehicle->vehicleLinkManager()) {
            _vehicleConnections << connect(links, &VehicleLinkManager::communicationLostChanged,
                                           this, [this](bool) { _reset(); });
            _vehicleConnections << connect(links, &VehicleLinkManager::allLinksRemoved,
                                           this, [this](Vehicle *) { _bindVehicle(nullptr); });
        }
    }
    if (_controller) {
        _vehicleConnections << connect(_controller, &GimbalController::activeGimbalChanged,
                                       this, &GimbalModeController::_bindGimbal);
        _vehicleConnections << connect(_controller, &QObject::destroyed, this, [this]() {
            _controller.clear();
            _bindGimbal();
        });
        _vehicleConnections << connect(_controller->gimbals(), &QmlObjectListModel::countChanged,
                                       this, [this]() { _reset(); });
    }
    _bindGimbal();
}

void GimbalModeController::_bindGimbal()
{
    _disconnect(_gimbalConnections);
    _gimbal = _controller ? _controller->activeGimbal() : nullptr;
    if (_gimbal) {
        _gimbalConnections << connect(_gimbal, &QObject::destroyed, this, [this]() {
            _gimbal.clear();
            _reset();
        });
        // Keep native rate controls on the same confirmed mode as the toolbar.
        // A legacy attitude's cached flag must not overwrite fresh SDK state.
        _gimbalConnections << connect(_gimbal, &Gimbal::yawLockChanged,
                                      this, &GimbalModeController::_applyToNative);
        _gimbalConnections << connect(_gimbal->deviceId(), &Fact::rawValueChanged,
                                      this, [this]() { _reset(); });
        _gimbalConnections << connect(_gimbal->managerCompid(), &Fact::rawValueChanged,
                                      this, [this]() { _reset(); });
    }
    _reset();
}

void GimbalModeController::_reset()
{
    ++_sessionRevision;
    const bool cancelledCommand = commandPending();
    _commandPhase = Idle;
    _commandComponent = -1;
    ++_requestId;
    _requestPending = false;
    if (_camera) {
        _camera->cancelGimbalModeRequest();
    }
    _sampleMode = Unknown;
    _sampleAt = -1;
    _deviceBootMs = 0;
    _lastQueryAt = -10000;
    _queryNotBefore = 0;
    _lastCanQueryA8 = false;
    _publish(Unknown, "session reset");
    if (cancelledCommand) {
        _finishCommand(tr("Gimbal mode change cancelled: connection or gimbal changed."));
    }
    // Also notify a route change when the old and new states are both Unknown.
    emit modeChanged();
    // Also invalidate QML actions that have not reached requestYawLock yet.
    // This must not be tied to known(): ordinary sample expiry is harmless
    // to an explicit intent within the SAME connection/endpoint/route.
    emit sessionChanged();
    qCDebug(GimbalModeLog) << "Gimbal mode session reset" << "session" << _sessionRevision;
}

bool GimbalModeController::_connected() const
{
    return _vehicle && _controller && _gimbal && _vehicle->vehicleLinkManager()
        && !_vehicle->vehicleLinkManager()->communicationLost();
}

bool GimbalModeController::_isProductA8Route() const
{
    // Product wiring, confirmed by the recorded manager information: the A8
    // connected through the autopilot is manager 1 / standalone device 154.
    // NOT a generic SIYI detector and NOT the selected right-side camera.
    return _vehicle && _gimbal && _vehicle->compId() == 1
        && _gimbal->managerCompid()->rawValue().toUInt() == 1
        && _gimbal->deviceId()->rawValue().toUInt() == 154;
}

bool GimbalModeController::_canQueryA8() const
{
    // There is only one configured A8 SDK endpoint. Never bind it by recency
    // to an arbitrary vehicle or to one of several MAVLink gimbals.
    return _connected() && _isProductA8Route() && _camera && _camera->enabled()
        && MultiVehicleManager::instance()->vehicles()->count() == 1
        && _controller->gimbals()->count() == 1;
}

void GimbalModeController::_poll()
{
    if (!_connected()) {
        if (_sampleAt >= 0 || _requestPending || known() || commandPending()) {
            _reset();
        }
        return;
    }
    const qint64 now = _clock.elapsed();
    if (commandPending() && !_haveControl()) {
        _finishCommand(tr("Gimbal mode change cancelled: control was lost."));
    }
    if (commandPending() && now >= _commandDeadline) {
        _finishCommand(_commandPhase == AwaitingAck
            ? tr("Gimbal mode change failed: no flight controller acknowledgement.")
            : tr("Gimbal mode change was not confirmed by the gimbal. Check the connection and retry."));
    }
    if (_isProductA8Route()) {
        const bool canQuery = _canQueryA8();
        if (canQuery != _lastCanQueryA8) {
            _reset();
            _lastCanQueryA8 = canQuery;
        }
        if (!canQuery) {
            _publish(Unknown, "A8 endpoint unavailable or ambiguous route");
            return;
        }
        if (_commandPhase != AwaitingAck && now >= _queryNotBefore
            && now - _lastQueryAt >= kQueryIntervalMs) {
            if (_requestPending) {
                qCDebug(GimbalModeLog) << "Gimbal mode query has no valid reply"
                                     << "request" << _requestId;
            }
            _lastQueryAt = now;
            ++_requestId;
            _requestPending = _camera->requestGimbalMode(_requestId);
            qCDebug(GimbalModeLog) << "Gimbal mode query" << "vehicle" << _vehicle->id()
                                 << "manager" << 1 << "device" << 154
                                 << "request" << _requestId << "sent" << _requestPending;
        }
    } else if ((!known() || _commandPhase == AwaitingFeedback) && now - _lastQueryAt >= kQueryIntervalMs) {
        _lastQueryAt = now;
        const uint device = _gimbal->deviceId()->rawValue().toUInt();
        const uint manager = _gimbal->managerCompid()->rawValue().toUInt();
        _vehicle->sendMavCommand(device <= 6 ? manager : device,
                                MAV_CMD_REQUEST_MESSAGE, false,
                                MAVLINK_MSG_ID_GIMBAL_DEVICE_ATTITUDE_STATUS);
    }
    _publish(_sampleAt >= 0 && now - _sampleAt <= kModeTimeoutMs ? _sampleMode : Unknown,
             _isProductA8Route() ? "SIYI 0x0A" : "MAVLink 285");
}

void GimbalModeController::_handleSdkMode(quint64 requestId, quint8 mode)
{
    if (!_requestPending || requestId != _requestId || !_canQueryA8()) {
        return;
    }
    _requestPending = false;
    _sampleMode = mode == 0 ? Locked : mode == 1 ? Follow : mode == 2 ? Fpv : Unknown;
    _sampleAt = _clock.elapsed();
    qCDebug(GimbalModeLog) << "Gimbal mode SDK reply" << "vehicle" << _vehicle->id()
                         << "manager" << 1 << "device" << 154
                         << "request" << requestId << "motionMode" << mode;
    _publish(_sampleMode, "SIYI 0x0A");
    _confirmCommand();
}

void GimbalModeController::_handleMessage(const mavlink_message_t &message)
{
    if (!_connected() || message.sysid != _vehicle->id()) {
        return;
    }
    const uint manager = _gimbal->managerCompid()->rawValue().toUInt();
    const uint device = _gimbal->deviceId()->rawValue().toUInt();
    if (message.msgid == MAVLINK_MSG_ID_GIMBAL_MANAGER_STATUS) {
        mavlink_gimbal_manager_status_t status{};
        mavlink_msg_gimbal_manager_status_decode(&message, &status);
        if (message.compid == manager && status.gimbal_device_id == device) {
            qCDebug(GimbalModeLog) << "Gimbal mode manager feedback" << "vehicle" << message.sysid
                                 << "manager" << manager << "device" << device
                                 << "bootMs" << status.time_boot_ms << "flags" << status.flags;
        }
        return; // Manager-applied flags are not proof of the A8's actual mode.
    }
    if (message.msgid != MAVLINK_MSG_ID_GIMBAL_DEVICE_ATTITUDE_STATUS) {
        return;
    }
    mavlink_gimbal_device_attitude_status_t status{};
    mavlink_msg_gimbal_device_attitude_status_decode(&message, &status);
    const bool matches = device > 6
        ? message.compid == device && status.gimbal_device_id == 0
        : device >= 1 && message.compid == manager && status.gimbal_device_id == device;
    if (!matches) {
        return;
    }
    const bool locked = (status.flags & GIMBAL_DEVICE_FLAGS_YAW_LOCK) != 0;
    if (_isProductA8Route()) {
        if (known() && locked != yawLocked() && _clock.elapsed() - _lastConflictLogAt >= 2000) {
            _lastConflictLogAt = _clock.elapsed();
            qCWarning(GimbalModeLog) << "Gimbal mode feedback mismatch: keeping confirmed A8 mode"
                << "vehicle" << message.sysid << "component" << message.compid
                << "device" << status.gimbal_device_id << "bootMs" << status.time_boot_ms
                << "flags" << status.flags << "confirmedMode" << _mode;
        }
        _poll();
        return;
    }
    // Standard devices retain their protocol-defined mode. Reject duplicates
    // and small reordering without extending the mode's lifetime.
    if (_deviceBootMs && status.time_boot_ms) {
        const quint32 forward = status.time_boot_ms - _deviceBootMs;
        if (forward == 0 || (forward >= 0x80000000U && _deviceBootMs - status.time_boot_ms <= 1000)) {
            return;
        }
    }
    _deviceBootMs = status.time_boot_ms;
    _sampleMode = locked ? Locked : Follow;
    _sampleAt = _clock.elapsed();
    _publish(_sampleMode, "MAVLink 285");
    _confirmCommand();
}

void GimbalModeController::_publish(Mode mode, const char *source)
{
    const bool changed = mode != _mode;
    _mode = mode;
    _applyToNative();
    if (changed) {
        qCInfo(GimbalModeLog) << "Gimbal mode changed" << "vehicle" << (_vehicle ? _vehicle->id() : -1)
            << "manager" << (_gimbal ? _gimbal->managerCompid()->rawValue().toInt() : -1)
            << "device" << (_gimbal ? _gimbal->deviceId()->rawValue().toInt() : -1)
            << "mode" << mode << "source" << source;
        emit modeChanged();
    }
}

void GimbalModeController::_applyToNative()
{
    const bool fresh = known() && _sampleAt >= 0 && _clock.elapsed() - _sampleAt <= kModeTimeoutMs;
    // While a user command is in flight, old actual-state feedback must not
    // put native rate commands back into the previous mode. UI still uses
    // only confirmed feedback, never this desired-mode latch.
    const bool locked = commandPending() ? _targetLocked : yawLocked();
    if (_connected() && (commandPending() || fresh)
        && (!_isProductA8Route() || _canQueryA8()) && _gimbal->yawLock() != locked) {
        _gimbal->setYawLock(locked);
    }
}

bool GimbalModeController::_haveControl() const
{
    return _connected() && _gimbal->gimbalHaveControl() && !_gimbal->gimbalOthersHaveControl();
}

bool GimbalModeController::requestYawLock(bool locked, quint32 sessionRevision)
{
    if (sessionRevision != _sessionRevision) {
        qCWarning(GimbalModeLog) << "Gimbal mode stale action rejected"
            << "clickSession" << sessionRevision << "currentSession" << _sessionRevision;
        emit commandFailed(tr("Gimbal mode change cancelled: connection or gimbal changed."));
        return false;
    }
    if (commandPending()) {
        return false;
    }
    if (!_haveControl() || (_isProductA8Route() && !_canQueryA8())) {
        emit commandFailed(tr("Gimbal mode command not sent: control or the A8 connection is unavailable."));
        return false;
    }
    const int component = _gimbal->managerCompid()->rawValue().toInt();
    if (_vehicle->isMavCommandPending(component, MAV_CMD_DO_GIMBAL_MANAGER_PITCHYAW)) {
        emit commandFailed(tr("Another gimbal command is pending. Release the stick and retry."));
        return false;
    }

    // Do not re-test known() here: ownership acquisition can outlast the SDK
    // sample. The queued user intent is an explicit bool, not a fresh toggle.
    ++_requestId;
    _requestPending = false;
    if (_camera) {
        _camera->cancelGimbalModeRequest();
    }
    _lastCanQueryA8 = _canQueryA8();
    _targetLocked = locked;
    _commandComponent = component;
    _commandDeadline = _clock.elapsed() + kCommandAckTimeoutMs;
    _commandPhase = AwaitingAck;
    qCInfo(GimbalModeLog) << "Gimbal mode command requested" << "vehicle" << _vehicle->id()
        << "manager" << component << "device" << _gimbal->deviceId()->rawValue()
        << "targetLocked" << locked << "confirmedMode" << _mode << "session" << _sessionRevision;
    _applyToNative();
    // Native sendRate with zero rates sends NAN position targets and stops
    // its 500 ms rate timer. Do not reuse legacy body/earth yaw position Facts
    // for a mode-only change, or leave an old rate command repeating.
    _gimbal->setPitchRate(0.f);
    _gimbal->setYawRate(0.f);
    const auto sent = _vehicle->messagesSent();
    _controller->sendRate();
    if (_vehicle && _vehicle->messagesSent() == sent && commandPending()) {
        _finishCommand(tr("Gimbal mode command could not be sent."));
        return false;
    }
    emit commandPendingChanged();
    return true;
}

void GimbalModeController::_handleCommandResult(int vehicleId, int component, int command, int result, int failure)
{
    if (_commandPhase != AwaitingAck || !_vehicle || vehicleId != _vehicle->id()
        || component != _commandComponent || command != MAV_CMD_DO_GIMBAL_MANAGER_PITCHYAW) {
        return;
    }
    if (failure == Vehicle::MavCmdResultFailureDuplicateCommand) {
        // Vehicle permits only one outstanding command 1000 per component.
        // A joystick/screen command rejected behind ours emits this GLOBAL
        // signal, but does not terminate the original queue entry. It is not
        // a flight-controller ACK for our command: keep its original deadline.
        // If our own synchronous dispatch was rejected instead, sendRate()
        // leaves messagesSent unchanged and requestYawLock fails it there.
        qCDebug(GimbalModeLog) << "Ignoring local duplicate rejection while awaiting mode ACK"
                              << "vehicle" << vehicleId << "manager" << component;
        return;
    }
    qCInfo(GimbalModeLog) << "Gimbal mode manager ACK" << "vehicle" << vehicleId
        << "manager" << component << "result" << result << "failure" << failure
        << "targetLocked" << _targetLocked;
    if (_clock.elapsed() >= _commandDeadline) {
        _finishCommand(tr("Gimbal mode change failed: no flight controller acknowledgement."));
        return;
    }
    if (failure == 0 && result == MAV_RESULT_IN_PROGRESS) {
        return;
    }
    if (failure != 0 || result != MAV_RESULT_ACCEPTED) {
        _finishCommand(tr("Flight controller rejected the gimbal mode command (result %1, failure %2).")
                       .arg(result).arg(failure));
        return;
    }
    if (!_haveControl() || (_isProductA8Route() && !_canQueryA8())) {
        _finishCommand(tr("Gimbal mode change cancelled: control or connection was lost."));
        return;
    }
    // The product's legacy bridge flags can already say Follow while A8 is
    // physically Locked. An accepted/repeated MAVLink setpoint is therefore
    // not sufficient: explicitly select the A8 motion mode, only on a user
    // request and only AFTER the owning manager accepts that same mode.
    if (_isProductA8Route()) {
        const bool sent = _camera->setGimbalYawLock(_targetLocked);
        qCInfo(GimbalModeLog) << "Gimbal mode A8 command" << "function" << (_targetLocked ? 3 : 4)
                             << "sent" << sent;
        if (!sent) {
            _finishCommand(tr("A8 mode command could not be sent."));
            return;
        }
    }
    _commandPhase = AwaitingFeedback;
    _commandDeadline = _clock.elapsed() + kCommandFeedbackTimeoutMs;
    _queryNotBefore = _clock.elapsed() + kCommandSettleMs;
    _lastQueryAt = -10000;
}

void GimbalModeController::_confirmCommand()
{
    if (_commandPhase == AwaitingFeedback && _clock.elapsed() >= _commandDeadline) {
        _finishCommand(tr("Gimbal mode change was not confirmed by the gimbal. Check the connection and retry."));
        return;
    }
    if (_commandPhase == AwaitingFeedback && _haveControl() && _sampleAt >= _queryNotBefore
        && _sampleMode == (_targetLocked ? Locked : Follow)) {
        _finishCommand();
    }
}

void GimbalModeController::_finishCommand(const QString &error)
{
    _commandPhase = Idle;
    _commandComponent = -1;
    _applyToNative();
    qCInfo(GimbalModeLog) << "Gimbal mode command finished" << "targetLocked" << _targetLocked
                         << "confirmedMode" << _mode << "error" << error;
    emit commandPendingChanged();
    if (!error.isEmpty()) {
        emit commandFailed(error);
    }
}

void GimbalModeController::cancelModeCommand()
{
    if (commandPending()) {
        _finishCommand(tr("Gimbal mode change cancelled by another gimbal action."));
    }
}
