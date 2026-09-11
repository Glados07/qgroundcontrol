/****************************************************************************
 * Read-only mode synchronization. No quaternion, heading or setpoint changes.
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
    // Also notify a route change when the old and new states are both Unknown.
    emit modeChanged();
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
        if (_sampleAt >= 0 || _requestPending || known()) {
            _reset();
        }
        return;
    }
    const qint64 now = _clock.elapsed();
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
        if (now >= _queryNotBefore && now - _lastQueryAt >= kQueryIntervalMs) {
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
    } else if (!known() && now - _lastQueryAt >= kQueryIntervalMs) {
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
    if (_connected() && known() && _sampleAt >= 0
        && _clock.elapsed() - _sampleAt <= kModeTimeoutMs
        && (!_isProductA8Route() || _canQueryA8()) && _gimbal->yawLock() != yawLocked()) {
        _gimbal->setYawLock(yawLocked());
    }
}

void GimbalModeController::noteModeCommandDispatched()
{
    _reset();
    // This is not an optimistic mode update. Wait for command propagation,
    // then obtain a new reply; ACK alone does not confirm physical execution.
    _queryNotBefore = _clock.elapsed() + 400;
    _lastCanQueryA8 = _canQueryA8();
}
