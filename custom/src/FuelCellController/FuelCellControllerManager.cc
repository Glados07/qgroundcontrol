#include "FuelCellControllerManager.h"

#include "LinkInterface.h"
#include "MAVLinkProtocol.h"
#include "MultiVehicleManager.h"
#include "Vehicle.h"
#include "VehicleLinkManager.h"

#include <QtCore/QLoggingCategory>
#include <QtCore/QRandomGenerator>
#include <limits>
#include <utility>

Q_LOGGING_CATEGORY(FuelCellControllerLog, "qgc.custom.fuelcellcontroller")

namespace {
constexpr qint64 kStatusTimeoutMs = 1000;
constexpr qint64 kRetryIntervalMs = 500;
constexpr qint64 kRequestTimeoutMs = 2000;
constexpr unsigned kMaximumSends = 3;
constexpr uint8_t kMaximumRawDeviceState = 15;
constexpr double kUnavailable = std::numeric_limits<double>::quiet_NaN();
}

FuelCellControllerManager::FuelCellControllerManager(QObject *parent)
    : QObject(parent)
{
    _clock.start();
    _nextRequestId = QRandomGenerator::global()->generate();
    auto *vehicles = MultiVehicleManager::instance();
    _activeVehicleConnection = connect(vehicles, &MultiVehicleManager::activeVehicleChanged,
                                       this, &FuelCellControllerManager::_bindVehicle);
    _bindVehicle(vehicles->activeVehicle());
    _timer.setInterval(100);
    connect(&_timer, &QTimer::timeout, this, &FuelCellControllerManager::_poll);
    _timer.start();
}

bool FuelCellControllerManager::vehicleAvailable() const
{
    return _vehicle && !_vehicle->isOfflineEditingVehicle();
}

bool FuelCellControllerManager::telemetryFresh() const
{
    return vehicleAvailable() && _hasStatus && _lastStatusAt >= 0
        && !_vehicle->vehicleLinkManager()->communicationLost()
        && _clock.elapsed() - _lastStatusAt < kStatusTimeoutMs;
}

FuelCellControllerManager::ConnectionState FuelCellControllerManager::connectionState() const
{
    return telemetryFresh() ? static_cast<ConnectionState>(_status.connection_state) : Unknown;
}

double FuelCellControllerManager::requestedPower() const { return telemetryFresh() ? _status.requested_power : kUnavailable; }
double FuelCellControllerManager::socPercent() const { return deviceDataValid() ? _status.soc * 0.1 : kUnavailable; }
double FuelCellControllerManager::dcdcOutputCurrent() const
{
    return deviceDataValid() ? _status.dcdc_output_current * 0.1 : kUnavailable;
}
int FuelCellControllerManager::fcsState() const { return deviceDataValid() ? _status.device_state : -1; }
int FuelCellControllerManager::acceptedCommand() const { return telemetryFresh() ? _status.control_command : -1; }

std::shared_ptr<LinkInterface> FuelCellControllerManager::_controlLink() const
{
    if (!vehicleAvailable() || _vehicle->vehicleLinkManager()->communicationLost()) {
        return {};
    }
    auto link = _vehicle->vehicleLinkManager()->primaryLink().lock();
    if (!link || !link->isConnected() || link->isLogReplay() || !link->mavlinkChannelIsSet()) {
        return {};
    }
    const uint8_t channel = link->mavlinkChannel();
    if (channel >= MAVLINK_COMM_NUM_BUFFERS
        || (mavlink_get_channel_status(channel)->flags & MAVLINK_STATUS_FLAG_OUT_MAVLINK1)) {
        return {}; // Message IDs 61000/61001 require MAVLink 2 on this link.
    }
    return link;
}

bool FuelCellControllerManager::canControl() const
{
    return deviceDataValid() && !commandPending() && bool(_controlLink());
}

bool FuelCellControllerManager::canEmergencyStop() const
{
    return deviceDataValid() && bool(_controlLink())
        && (!commandPending() || _request.control_command != FUEL_CELL_CONTROL_COMMAND_EMERGENCY_STOP);
}

void FuelCellControllerManager::_notifyStatus()
{
    _notifiedFresh = telemetryFresh();
    _notifiedCanControl = canControl();
    _notifiedCanEmergencyStop = canEmergencyStop();
    emit statusChanged();
}

void FuelCellControllerManager::_bindVehicle(Vehicle *vehicle)
{
    if (vehicle && vehicle == _vehicle) {
        return;
    }
    for (const auto &connection : std::as_const(_vehicleConnections)) {
        disconnect(connection);
    }
    _vehicleConnections.clear();
    const bool interrupted = commandPending();
    _vehicle = vehicle;
    _status = {};
    _hasStatus = false;
    _lastStatusAt = -1;
    _request = {};
    _sendCount = 0;
    _requestState = interrupted ? Unconfirmed : Idle;
    _lastError = interrupted ? tr("Flight controller session changed. The previous FCS request result is unknown.") : QString();
    ++_sessionRevision;
    if (_vehicle) {
        _vehicleConnections << connect(_vehicle, &QObject::destroyed, this, [this]() { _bindVehicle(nullptr); });
        _vehicleConnections << connect(_vehicle, &Vehicle::mavlinkMessageReceived,
                                       this, &FuelCellControllerManager::_handleMessage);
        auto *links = _vehicle->vehicleLinkManager();
        _vehicleConnections << connect(links, &VehicleLinkManager::allLinksRemoved,
                                       this, [this](Vehicle *) { _bindVehicle(nullptr); });
        _vehicleConnections << connect(links, &VehicleLinkManager::primaryLinkChanged,
                                       this, &FuelCellControllerManager::_poll);
        _vehicleConnections << connect(links, &VehicleLinkManager::communicationLostChanged, this, [this](bool lost) {
            if (lost) {
                // A restored heartbeat alone must not make cached FCS telemetry fresh again.
                _lastStatusAt = -1;
                ++_sessionRevision;
                if (commandPending()) {
                    _finishRequest(Unconfirmed, tr("Flight controller communication was lost. The FCS request result is unknown."));
                }
                emit sessionChanged();
            }
            _notifyStatus();
        });
    }
    emit sessionChanged();
    emit requestChanged();
    _notifyStatus();
}

void FuelCellControllerManager::shutdown()
{
    _timer.stop();
    disconnect(_activeVehicleConnection);
    _bindVehicle(nullptr);
}

void FuelCellControllerManager::_handleMessage(const mavlink_message_t &message)
{
    if (!vehicleAvailable() || message.msgid != MAVLINK_MSG_ID_FUEL_CELL_STATUS
        || message.sysid != _vehicle->id() || message.compid != _vehicle->compId()
        || _vehicle->vehicleLinkManager()->communicationLost()) {
        return;
    }
    mavlink_fuel_cell_status_t status{};
    mavlink_msg_fuel_cell_status_decode(&message, &status);
    if (status.connection_state > FUEL_CELL_CONNECTION_STATE_ERROR_OCCURRED
        || status.control_command > FUEL_CELL_CONTROL_COMMAND_EMERGENCY_STOP
        || status.device_state > kMaximumRawDeviceState
        || (status.connection_state == FUEL_CELL_CONNECTION_STATE_OK && status.soc > 1000)) {
        qCWarning(FuelCellControllerLog) << "Invalid FUEL_CELL_STATUS from" << message.sysid << message.compid;
        return;
    }
    _status = status;
    _hasStatus = true;
    _lastStatusAt = _clock.elapsed();

    // An ACK reports acceptance by PX4. Only device_state reports actual FCS progress.
    // Console changes may already have replaced an accepted command; keep that telemetry.
    if (commandPending() && status.response_request_id == _request.request_id) {
        if (status.control_result == MAV_RESULT_ACCEPTED) {
            _finishRequest(Accepted);
        } else {
            _finishRequest(Failed, _resultError(status.control_result));
        }
    }
    if (commandPending() && !deviceDataValid()) {
        _finishRequest(Unconfirmed, tr("FCS device connection was lost. The request result is unknown."));
    }
    _notifyStatus();
}

QString FuelCellControllerManager::_resultError(uint8_t result)
{
    switch (result) {
    case MAV_RESULT_TEMPORARILY_REJECTED:
        return tr("Flight controller temporarily rejected the FCS command: device is not connected or has disconnected.");
    case MAV_RESULT_DENIED:
        return tr("Flight controller denied the FCS command: request ID conflicts with the command.");
    case MAV_RESULT_UNSUPPORTED:
        return tr("Flight controller does not support this FCS command.");
    default:
        return tr("Flight controller did not accept the FCS command (result %1).").arg(result);
    }
}

void FuelCellControllerManager::_finishRequest(RequestState state, const QString &error)
{
    _requestState = state;
    _lastError = error;
    emit requestChanged();
    _notifyStatus();
}

bool FuelCellControllerManager::_rejectRequest(const QString &error)
{
    // A rejected UI action must not cancel a request already waiting for confirmation.
    _lastError = error;
    emit requestChanged();
    return false;
}

bool FuelCellControllerManager::requestStartFcs(quint32 revision)
{
    return _requestCommand(FUEL_CELL_CONTROL_COMMAND_ON, revision);
}

bool FuelCellControllerManager::requestStopFcs(quint32 revision)
{
    return _requestCommand(FUEL_CELL_CONTROL_COMMAND_OFF, revision);
}

bool FuelCellControllerManager::requestEmergencyStopFcs(quint32 revision)
{
    return _requestCommand(FUEL_CELL_CONTROL_COMMAND_EMERGENCY_STOP, revision);
}

bool FuelCellControllerManager::_requestCommand(uint8_t command, quint32 revision)
{
    if (revision != _sessionRevision) {
        return _rejectRequest(tr("Flight controller session changed. Please confirm the operation again."));
    }
    if (!deviceDataValid()) {
        return _rejectRequest(tr("FCS device status is unavailable or the device is not connected."));
    }
    if (!_controlLink()) {
        return _rejectRequest(tr("No MAVLink 2 control link is available."));
    }
    const bool emergency = command == FUEL_CELL_CONTROL_COMMAND_EMERGENCY_STOP;
    if (commandPending() && (!emergency || _request.control_command == command)) {
        return _rejectRequest(tr("Waiting for FCS command confirmation."));
    }

    // A fresh ID supersedes ordinary requests on FCS emergency stop; retries never change it.
    do {
        ++_nextRequestId;
    } while (_nextRequestId == 0 || _nextRequestId == _status.response_request_id);
    _request = {};
    _request.request_id = _nextRequestId;
    _request.target_system = static_cast<uint8_t>(_vehicle->id());
    _request.target_component = static_cast<uint8_t>(_vehicle->compId());
    _request.control_command = command;
    _sourceSystem = static_cast<uint8_t>(MAVLinkProtocol::instance()->getSystemId());
    _sourceComponent = static_cast<uint8_t>(MAVLinkProtocol::getComponentId());
    _firstSendAt = _clock.elapsed();
    _lastSendAt = _firstSendAt;
    _sendCount = 0;
    _requestState = Pending; // Set before sending: synchronous replies must see this request.
    _lastError.clear();
    const bool sent = _sendRequest();
    emit requestChanged();
    _notifyStatus();
    return sent;
}

bool FuelCellControllerManager::_sendRequest()
{
    auto link = _controlLink();
    if (!link) {
        _finishRequest(_sendCount ? Unconfirmed : Failed, tr("FCS control link is unavailable."));
        return false;
    }
    const auto request = _request;
    mavlink_message_t message{};
    mavlink_msg_fuel_cell_control_encode_chan(_sourceSystem, _sourceComponent, link->mavlinkChannel(), &message, &request);
    _lastSendAt = _clock.elapsed();
    const bool retry = _sendCount++ > 0;
    qCDebug(FuelCellControllerLog) << "FCS command" << request.control_command << "request" << request.request_id
                                  << "vehicle" << request.target_system << "send" << _sendCount;
    if (!_vehicle->sendMessageOnLinkThreadSafe(link.get(), message)) {
        if (commandPending() && _request.request_id == request.request_id) {
            _finishRequest(retry ? Unconfirmed : Failed, tr("Failed to send the FCS command."));
        }
        return false;
    }
    return true;
}

void FuelCellControllerManager::_poll()
{
    const qint64 now = _clock.elapsed();
    if (commandPending()) {
        if (!telemetryFresh() || !deviceDataValid() || !_controlLink()) {
            _finishRequest(Unconfirmed, tr("FCS status or control link is unavailable. The request result is unknown."));
        } else if (now - _firstSendAt >= kRequestTimeoutMs) {
            _finishRequest(Unconfirmed, tr("FCS request confirmation timed out. The result is unknown."));
        } else if (_sendCount < kMaximumSends && now - _lastSendAt >= kRetryIntervalMs) {
            _sendRequest();
        }
    }
    if (_notifiedFresh != telemetryFresh() || _notifiedCanControl != canControl()
        || _notifiedCanEmergencyStop != canEmergencyStop()) {
        _notifyStatus();
    }
}
