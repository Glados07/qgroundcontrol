/****************************************************************************
 * Rouke UDP transport and acknowledgement state. No automatic command retry.
 ****************************************************************************/
#include "RoukePayloadController.h"
#include "RoukeProtocol.h"
#include "QGCLoggingCategory.h"

#include <QtCore/QByteArray>
#include <QtCore/QJsonValue>
#include <QtNetwork/QNetworkDatagram>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <limits>

QGC_LOGGING_CATEGORY(RoukePayloadLog, "gcs.custom.gimbal.rouke.payload")
// QGC's registration wrapper has no default-level constructor. This explicit
// category remains off unless enabled by --logging or Qt filter rules.
Q_LOGGING_CATEGORY(RoukePayloadTraceLog, "gcs.custom.gimbal.rouke.trace", QtInfoMsg)

namespace {
static_assert(MAVLINK_MSG_ID_VK_PAYLOAD_DATA_RELAY == 53022, "Rouke relay message ID mismatch");
static_assert(MAVLINK_MSG_ID_VK_PAYLOAD_DATA_RELAY_CRC == 37, "Rouke relay CRC mismatch");
static_assert(MAVLINK_MSG_ID_VK_PAYLOAD_DATA_RELAY_LEN == 132
              && MAVLINK_MSG_ID_VK_PAYLOAD_DATA_RELAY_MIN_LEN == 132,
              "Rouke relay message layout mismatch");
static_assert(sizeof(mavlink_vk_payload_data_relay_t::data) == 128,
              "Rouke relay data capacity mismatch");
constexpr int kCommandTimeoutMs = 3500;
constexpr int kResponseTimeoutMs = 15000;
constexpr int kSdCardFreshnessMs = 25000;
// Local recovery limits, not additional fields in the manufacturer's protocol.
constexpr int kPartialFrameTimeoutMs = 3500;
constexpr int kMaxResponseStreams = 8;
constexpr int kMaxOtherMavlinkHeaders = 16;
constexpr int kReceiveDiagnosticIntervalMs = 1000;
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

bool isParameterRead(const CameraParameter &parameter, const QString &command,
                     const QJsonObject &request)
{
    return parameter.readCommand
        && (command != QLatin1String(parameter.command)
            || (parameter.operation
                && request.value(QString::fromLatin1(parameter.operation)).toInt(-1) == 0));
}

QString traceCommand(const QString &command)
{
    if (parameterForCommand(command) || command == QStringLiteral("gimbal_speed_ctrl")
        || command == QStringLiteral("gimbal_center") || command == QStringLiteral("get_sdcard_capacity")
        || command == QStringLiteral("lr1_record") || command == QStringLiteral("lr1_switch_view")
        || command == QStringLiteral("lr1_take_photo") || command == QStringLiteral("lr1_focus")) return command;
    return QStringLiteral("<other>");
}

QStringList traceFields(const QString &command, const QJsonObject &data)
{
    QStringList fields;
    if (const auto *parameter = parameterForCommand(command)) {
        fields.append(QString::fromLatin1(parameter->field));
        if (parameter->operation) fields.append(QString::fromLatin1(parameter->operation));
    } else if (command == QStringLiteral("gimbal_speed_ctrl")) {
        fields = QStringList{QStringLiteral("yaw_speed"), QStringLiteral("pitch_speed"), QStringLiteral("roll_speed")};
    } else if (command == QStringLiteral("get_sdcard_capacity")) {
        fields = QStringList{QStringLiteral("total_capacity"), QStringLiteral("free_capacity")};
    } else if (command == QStringLiteral("lr1_record")) {
        fields.append(QStringLiteral("action"));
    } else if (command == QStringLiteral("lr1_switch_view")) {
        fields.append(QStringLiteral("view_type"));
    }
    QStringList values;
    for (const QString &field : fields) {
        const QJsonValue value = data.value(field);
        // Never include raw JSON, free-text messages or unknown/string fields.
        values.append(field + QLatin1Char('=') + (value.isUndefined() ? QStringLiteral("missing")
            : value.isDouble() ? QString::number(value.toDouble(), 'g', 16) : QStringLiteral("non-numeric")));
    }
    return values;
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
    _receiveDiagnosticTimer.setInterval(kReceiveDiagnosticIntervalMs);
    _receiveDiagnosticTimer.setTimerType(Qt::PreciseTimer);
    connect(&_receiveDiagnosticTimer, &QTimer::timeout, this, [this]() {
        if (RoukePayloadTraceLog().isDebugEnabled()) _logReceiveDiagnostics("periodic");
    });
    connect(&_sdCardQueryTimer, &QTimer::timeout, this, [this]() {
        _logTimeoutDiagnostics(QStringLiteral("get_sdcard_capacity"));
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
    // Advertise this binary's diagnostic capability even with tracing off.
    // This is a build contract, not evidence that a packet reached the peer.
    qCInfo(RoukePayloadLog).noquote()
        << QStringLiteral("ROUKE_DIAGNOSTICS schema=3 msgid=%1 len=%2 crc=%3 scope=build-contract")
               .arg(MAVLINK_MSG_ID_VK_PAYLOAD_DATA_RELAY)
               .arg(MAVLINK_MSG_ID_VK_PAYLOAD_DATA_RELAY_LEN)
               .arg(MAVLINK_MSG_ID_VK_PAYLOAD_DATA_RELAY_CRC);
    qCInfo(RoukePayloadLog) << "Rouke UDP configuration"
        << "enabled" << _enabled << "host" << _host.toString()
        << "remotePort" << _port << "requestedLocalPort" << _localPort
        << "boundLocalPort" << _socket.localPort() << "payloadAddress" << _payloadAddress
        << "available" << available();
    qCInfo(RoukePayloadLog).noquote()
        << QStringLiteral("ROUKE_RX_CONFIG session=%1 peer=%2:%3 localPort=%4 payload=%5 enabled=%6 available=%7")
               .arg(_configurationSerial).arg(_host.toString()).arg(_port).arg(_socket.localPort())
               .arg(_payloadAddress).arg(_enabled).arg(available());
    if (_enabled) _receiveDiagnosticTimer.start();
    if (RoukePayloadTraceLog().isDebugEnabled()) _logReceiveDiagnostics("configured");
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
    _receiveDiagnostics = {};
    _receiveDiagnosticTimer.stop();
    _receiveDiagnosticAge.start();
    _receiveDiagnosticLastReportAge.invalidate();
    _lastUdpAge.invalidate();
    _lastUdpSource = {};
    _lastUdpSourcePort = 0;
    _lastPeerAge.invalidate();
    _lastRelayAge.invalidate();
    _lastMatchedFrameAge.invalidate();
    _receiveDiagnosticReport = 0;
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
    _possiblyDelayedReadCommands.clear();
    _responding = false;
    _recording = false;
    _recordingKnown = false;
    _viewKnown = false;
    _discoveredPayloadAddress = -1;
    _txStatus = {};
    _rxStatus = {};
    _rxMessage = {};
    _txSpeedTraceElapsed.invalidate();
    _rxSpeedTraceElapsed.invalidate();
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

void RoukePayloadController::_logTimeoutDiagnostics(const QString &command)
{
    _logReceiveDiagnostics("timeout", command);
}

void RoukePayloadController::_logReceiveDiagnostics(const char *reason, const QString &command)
{
    const bool timeout = qstrcmp(reason, "timeout") == 0;
    if (!timeout && !RoukePayloadTraceLog().isDebugEnabled()) return;
    const auto &rx = _receiveDiagnostics;
    const QString tag = QStringLiteral(" session=%1 report=%2")
                            .arg(_configurationSerial).arg(++_receiveDiagnosticReport);
    const auto age = [](const QElapsedTimer &timer) -> qint64 {
        return timer.isValid() ? timer.elapsed() : -1;
    };
    // Keep essential stages in short separate records. Android's logging
    // backend may truncate a single message at about 1 KiB including context.
    QStringList lines;
    lines << QStringLiteral("ROUKE_RX_SNAPSHOT") + tag
        + QStringLiteral(" reason=%1 cmd=%2 peer=%3:%4 localPort=%5 payload=%6 enabled=%7 available=%8 responding=%9 ageMs=%10 lastUdpMs=%11 lastPeerMs=%12 lastRelayMs=%13 lastMatchedMs=%14 gapMs=%15 lastSource=%16:%17")
              .arg(QString::fromLatin1(reason), command.isEmpty() ? QStringLiteral("none") : traceCommand(command))
              .arg(_host.toString()).arg(_port).arg(_socket.localPort()).arg(_payloadAddress)
              .arg(_enabled).arg(available()).arg(_responding).arg(age(_receiveDiagnosticAge))
              .arg(age(_lastUdpAge)).arg(age(_lastPeerAge)).arg(age(_lastRelayAge)).arg(age(_lastMatchedFrameAge))
              .arg(age(_receiveDiagnosticLastReportAge))
              .arg(_lastUdpSource.isNull() ? QStringLiteral("none") : _lastUdpSource.toString()).arg(_lastUdpSourcePort);
    _receiveDiagnosticLastReportAge.restart();
    lines << QStringLiteral("ROUKE_RX_TRANSPORT") + tag
        + QStringLiteral(" udp=%1 bytes=%2 peer=%3 rejectAddress=%4 rejectPort=%5 badCrc=%6 incomplete=%7 other=%8 relay=%9 invalidRelay=%10 otherOverflow=%11")
              .arg(rx.datagrams).arg(rx.bytes).arg(rx.matchedDatagrams).arg(rx.rejectedAddress)
              .arg(rx.rejectedPort).arg(rx.badMavlink).arg(rx.noCompleteMavlink).arg(rx.otherMavlink)
              .arg(rx.relayMavlink).arg(rx.invalidRelay).arg(rx.otherMavlinkHeaderOverflow);
    lines << QStringLiteral("ROUKE_RX_INNER") + tag
        + QStringLiteral(" resync=%1 expired=%2 streamLimit=%3 pending=%4 valid=%5 matched=%6 rejectRoute=%7 rejectPayload=%8 unmatchedAck=%9 ignoredRead=%10")
              .arg(rx.innerResyncs).arg(rx.expiredStreams).arg(rx.streamLimit).arg(_responseStreams.size())
              .arg(rx.innerFrames).arg(rx.matchedFrames).arg(rx.rejectedRoute).arg(rx.rejectedPayload)
              .arg(rx.unmatchedCommandAcks).arg(rx.ignoredReadReports);
    if (timeout) {
        auto headerKeys = rx.otherMavlinkHeaders.keys();
        std::sort(headerKeys.begin(), headerKeys.end());
        for (const quint64 key : headerKeys) {
            lines << QStringLiteral("ROUKE_RX_OTHER") + tag
                + QStringLiteral(" id=%1 magic=%2 sys=%3 comp=%4 count=%5")
                      .arg(key & 0xffffff).arg((key >> 40) & 0xff)
                      .arg((key >> 24) & 0xff).arg((key >> 32) & 0xff)
                      .arg(rx.otherMavlinkHeaders.value(key));
        }
    }
    for (const QString &line : lines) {
        if (timeout) qCWarning(RoukePayloadLog).noquote() << line;
        else qCDebug(RoukePayloadTraceLog).noquote() << line;
    }
}

void RoukePayloadController::_rememberUncertainRequest(const QString &command, const QJsonObject &data)
{
    const auto *parameter = parameterForCommand(command);
    if (parameter && isParameterRead(*parameter, command, data)) {
        _possiblyDelayedReadCommands.insert(command);
    } else {
        _uncertainCommands.insert(command);
    }
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
    qCDebug(RoukePayloadTraceLog) << "ROUKE_TX_PENDING cmd" << traceCommand(command)
        << "lane camera serial" << _requestSerial;
    emit stateChanged();
    return true;
}

bool RoukePayloadController::_writeCommand(const QString &command, const QJsonObject &data,
                                            QString *error)
{
    if (!available()) {
        qCDebug(RoukePayloadTraceLog) << "ROUKE_TX_FAIL cmd" << traceCommand(command) << "reason endpoint-unavailable";
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
    bool trace = RoukePayloadTraceLog().isDebugEnabled();
    if (trace && command == QStringLiteral("gimbal_speed_ctrl")) {
        const bool zero = data.value(QStringLiteral("yaw_speed")).toInt() == 0
            && data.value(QStringLiteral("pitch_speed")).toInt() == 0;
        trace = !_txSpeedTraceElapsed.isValid() || _txSpeedTraceElapsed.hasExpired(1000)
            || zero != _lastTracedSpeedZero;
        if (trace) {
            _txSpeedTraceElapsed.start();
            _lastTracedSpeedZero = zero;
        }
    }
    if (trace) {
        const auto *parameter = parameterForCommand(command);
        qCDebug(RoukePayloadTraceLog) << "ROUKE_TX cmd" << traceCommand(command)
            << "operation" << (parameter ? (isParameterRead(*parameter, command, data) ? "read" : "write") : "action")
            << "payloadAddress" << _payloadAddress
            << "fields" << traceFields(command, data) << "innerBytes" << frame.size()
            << "fragments" << fragments.size();
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
        const qint64 written = _socket.writeDatagram(reinterpret_cast<const char *>(bytes), count, _host, _port);
        if (trace) {
            qCDebug(RoukePayloadTraceLog) << "ROUKE_TX_FRAGMENT cmd" << traceCommand(command)
                << "part" << index + 1 << "of" << fragments.size() << "seq" << message.seq
                << "data_len" << relay.data_len << "mavlinkPayloadBytes" << message.len
                << "datagramBytes" << count << "written" << written;
        }
        if (written != count) {
            qCDebug(RoukePayloadTraceLog) << "ROUKE_TX_FAIL cmd" << traceCommand(command)
                << "part" << index + 1 << "of" << fragments.size()
                << "socketError" << _socket.error() << "written" << written << "expected" << count;
            // Never retry an ambiguous partially delivered command automatically.
            if (index > 0) {
                _rememberUncertainRequest(command, data);
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
    qCDebug(RoukePayloadTraceLog) << "ROUKE_TX_PENDING cmd get_sdcard_capacity lane storage serial" << _sdCardRequestSerial;
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
    const bool reading = isParameterRead(*parameter, command, request);
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
    qCDebug(RoukePayloadTraceLog) << "ROUKE_TX_PENDING cmd gimbal_center lane center serial" << _centerSerial;
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
        if (!_enabled) continue;
        ++_receiveDiagnostics.datagrams;
        _receiveDiagnostics.bytes += static_cast<quint64>(datagram.data().size());
        _lastUdpAge.restart();
        _lastUdpSource = datagram.senderAddress();
        _lastUdpSourcePort = datagram.senderPort();
        if (datagram.senderAddress() != _host) {
            ++_receiveDiagnostics.rejectedAddress;
            continue;
        }
        if (datagram.senderPort() != _port) {
            ++_receiveDiagnostics.rejectedPort;
            continue;
        }
        ++_receiveDiagnostics.matchedDatagrams;
        _lastPeerAge.restart();
        // UDP preserves boundaries. Reject partial MAVLink packets instead of
        // joining bytes from unrelated datagrams into a fabricated message.
        _rxStatus = {};
        _rxMessage = {};
        const quint64 configurationSerial = _configurationSerial;
        const quint64 requestSerial = _requestSerial;
        const quint64 centerSerial = _centerSerial;
        const quint64 sdCardSerial = _sdCardRequestSerial;
        bool completeMavlink = false;
        for (const char byte : datagram.data()) {
            mavlink_message_t message{};
            mavlink_status_t status{};
            const auto framing = mavlink_frame_char_buffer(
                &_rxMessage, &_rxStatus, static_cast<uint8_t>(byte), &message, &status);
            if (framing == MAVLINK_FRAMING_BAD_CRC || framing == MAVLINK_FRAMING_BAD_SIGNATURE) {
                ++_receiveDiagnostics.badMavlink;
            } else if (framing == MAVLINK_FRAMING_OK) {
                completeMavlink = true;
                _handleMessage(message, requestSerial, centerSerial, sdCardSerial);
                if (_configurationSerial != configurationSerial) break;
            }
        }
        if (_configurationSerial == configurationSerial && !completeMavlink) {
            ++_receiveDiagnostics.noCompleteMavlink;
        }
    }
}

void RoukePayloadController::_handleMessage(const mavlink_message_t &message, quint64 requestSerial,
                                           quint64 centerSerial, quint64 sdCardSerial)
{
    if (message.magic != MAVLINK_STX || message.msgid != MAVLINK_MSG_ID_VK_PAYLOAD_DATA_RELAY) {
        ++_receiveDiagnostics.otherMavlink;
        const quint64 key = quint64(message.msgid) | (quint64(message.sysid) << 24)
            | (quint64(message.compid) << 32) | (quint64(message.magic) << 40);
        auto &headers = _receiveDiagnostics.otherMavlinkHeaders;
        if (headers.contains(key) || headers.size() < kMaxOtherMavlinkHeaders) ++headers[key];
        else ++_receiveDiagnostics.otherMavlinkHeaderOverflow;
        return;
    }
    ++_receiveDiagnostics.relayMavlink;
    _lastRelayAge.restart();
    mavlink_vk_payload_data_relay_t relay{};
    mavlink_msg_vk_payload_data_relay_decode(&message, &relay);
    if (_receiveDiagnostics.relayMavlink == 1) {
        qCDebug(RoukePayloadTraceLog).noquote()
            << QStringLiteral("ROUKE_RX_RELAY_FIRST session=%1 peer=%2:%3 localPort=%4 msgid=%5 sys=%6 comp=%7 seq=%8 len=%9 payload=%10 reserve=%11 dataLen=%12")
                   .arg(_configurationSerial).arg(_host.toString()).arg(_port).arg(_socket.localPort())
                   .arg(message.msgid).arg(message.sysid).arg(message.compid).arg(message.seq).arg(message.len)
                   .arg(relay.payload_addr).arg(relay.reserve).arg(relay.data_len);
    }
    const quint64 key = (quint64(relay.payload_addr) << 16)
        | (quint64(message.sysid) << 8) | message.compid;
    if (relay.reserve != 0 || relay.data_len == 0
        || relay.data_len > RoukeProtocol::MaxRelayDataLength) {
        ++_receiveDiagnostics.invalidRelay;
        _responseStreams.remove(key);
        return;
    }
    for (auto it = _responseStreams.begin(); it != _responseStreams.end();) {
        if (it->lastFragment.hasExpired(kPartialFrameTimeoutMs)) {
            ++_receiveDiagnostics.expiredStreams;
            it = _responseStreams.erase(it);
        }
        else ++it;
    }
    if (!_responseStreams.contains(key) && _responseStreams.size() >= kMaxResponseStreams) {
        ++_receiveDiagnostics.streamLimit;
        return;
    }
    auto &stream = _responseStreams[key];
    stream.lastFragment.start();
    struct Response {
        QJsonObject object;
        quint64 requestSerial;
        quint64 centerSerial;
        quint64 sdCardSerial;
        QStringList traceFragments;
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
            stream.traceFragments.clear();
        }
        if (RoukePayloadTraceLog().isDebugEnabled() && (index == 0 || !buffered)) {
            if (stream.traceFragments.size() < 32) {
                stream.traceFragments.append(QStringLiteral("seq=%1,data_len=%2")
                    .arg(message.seq).arg(relay.data_len));
            } else if (stream.traceFragments.size() == 32) {
                stream.traceFragments.append(QStringLiteral("further-fragments-omitted"));
            }
        }
        const auto objects = stream.decoder.append(QByteArray(1, static_cast<char>(relay.data[index])));
        if (objects.isEmpty() && stream.decoder.bufferedSize() < buffered + 1) {
            // Includes bad framing/JSON/envelopes and discarded noise; this
            // counts parser resynchronizations, not complete invalid frames.
            ++_receiveDiagnostics.innerResyncs;
            // Invalid bytes were discarded and a new header may have begun.
            stream.requestSerial = requestSerial;
            stream.centerSerial = centerSerial;
            stream.sdCardSerial = sdCardSerial;
        }
        for (const auto &object : objects) {
            responses.append({object, stream.requestSerial, stream.centerSerial, stream.sdCardSerial,
                              stream.traceFragments});
        }
    }
    if (!stream.decoder.hasPendingData()) _responseStreams.remove(key);
    const quint64 configurationSerial = _configurationSerial;
    for (const auto &response : responses) {
        _handleFrame(response.object, relay.payload_addr, response.requestSerial, response.centerSerial,
                     response.sdCardSerial, response.traceFragments);
        if (_configurationSerial != configurationSerial) break;
    }
}

void RoukePayloadController::_handleFrame(const QJsonObject &object, uint16_t payloadAddress,
                                        quint64 requestSerial, quint64 centerSerial, quint64 sdCardSerial,
                                        const QStringList &traceFragments)
{
    ++_receiveDiagnostics.innerFrames;
    if (object.value(QStringLiteral("source")).toString() != QStringLiteral("psdk")
        || object.value(QStringLiteral("target")).toString() != QStringLiteral("msdk")) {
        ++_receiveDiagnostics.rejectedRoute;
        return;
    }
    _discoveredPayloadAddress = payloadAddress;
    // Discovery is informational only; never guess or auto-assign an address.
    if (_payloadAddress < 0 || payloadAddress != _payloadAddress) {
        ++_receiveDiagnostics.rejectedPayload;
        emit stateChanged();
        return;
    }
    ++_receiveDiagnostics.matchedFrames;
    _lastMatchedFrameAge.restart();
    _responding = true;
    _responseTimer.start();
    const QString command = object.value(QStringLiteral("cmd")).toString();
    const bool isAck = object.value(QStringLiteral("type")).toString() == QStringLiteral("ack");
    const bool accepted = object.value(QStringLiteral("code")).toInt(-1) == 0;
    bool trace = RoukePayloadTraceLog().isDebugEnabled();
    if (trace && command == QStringLiteral("gimbal_speed_ctrl")) {
        trace = !_rxSpeedTraceElapsed.isValid() || _rxSpeedTraceElapsed.hasExpired(1000)
            || accepted != _lastTracedSpeedAccepted;
        if (trace) {
            _rxSpeedTraceElapsed.start();
            _lastTracedSpeedAccepted = accepted;
        }
    }
    if (trace) {
        const bool storage = command == QStringLiteral("get_sdcard_capacity");
        const bool center = command == QStringLiteral("gimbal_center");
        const bool speed = command == QStringLiteral("gimbal_speed_ctrl");
        const quint64 frameSerial = storage ? sdCardSerial : center ? centerSerial : speed ? 0 : requestSerial;
        const quint64 currentSerial = storage ? _sdCardRequestSerial : center ? _centerSerial : speed ? 0 : _requestSerial;
        const bool lanePending = storage ? _sdCardQueryPending : center ? _gimbalCenterPending
            : !speed && pending() && command == _pendingCommand;
        qCDebug(RoukePayloadTraceLog) << "ROUKE_RX cmd" << traceCommand(command)
            << "ack" << isAck << "accepted" << accepted << "code" << object.value(QStringLiteral("code")).toInt(-1)
            << "payloadAddress" << payloadAddress
            << "lane" << (storage ? "storage" : center ? "center" : speed ? "speed" : "camera")
            << "frameSerial" << frameSerial << "currentSerial" << currentSerial
            << "pendingMatch" << (lanePending && frameSerial == currentSerial)
            << "fields" << traceFields(command, object.value(QStringLiteral("data")).toObject())
            << "fragments" << traceFragments;
    }
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
        qCDebug(RoukePayloadTraceLog) << "ROUKE_RX_RESULT cmd" << traceCommand(command)
            << "success" << success << "writeOutcomeUncertain" << _uncertainCommands.contains(command);
        emit stateChanged();
        emit gimbalCenterFinished(success);
        return;
    }
    if (!isAck
        || command != _pendingCommand || !pending() || requestSerial != _requestSerial) {
        if (isAck) ++_receiveDiagnostics.unmatchedCommandAcks;
        emit stateChanged();
        return;
    }
    const auto *parameter = parameterForCommand(command);
    const bool parameterRead = parameter && isParameterRead(*parameter, command, _pendingData);
    const QJsonObject responseData = object.value(QStringLiteral("data")).toObject();
    const bool validReadReport = parameterRead
        && validParameterValue(*parameter, responseData.value(QString::fromLatin1(parameter->field)));
    const bool delayedReadPossible = _possiblyDelayedReadCommands.contains(command);
    if (accepted && parameterRead
        && (_uncertainCommands.contains(command) || delayedReadPossible) && !validReadReport) {
        ++_receiveDiagnostics.ignoredReadReports;
        qCDebug(RoukePayloadTraceLog) << "ROUKE_RX_IGNORED cmd" << traceCommand(command)
            << "reason ambiguous-read-without-valid-value";
        // GET and SET share command names for ISO, white balance and image
        // size. An empty delayed SET ACK must not consume a later GET or turn
        // its placeholder into zero. Keep waiting for a real reported value;
        // the existing deadline still bounds this request.
        emit stateChanged();
        return;
    }
    if (accepted && parameter && !parameterRead && delayedReadPossible
        && !_uncertainCommands.contains(command) && !responseData.isEmpty()) {
        const QString field = QString::fromLatin1(parameter->field);
        const QJsonValue reportedValue = responseData.value(field);
        // ISO/WB/image-size GET and SET ACKs both carry a value. After an
        // earlier GET timed out, a different value could be its late reply;
        // it must not complete this SET or replace the submitted target.
        // An equal value is only a matching target report, not transaction
        // proof. Empty setter ACK compatibility remains unchanged.
        if (!validParameterValue(*parameter, reportedValue)
            || reportedValue != _pendingData.value(field)) {
            ++_receiveDiagnostics.ignoredReadReports;
            qCDebug(RoukePayloadTraceLog) << "ROUKE_RX_IGNORED cmd" << traceCommand(command)
                << "reason delayed-read-does-not-match-write-target";
            emit stateChanged();
            return;
        }
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
    // A valid read result reports a value even after an earlier request timed
    // out. Without transaction IDs it cannot prove which request produced the
    // report or the outcome of any write. Retain the command's uncertainty for
    // future SET/action ACKs instead of permanently disabling all reads too.
    bool success = accepted && (!_uncertainCommands.contains(command)
                                || sameDirectionStopRetry || validReadReport);
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
        success = _applyParameterAck(command, data, responseData);
    }
    _pendingStartCreatedIntent = false;
    qCDebug(RoukePayloadTraceLog) << "ROUKE_RX_RESULT cmd" << traceCommand(command)
        << "success" << success << "writeOutcomeUncertain" << _uncertainCommands.contains(command)
        << "parameterKnown" << (parameter && _cameraParameters.contains(QString::fromLatin1(parameter->key)));
    _sendQueuedStop();
    emit stateChanged();
    emit commandFinished(command, success);
}

void RoukePayloadController::_gimbalCenterTimeout()
{
    if (!_gimbalCenterPending) return;
    _logTimeoutDiagnostics(QStringLiteral("gimbal_center"));
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
    _logTimeoutDiagnostics(command);
    if (command == QStringLiteral("lr1_record")
        && _pendingData.value(QStringLiteral("action")).toInt(-1) == 0) {
        _recordingStartOutcomeUncertain = true;
    }
    _pendingStartCreatedIntent = false;
    _rememberUncertainRequest(command, _pendingData);
    _pendingCommand.clear();
    _pendingData = {};
    if (command == QStringLiteral("lr1_record")) _recordingKnown = false;
    if (command == QStringLiteral("lr1_switch_view")) _viewKnown = false;
    _invalidateParameter(command);
    _lastError = tr("No acknowledgement for Rouke command %1; device state is unknown.").arg(command);
    _sendQueuedStop();
    emit stateChanged();
    emit commandFinished(command, false);
}
