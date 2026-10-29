/****************************************************************************
 * Rouke UDP transport and acknowledgement state. No automatic command retry.
 ****************************************************************************/
#include "RoukePayloadController.h"
#include "RoukeProtocol.h"

#include <QtCore/QByteArray>
#include <QtCore/QJsonValue>
#include <QtNetwork/QNetworkDatagram>
#include <cstring>

namespace {
constexpr int kCommandTimeoutMs = 3500;
constexpr int kResponseTimeoutMs = 15000;
// Local recovery limits, not additional fields in the manufacturer's protocol.
constexpr int kPartialFrameTimeoutMs = 3500;
constexpr int kMaxResponseStreams = 8;
// Standard QGC sender identity; this independent payload endpoint does not
// route or impersonate an active aircraft's MAVLink system/component.
constexpr uint8_t kGroundStationSystemId = 255;
constexpr uint8_t kGroundStationComponentId = MAV_COMP_ID_MISSIONPLANNER;
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
    connect(&_socket, &QUdpSocket::readyRead, this, &RoukePayloadController::_readDatagrams);
    connect(&_commandTimer, &QTimer::timeout, this, &RoukePayloadController::_timeout);
    connect(&_gimbalCenterTimer, &QTimer::timeout, this, &RoukePayloadController::_gimbalCenterTimeout);
    connect(&_responseTimer, &QTimer::timeout, this, [this]() {
        _responding = false;
        _recordingKnown = false;
        _viewKnown = false;
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
    _responseStreams.clear();
    const QString cancelled = _pendingCommand;
    const bool centerCancelled = _gimbalCenterPending;
    _commandTimer.stop();
    _responseTimer.stop();
    _gimbalCenterTimer.stop();
    _gimbalCenterPending = false;
    _speedMayBeActive = false;
    _speedErrorNotified = false;
    _gimbalError.clear();
    _pendingCommand.clear();
    _pendingData = {};
    _recordingStopQueued = false;
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
    QString error;
    if (!_writeCommand(command, data, &error)) {
        _setError(error);
        return false;
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

bool RoukePayloadController::setRecording(bool recording)
{
    if (!recording && pending()) {
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
        for (const char byte : datagram.data()) {
            mavlink_message_t message{};
            mavlink_status_t status{};
            if (mavlink_frame_char_buffer(&_rxMessage, &_rxStatus, static_cast<uint8_t>(byte),
                                         &message, &status) == MAVLINK_FRAMING_OK) {
                _handleMessage(message, requestSerial, centerSerial);
                if (_configurationSerial != configurationSerial) break;
            }
        }
    }
}

void RoukePayloadController::_handleMessage(const mavlink_message_t &message, quint64 requestSerial,
                                           quint64 centerSerial)
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
    };
    QList<Response> responses;
    // Stamp each inner frame at its start, rather than at its final fragment.
    // This allows camera and center ACKs to coexist without clearing the
    // shared receive stream or accepting a stale split acknowledgement.
    for (int index = 0; index < relay.data_len; ++index) {
        const int buffered = stream.decoder.bufferedSize();
        if (!buffered) {
            stream.requestSerial = requestSerial;
            stream.centerSerial = centerSerial;
        }
        const auto objects = stream.decoder.append(QByteArray(1, static_cast<char>(relay.data[index])));
        if (objects.isEmpty() && stream.decoder.bufferedSize() < buffered + 1) {
            // Invalid bytes were discarded and a new header may have begun.
            stream.requestSerial = requestSerial;
            stream.centerSerial = centerSerial;
        }
        for (const auto &object : objects) {
            responses.append({object, stream.requestSerial, stream.centerSerial});
        }
    }
    if (!stream.decoder.hasPendingData()) _responseStreams.remove(key);
    const quint64 configurationSerial = _configurationSerial;
    for (const auto &response : responses) {
        _handleFrame(response.object, relay.payload_addr, response.requestSerial, response.centerSerial);
        if (_configurationSerial != configurationSerial) break;
    }
}

void RoukePayloadController::_handleFrame(const QJsonObject &object, uint16_t payloadAddress,
                                        quint64 requestSerial, quint64 centerSerial)
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
    const bool success = accepted && !_uncertainCommands.contains(command);
    if (accepted && !success) {
        _lastError = tr("Rouke acknowledged %1 after an earlier timeout or partial send. Without a transaction ID, device state remains unknown until a new endpoint session.").arg(command);
    } else if (!accepted) {
        const QString detail = object.value(QStringLiteral("msg")).toString();
        _lastError = detail.isEmpty() ? tr("Rouke rejected command %1.").arg(command) : detail;
    } else {
        _lastError.clear();
        if (command == QStringLiteral("lr1_record")) {
            _recording = data.value(QStringLiteral("action")).toInt() == 0;
            _recordingKnown = true;
        } else if (command == QStringLiteral("lr1_switch_view")) {
            _viewType = data.value(QStringLiteral("view_type")).toInt();
            _viewKnown = true;
        } else if (command == QStringLiteral("lr1_take_photo")) {
            ++_photoCount;
        }
    }
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
    _pendingCommand.clear();
    _pendingData = {};
    _uncertainCommands.insert(command);
    if (command == QStringLiteral("lr1_record")) _recordingKnown = false;
    if (command == QStringLiteral("lr1_switch_view")) _viewKnown = false;
    _lastError = tr("No acknowledgement for Rouke command %1; device state is unknown.").arg(command);
    _sendQueuedStop();
    emit stateChanged();
    emit commandFinished(command, false);
}
