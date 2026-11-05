/****************************************************************************
 * Rouke UDP transport and acknowledgement state. No automatic command retry.
 ****************************************************************************/
#include "RoukePayloadController.h"
#include "RoukeProtocol.h"

#include <QtCore/QByteArray>
#include <QtCore/QJsonValue>
#include <QtNetwork/QNetworkDatagram>
#include <cstring>
#include <cmath>
#include <limits>

namespace {
constexpr int kCommandTimeoutMs = 3500;
constexpr int kResponseTimeoutMs = 15000;
constexpr int kSdCardFreshnessMs = 25000;
// Local recovery limits, not additional fields in the manufacturer's protocol.
constexpr int kPartialFrameTimeoutMs = 3500;
constexpr int kMaxResponseStreams = 8;
// Standard QGC sender identity; this independent payload endpoint does not
// route or impersonate an active aircraft's MAVLink system/component.
constexpr uint8_t kGroundStationSystemId = 255;
constexpr uint8_t kGroundStationComponentId = MAV_COMP_ID_MISSIONPLANNER;

struct CameraParameter {
    const char *key;
    const char *command;
    const char *field;
    int minimum;
    int maximum;
    const char *operation; // 0 = read, 1 = write, when documented.
    const char *readCommand;
};

// M400 sections 3.1.5-9, 17-18, 20-21 and 23-25. ISO/white balance,
// shutter/aperture and focus values are protocol indices/raw integers, not
// invented ISO, seconds, f-numbers or millimetres. INT_MAX is a wire integer
// representation limit, not a claimed camera capability.
constexpr CameraParameter kCameraParameters[] = {
    {"iso",              "lr1_iso",                "value",          0, 34, "action", "lr1_iso"},
    {"whiteBalance",     "lr1_white_balance",      "value",          0, 14, "action", "lr1_white_balance"},
    {"focusMode",        "lr1_focus_mode",         "mode",           0, 4, nullptr, nullptr},
    {"shutterSpeed",     "lr1_shutter_speed",      "shutter_speed",  0, std::numeric_limits<int>::max(), nullptr, nullptr},
    {"focusValue",       "lr1_focus_value_set",    "value",          0, std::numeric_limits<int>::max(), nullptr, "lr1_focus_value_get"},
    {"aperture",         "lr1_aperture",           "aperture_index", 0, std::numeric_limits<int>::max(), nullptr, nullptr},
    {"imageQuality",     "lr1_image_quality",      "quality",        1, 4, nullptr, nullptr},
    {"imageSize",        "lr1_image_size",         "size",           0, 3, "operate", "lr1_image_size"},
    {"triggerPhoto",     "lr1_trigger_photo",      "enable",         0, 1, nullptr, nullptr},
    {"shootingDistance", "lr1_shooting_distance",  "distance_m",    20, 40, nullptr, nullptr},
    {"distanceThreshold","lr1_distance_threshold", "distance_cm", 30, 15000, nullptr, nullptr},
};

const CameraParameter *parameterForKey(const QString &key)
{
    for (const auto &parameter : kCameraParameters) {
        if (key == QLatin1String(parameter.key)) return &parameter;
    }
    return nullptr;
}

const CameraParameter *parameterForCommand(const QString &command)
{
    for (const auto &parameter : kCameraParameters) {
        if (command == QLatin1String(parameter.command)
            || (parameter.readCommand && command == QLatin1String(parameter.readCommand))) return &parameter;
    }
    return nullptr;
}

bool validParameterValue(const CameraParameter &parameter, const QJsonValue &value)
{
    if (!value.isDouble()) return false;
    const double number = value.toDouble();
    return std::isfinite(number) && std::floor(number) == number
        && number >= parameter.minimum && number <= parameter.maximum;
}
}

RoukePayloadController::RoukePayloadController(QObject *parent)
    : QObject(parent)
{
    _commandTimer.setSingleShot(true);
    _commandTimer.setInterval(kCommandTimeoutMs);
    _responseTimer.setSingleShot(true);
    _responseTimer.setInterval(kResponseTimeoutMs);
    _gimbalCenterTimer.setSingleShot(true);
    _gimbalCenterTimer.setInterval(kCommandTimeoutMs);
    _sdCardQueryTimer.setSingleShot(true);
    _sdCardQueryTimer.setInterval(kCommandTimeoutMs);
    _sdCardFreshnessTimer.setSingleShot(true);
    _sdCardFreshnessTimer.setInterval(kSdCardFreshnessMs);
    connect(&_sdCardQueryTimer, &QTimer::timeout, this, [this]() {
        _sdCardQueryPending = false;
        _sdCardError = QStringLiteral("No response to the Rouke SD card query.");
        _sdCardFreshnessTimer.stop();
        _setSdCardState(false, false);
    });
    connect(&_sdCardFreshnessTimer, &QTimer::timeout, this, [this]() {
        _setSdCardState(false, false);
    });
    connect(&_socket, &QUdpSocket::readyRead, this, &RoukePayloadController::_readDatagrams);
    connect(&_commandTimer, &QTimer::timeout, this, &RoukePayloadController::_timeout);
    connect(&_gimbalCenterTimer, &QTimer::timeout, this, &RoukePayloadController::_gimbalCenterTimeout);
    connect(&_responseTimer, &QTimer::timeout, this, [this]() {
        _responding = false;
        _recordingKnown = false;
        _viewKnown = false;
        _cameraParameters.clear();
        _sdCardFreshnessTimer.stop();
        _setSdCardState(false, false);
        emit stateChanged();
    });
}

void RoukePayloadController::setConfiguration(bool enabled, const QString &host,
                                             quint16 port, quint16 localPort,
                                             int payloadAddress)
{
    const QHostAddress address(host.trimmed());
    if (_enabled == enabled && _host == address && _port == port
        && _localPort == localPort && _payloadAddress == payloadAddress
        && (!enabled || _socket.state() == QAbstractSocket::BoundState)) {
        return;
    }
    // Submit a stop to the old destination before replacing or disabling it.
    // UDP submission cannot guarantee delivery; the payload still needs its
    // own stale-setpoint timeout if the network disappears.
    if (_speedMayBeActive && available()) {
        (void) sendGimbalSpeed(0, 0);
    }
    _enabled = false;
    _reset();
    _socket.close();
    _enabled = enabled;
    _host = address;
    _port = port;
    _localPort = localPort;
    _payloadAddress = payloadAddress;
    if (!enabled) {
        _lastError.clear();
    } else if (address.protocol() != QAbstractSocket::IPv4Protocol || !port) {
        _lastError = tr("Set a valid Rouke IPv4 address and UDP port.");
    } else if (!_socket.bind(QHostAddress::AnyIPv4, localPort, QUdpSocket::DontShareAddress)) {
        _lastError = tr("Cannot bind the Rouke UDP socket: %1").arg(_socket.errorString());
    } else if (payloadAddress < 0 || payloadAddress > 65535) {
        _lastError = tr("Set the payload address supplied by the AI-box vendor before sending controls.");
    } else {
        _lastError.clear();
    }
    emit stateChanged();
}

bool RoukePayloadController::available() const
{
    return _enabled && _socket.state() == QAbstractSocket::BoundState
        && _payloadAddress >= 0 && _payloadAddress <= 65535
        && _host.protocol() == QAbstractSocket::IPv4Protocol && _port != 0;
}

void RoukePayloadController::_reset()
{
    ++_configurationSerial;
    ++_requestSerial;
    ++_centerSerial;
    ++_sdCardRequestSerial;
    _responseStreams.clear();
    const QString cancelled = _pendingCommand;
    const bool centerCancelled = _gimbalCenterPending;
    _commandTimer.stop();
    _responseTimer.stop();
    _gimbalCenterTimer.stop();
    _sdCardQueryTimer.stop();
    _sdCardFreshnessTimer.stop();
    _sdCardQueryPending = false;
    _sdCardStatusKnown = false;
    _sdCardInserted = false;
    _sdCardError.clear();
    _gimbalCenterPending = false;
    _speedMayBeActive = false;
    _speedErrorNotified = false;
    _gimbalError.clear();
    _pendingCommand.clear();
    _pendingData = {};
    _cameraParameters.clear();
    _recordingStopQueued = false;
    _pendingStartCreatedIntent = false;
    _recordingStartOutcomeUncertain = false;
    _uncertainCommands.clear();
    _responding = false;
    _recording = false;
    _recordingKnown = false;
    _viewKnown = false;
    _discoveredPayloadAddress = -1;
    _txStatus = {};
    _rxStatus = {};
    _rxMessage = {};
    if (!cancelled.isEmpty()) {
        emit commandFinished(cancelled, false);
    }
    if (centerCancelled) emit gimbalCenterFinished(false);
}

void RoukePayloadController::_setError(const QString &error)
{
    _lastError = error;
    emit stateChanged();
}

bool RoukePayloadController::sendCommand(const QString &command, const QJsonObject &data)
{
    if (command == QStringLiteral("get_sdcard_capacity")) return requestSdCardStatus();
    if (command == QStringLiteral("gimbal_center")) return centerGimbal();
    if (command == QStringLiteral("gimbal_speed_ctrl")) {
        const auto yaw = data.value(QStringLiteral("yaw_speed"));
        const auto pitch = data.value(QStringLiteral("pitch_speed"));
        const auto roll = data.value(QStringLiteral("roll_speed"));
        if (!yaw.isDouble() || !pitch.isDouble() || !roll.isDouble()
            || yaw.toDouble() != yaw.toInt() || pitch.toDouble() != pitch.toInt()
            || roll.toDouble() != 0) {
            _setError(tr("Rouke speed controls require integer yaw/pitch values and zero roll."));
            return false;
        }
        return sendGimbalSpeed(yaw.toInt(), pitch.toInt());
    }
    if (pending()) {
        _setError(tr("Wait for the pending Rouke command acknowledgement."));
        return false;
    }
    const bool recordingCommand = command == QStringLiteral("lr1_record");
    const bool startRecording = data.value(QStringLiteral("action")).toInt(-1) == 0;
    if (recordingCommand) {
        const auto action = data.value(QStringLiteral("action"));
        if (!action.isDouble() || (action.toDouble() != 0 && action.toDouble() != 1)) {
            _setError(tr("Rouke recording requires a start or stop action."));
            return false;
        }
        if (startRecording && recordingStopRequired()) {
            _setError(tr("Stop the previous Rouke recording session before starting another."));
            return false;
        }
    }
    QString error;
    if (!_writeCommand(command, data, &error)) {
        if (recordingCommand && _uncertainCommands.contains(command)) {
            auto &intent = _recordingIntents[_recordingEndpointKey()];
            intent.stopRequired = true;
            if (startRecording) _recordingStartOutcomeUncertain = true;
            else intent.stopRequested = true;
        }
        _setError(error);
        return false;
    }
    if (recordingCommand) {
        auto &intent = _recordingIntents[_recordingEndpointKey()];
        _pendingStartCreatedIntent = startRecording && !intent.stopRequired;
        intent.stopRequired = true;
        if (!startRecording) intent.stopRequested = true;
    }
    // Keep the other lane's partial ACK. Per-frame serials retire any response
    // that started before this request, including split duplicate START ACKs.
    ++_requestSerial;
    _pendingCommand = command;
    _pendingData = data;
    _lastError.clear();
    _commandTimer.start();
    emit stateChanged();
    return true;
}

bool RoukePayloadController::_writeCommand(const QString &command, const QJsonObject &data,
                                            QString *error)
{
    if (!available()) {
        *error = tr("Rouke controls need an enabled UDP endpoint and a configured payload address.");
        return false;
    }
    const QByteArray frame = RoukeProtocol::encodeRequest(command, data, error);
    if (frame.isEmpty()) {
        return false;
    }
    const auto fragments = RoukeProtocol::splitFrame(frame, error);
    if (fragments.isEmpty()) {
        return false;
    }
    // No event-loop yield between writes: another command cannot interleave
    // fragments. Each UDP datagram carries one complete MAVLink 2 packet, while
    // only its relay.data bytes participate in the inner continuous stream.
    for (int index = 0; index < fragments.size(); ++index) {
        const QByteArray &fragment = fragments.at(index);
        mavlink_vk_payload_data_relay_t relay{};
        relay.payload_addr = static_cast<uint16_t>(_payloadAddress);
        relay.reserve = 0;
        relay.data_len = static_cast<uint8_t>(fragment.size());
        std::memcpy(relay.data, fragment.constData(), static_cast<size_t>(fragment.size()));
        mavlink_message_t message{};
        mavlink_msg_vk_payload_data_relay_encode_status(
            kGroundStationSystemId, kGroundStationComponentId, &_txStatus, &message, &relay);
        uint8_t bytes[MAVLINK_MAX_PACKET_LEN]{};
        const uint16_t count = mavlink_msg_to_send_buffer(bytes, &message);
        if (_socket.writeDatagram(reinterpret_cast<const char *>(bytes), count, _host, _port) != count) {
            // Never retry an ambiguous partially delivered command automatically.
            if (index > 0) {
                _uncertainCommands.insert(command);
                if (command == QStringLiteral("lr1_record")) _recordingKnown = false;
                if (command == QStringLiteral("lr1_switch_view")) _viewKnown = false;
                _invalidateParameter(command);
            }
            *error = tr("Failed to send Rouke fragment %1/%2: %3. The command was not fully sent.")
                          .arg(index + 1).arg(fragments.size()).arg(_socket.errorString());
            return false;
        }
    }
    return true;
}

bool RoukePayloadController::takePhoto()
{
    return sendCommand(QStringLiteral("lr1_take_photo"));
}

QString RoukePayloadController::_recordingEndpointKey() const
{
    return QStringLiteral("%1:%2/%3").arg(_host.toString()).arg(_port).arg(_payloadAddress);
}

bool RoukePayloadController::recordingStopRequired() const
{
    return _recordingIntents.value(_recordingEndpointKey()).stopRequired;
}

bool RoukePayloadController::setRecording(bool recording)
{
    if (!recording && pending()) {
        auto &intent = _recordingIntents[_recordingEndpointKey()];
        intent.stopRequired = true;
        intent.stopRequested = true;
        // A user stop must survive an outstanding start/photo/focus ACK.
        // Do not enqueue a second stop behind an already pending stop.
        _recordingStopQueued = _pendingCommand != QStringLiteral("lr1_record")
            || _pendingData.value(QStringLiteral("action")).toInt() != 1;
        _lastError.clear();
        emit stateChanged();
        return true;
    }
    return sendCommand(QStringLiteral("lr1_record"), {{QStringLiteral("action"), recording ? 0 : 1}});
}

void RoukePayloadController::_sendQueuedStop()
{
    if (!_recordingStopQueued) return;
    _recordingStopQueued = false;
    (void) setRecording(false);
}

bool RoukePayloadController::setView(int viewType)
{
    if (viewType != 4 && viewType != 5) {
        _setError(tr("Rouke view must be LR1 (4) or OV2735 (5)."));
        return false;
    }
    return sendCommand(QStringLiteral("lr1_switch_view"), {{QStringLiteral("view_type"), viewType}});
}

bool RoukePayloadController::autoFocus() { return sendCommand(QStringLiteral("lr1_focus")); }

bool RoukePayloadController::requestSdCardStatus()
{
    if (_sdCardQueryPending) return false;
    QString error;
    if (!_writeCommand(QStringLiteral("get_sdcard_capacity"), {}, &error)) {
        _sdCardError = error;
        _sdCardFreshnessTimer.stop();
        _setSdCardState(false, false);
        return false;
    }
    ++_sdCardRequestSerial;
    _sdCardQueryPending = true;
    _sdCardError.clear();
    _sdCardQueryTimer.start();
    return true;
}

void RoukePayloadController::_setSdCardState(bool known, bool inserted)
{
    inserted = known && inserted;
    if (_sdCardStatusKnown == known && _sdCardInserted == inserted) return;
    _sdCardStatusKnown = known;
    _sdCardInserted = inserted;
    emit stateChanged();
}

void RoukePayloadController::_handleSdCardAck(const QJsonObject &object, quint64 sdCardSerial)
{
    if (!_sdCardQueryPending || sdCardSerial != _sdCardRequestSerial) return;
    _sdCardQueryPending = false;
    _sdCardQueryTimer.stop();
    const auto data = object.value(QStringLiteral("data")).toObject();
    const auto total = data.value(QStringLiteral("total_capacity"));
    const auto free = data.value(QStringLiteral("free_capacity"));
    const auto validCapacity = [](const QJsonValue &value) {
        const double number = value.toDouble(-1);
        // JSON numbers must be exact nonnegative integers. Capacity is MB,
        // and the vendor's long is not assumed to be a 32-bit signed int.
        return value.isDouble() && std::isfinite(number) && number >= 0
            && number <= 9007199254740991.0 && std::floor(number) == number;
    };
    const bool valid = object.value(QStringLiteral("code")).toInt(-1) == 0
        && validCapacity(total) && validCapacity(free) && total.toDouble() > 0
        && free.toDouble() <= total.toDouble();
    if (!valid) {
        // The protocol defines no absent-card code. A refusal, zero capacity
        // or malformed reply is unknown, never proof that the slot is empty.
        _sdCardError = QStringLiteral("Rouke SD card capacity could not be confirmed.");
        _sdCardFreshnessTimer.stop();
        _setSdCardState(false, false);
        return;
    }
    _sdCardError.clear();
    _sdCardFreshnessTimer.start();
    _setSdCardState(true, true);
}

QString RoukePayloadController::pendingParameter() const
{
    const auto *parameter = parameterForCommand(_pendingCommand);
    return parameter ? QString::fromLatin1(parameter->key) : QString();
}

bool RoukePayloadController::setCameraParameter(const QString &key, const QVariant &value)
{
    const auto *parameter = parameterForKey(key);
    const QJsonValue jsonValue = QJsonValue::fromVariant(value);
    if (!parameter || !validParameterValue(*parameter, jsonValue)) {
        _setError(tr("Invalid Rouke camera parameter or integer value: %1.").arg(key));
        return false;
    }
    QJsonObject data{{QString::fromLatin1(parameter->field), jsonValue.toInt()}};
    if (parameter->operation) data.insert(QString::fromLatin1(parameter->operation), 1);
    return sendCommand(QString::fromLatin1(parameter->command), data);
}

bool RoukePayloadController::requestCameraParameter(const QString &key)
{
    const auto *parameter = parameterForKey(key);
    if (!parameter || !parameter->readCommand) {
        _setError(tr("Reading this Rouke camera parameter is not supported: %1.").arg(key));
        return false;
    }
    QJsonObject data;
    if (parameter->operation) {
        data.insert(QString::fromLatin1(parameter->operation), 0);
        data.insert(QString::fromLatin1(parameter->field), 0);
    }
    return sendCommand(QString::fromLatin1(parameter->readCommand), data);
}

void RoukePayloadController::_invalidateParameter(const QString &command)
{
    const auto *parameter = parameterForCommand(command);
    if (parameter) _cameraParameters.remove(QString::fromLatin1(parameter->key));
    if (command == QStringLiteral("lr1_focus") || command == QStringLiteral("lr1_focus_mode")) {
        // A mode change may have reached the camera before the ACK was lost;
        // autofocus can then move the lens even though the mode is unknown.
        _cameraParameters.remove(QStringLiteral("focusValue"));
    }
}

bool RoukePayloadController::_applyParameterAck(const QString &command, const QJsonObject &request,
                                               const QJsonObject &response)
{
    const auto *parameter = parameterForCommand(command);
    if (!parameter) return true;
    const QString field = QString::fromLatin1(parameter->field);
    const QString key = QString::fromLatin1(parameter->key);
    const bool reading = parameter->readCommand
        && (command != QLatin1String(parameter->command)
            || (parameter->operation && request.value(QString::fromLatin1(parameter->operation)).toInt(-1) == 0));
    // A successful setter with the documented empty data acknowledges its
    // submitted value. A getter must contain the actual value. Never turn a
    // missing/malformed getter response into zero or the submitted placeholder.
    const QJsonValue value = response.contains(field) ? response.value(field)
        : reading ? QJsonValue() : request.value(field);
    if (!validParameterValue(*parameter, value)) {
        _cameraParameters.remove(key);
        _lastError = tr("Rouke returned a missing or invalid value for %1; device state is unknown.").arg(key);
        return false;
    }
    _cameraParameters.insert(key, value.toInt());
    if (key == QStringLiteral("focusMode")) {
        // A mode transition can move the lens independently of a raw value
        // previously read or set. Read it again before presenting it as current.
        _cameraParameters.remove(QStringLiteral("focusValue"));
    }
    return true;
}

bool RoukePayloadController::sendGimbalSpeed(int yaw, int pitch)
{
    QString error;
    const bool wasActive = _speedMayBeActive;
    // Retain the stop obligation even if a nonzero frame is partially written.
    if (yaw || pitch) _speedMayBeActive = true;
    if (!_writeCommand(QStringLiteral("gimbal_speed_ctrl"),
                       {{QStringLiteral("yaw_speed"), yaw},
                        {QStringLiteral("pitch_speed"), pitch},
                        {QStringLiteral("roll_speed"), 0}}, &error)) {
        _gimbalError = error;
        emit stateChanged();
        return false;
    }
    _speedMayBeActive = yaw || pitch;
    if (_speedMayBeActive && !wasActive) _speedErrorNotified = false;
    // No pending command, retry, request serial change, or camera error reset.
    return true;
}

bool RoukePayloadController::centerGimbal()
{
    if (_gimbalCenterPending) return false;
    if (_speedMayBeActive && !sendGimbalSpeed(0, 0)) return false;
    QString error;
    if (!_writeCommand(QStringLiteral("gimbal_center"), {}, &error)) {
        _gimbalError = error;
        emit stateChanged();
        return false;
    }
    ++_centerSerial;
    _gimbalCenterPending = true;
    _gimbalError.clear();
    _gimbalCenterTimer.start();
    emit stateChanged();
    return true;
}

void RoukePayloadController::_readDatagrams()
{
    while (_socket.hasPendingDatagrams()) {
        const QNetworkDatagram datagram = _socket.receiveDatagram();
        if (!_enabled || datagram.senderAddress() != _host || datagram.senderPort() != _port) {
            continue;
        }
        // UDP preserves boundaries. Reject partial MAVLink packets instead of
        // joining bytes from unrelated datagrams into a fabricated message.
        _rxStatus = {};
        _rxMessage = {};
        const quint64 configurationSerial = _configurationSerial;
        const quint64 requestSerial = _requestSerial;
        const quint64 centerSerial = _centerSerial;
        const quint64 sdCardSerial = _sdCardRequestSerial;
        for (const char byte : datagram.data()) {
            mavlink_message_t message{};
            mavlink_status_t status{};
            if (mavlink_frame_char_buffer(&_rxMessage, &_rxStatus, static_cast<uint8_t>(byte),
                                         &message, &status) == MAVLINK_FRAMING_OK) {
                _handleMessage(message, requestSerial, centerSerial, sdCardSerial);
                if (_configurationSerial != configurationSerial) break;
            }
        }
    }
}

void RoukePayloadController::_handleMessage(const mavlink_message_t &message, quint64 requestSerial,
                                           quint64 centerSerial, quint64 sdCardSerial)
{
    if (message.magic != MAVLINK_STX || message.msgid != MAVLINK_MSG_ID_VK_PAYLOAD_DATA_RELAY) return;
    mavlink_vk_payload_data_relay_t relay{};
    mavlink_msg_vk_payload_data_relay_decode(&message, &relay);
    const quint64 key = (quint64(relay.payload_addr) << 16)
        | (quint64(message.sysid) << 8) | message.compid;
    if (relay.reserve != 0 || relay.data_len == 0
        || relay.data_len > RoukeProtocol::MaxRelayDataLength) {
        _responseStreams.remove(key);
        return;
    }
    for (auto it = _responseStreams.begin(); it != _responseStreams.end();) {
        if (it->lastFragment.hasExpired(kPartialFrameTimeoutMs)) it = _responseStreams.erase(it);
        else ++it;
    }
    if (!_responseStreams.contains(key) && _responseStreams.size() >= kMaxResponseStreams) return;
    auto &stream = _responseStreams[key];
    stream.lastFragment.start();
    struct Response {
        QJsonObject object;
        quint64 requestSerial;
        quint64 centerSerial;
        quint64 sdCardSerial;
    };
    QList<Response> responses;
    // Stamp each inner frame at its start, rather than at its final fragment.
    // This allows camera, center and storage ACKs to coexist without clearing the
    // shared receive stream or accepting a stale split acknowledgement.
    for (int index = 0; index < relay.data_len; ++index) {
        const int buffered = stream.decoder.bufferedSize();
        if (!buffered) {
            stream.requestSerial = requestSerial;
            stream.centerSerial = centerSerial;
            stream.sdCardSerial = sdCardSerial;
        }
        const auto objects = stream.decoder.append(QByteArray(1, static_cast<char>(relay.data[index])));
        if (objects.isEmpty() && stream.decoder.bufferedSize() < buffered + 1) {
            // Invalid bytes were discarded and a new header may have begun.
            stream.requestSerial = requestSerial;
            stream.centerSerial = centerSerial;
            stream.sdCardSerial = sdCardSerial;
        }
        for (const auto &object : objects) {
            responses.append({object, stream.requestSerial, stream.centerSerial, stream.sdCardSerial});
        }
    }
    if (!stream.decoder.hasPendingData()) _responseStreams.remove(key);
    const quint64 configurationSerial = _configurationSerial;
    for (const auto &response : responses) {
        _handleFrame(response.object, relay.payload_addr, response.requestSerial, response.centerSerial,
                     response.sdCardSerial);
        if (_configurationSerial != configurationSerial) break;
    }
}

void RoukePayloadController::_handleFrame(const QJsonObject &object, uint16_t payloadAddress,
                                        quint64 requestSerial, quint64 centerSerial, quint64 sdCardSerial)
{
    if (object.value(QStringLiteral("source")).toString() != QStringLiteral("psdk")
        || object.value(QStringLiteral("target")).toString() != QStringLiteral("msdk")) return;
    _discoveredPayloadAddress = payloadAddress;
    // Discovery is informational only; never guess or auto-assign an address.
    if (_payloadAddress < 0 || payloadAddress != _payloadAddress) {
        emit stateChanged();
        return;
    }
    _responding = true;
    _responseTimer.start();
    const QString command = object.value(QStringLiteral("cmd")).toString();
    const bool isAck = object.value(QStringLiteral("type")).toString() == QStringLiteral("ack");
    const bool accepted = object.value(QStringLiteral("code")).toInt(-1) == 0;
    if (isAck && command == QStringLiteral("get_sdcard_capacity")) {
        _handleSdCardAck(object, sdCardSerial);
        emit stateChanged(); // Also publish endpoint liveness for unknown capacity.
        return;
    }
    if (isAck && command == QStringLiteral("gimbal_speed_ctrl")) {
        if (!accepted) {
            const QString detail = object.value(QStringLiteral("msg")).toString();
            _gimbalError = detail.isEmpty() ? tr("Rouke rejected command %1.").arg(command) : detail;
            if (!_speedErrorNotified) {
                _speedErrorNotified = true; // Set before the receiver can send a stop.
                emit gimbalControlError();
            }
        }
        emit stateChanged();
        return;
    }
    if (isAck && command == QStringLiteral("gimbal_center")) {
        if (!_gimbalCenterPending || centerSerial != _centerSerial) return;
        _gimbalCenterPending = false;
        _gimbalCenterTimer.stop();
        const bool success = accepted && !_uncertainCommands.contains(command);
        if (accepted && !success) {
            _gimbalError = tr("Rouke acknowledged %1 after an earlier timeout or partial send. Without a transaction ID, device state remains unknown until a new endpoint session.").arg(command);
        } else if (!accepted) {
            const QString detail = object.value(QStringLiteral("msg")).toString();
            _gimbalError = detail.isEmpty() ? tr("Rouke rejected command %1.").arg(command) : detail;
        } else {
            _gimbalError.clear();
        }
        emit stateChanged();
        emit gimbalCenterFinished(success);
        return;
    }
    if (!isAck
        || command != _pendingCommand || !pending() || requestSerial != _requestSerial) {
        emit stateChanged();
        return;
    }
    _commandTimer.stop();
    const QJsonObject data = _pendingData;
    _pendingCommand.clear();
    _pendingData = {};
    const bool recordingCommand = command == QStringLiteral("lr1_record");
    const bool recordingStart = data.value(QStringLiteral("action")).toInt(-1) == 0;
    // All outstanding uncertain requests being STOP makes a later STOP ACK
    // unambiguous in outcome. An uncertain START can still masquerade as that
    // ACK, so that case retains the existing endpoint-session protection.
    const bool sameDirectionStopRetry = recordingCommand && !recordingStart
        && !_recordingStartOutcomeUncertain;
    bool success = accepted && (!_uncertainCommands.contains(command) || sameDirectionStopRetry);
    if (accepted && !success) {
        _invalidateParameter(command);
        _lastError = tr("Rouke acknowledged %1 after an earlier timeout or partial send. Without a transaction ID, device state remains unknown until a new endpoint session.").arg(command);
    } else if (!accepted) {
        if (recordingCommand && recordingStart && _pendingStartCreatedIntent
            && !_uncertainCommands.contains(command)
            && !_recordingIntents.value(_recordingEndpointKey()).stopRequested) {
            _recordingIntents.remove(_recordingEndpointKey());
        }
        const QString detail = object.value(QStringLiteral("msg")).toString();
        _lastError = detail.isEmpty() ? tr("Rouke rejected command %1.").arg(command) : detail;
    } else {
        _lastError.clear();
        if (command == QStringLiteral("lr1_record")) {
            _recording = recordingStart;
            _recordingKnown = true;
            if (!recordingStart) {
                _recordingIntents.remove(_recordingEndpointKey());
                _uncertainCommands.remove(command);
            }
        } else if (command == QStringLiteral("lr1_switch_view")) {
            _viewType = data.value(QStringLiteral("view_type")).toInt();
            _viewKnown = true;
        } else if (command == QStringLiteral("lr1_take_photo")) {
            ++_photoCount;
        } else if (command == QStringLiteral("lr1_focus")) {
            _cameraParameters.remove(QStringLiteral("focusValue"));
        }
        success = _applyParameterAck(command, data, object.value(QStringLiteral("data")).toObject());
    }
    _pendingStartCreatedIntent = false;
    _sendQueuedStop();
    emit stateChanged();
    emit commandFinished(command, success);
}

void RoukePayloadController::_gimbalCenterTimeout()
{
    if (!_gimbalCenterPending) return;
    _gimbalCenterPending = false;
    const QString command = QStringLiteral("gimbal_center");
    _uncertainCommands.insert(command);
    _gimbalError = tr("No acknowledgement for Rouke command %1; device state is unknown.").arg(command);
    emit stateChanged();
    emit gimbalCenterFinished(false);
}

void RoukePayloadController::_timeout()
{
    const QString command = _pendingCommand;
    if (command.isEmpty()) return;
    if (command == QStringLiteral("lr1_record")
        && _pendingData.value(QStringLiteral("action")).toInt(-1) == 0) {
        _recordingStartOutcomeUncertain = true;
    }
    _pendingStartCreatedIntent = false;
    _pendingCommand.clear();
    _pendingData = {};
    _uncertainCommands.insert(command);
    if (command == QStringLiteral("lr1_record")) _recordingKnown = false;
    if (command == QStringLiteral("lr1_switch_view")) _viewKnown = false;
    _invalidateParameter(command);
    _lastError = tr("No acknowledgement for Rouke command %1; device state is unknown.").arg(command);
    _sendQueuedStop();
    emit stateChanged();
    emit commandFinished(command, false);
}
