#include "FuelCellTelemetry.h"

#include "MAVLinkProtocol.h"

#include <QtNetwork/QNetworkDatagram>

#include <cmath>

namespace {
constexpr quint16 kBoxPort = 14602;
constexpr int kPollIntervalMs = 500;
constexpr int kTelemetryTimeoutMs = 2000;
const QByteArray kRequest = QByteArrayLiteral("GET FUEL_CELL_TELEMETRY/1");
constexpr uint32_t kValidSystemSubstate = 1u << 15;

uint16_t systemSubstate(const mavlink_message_t &message)
{
#if MAVLINK_MSG_ID_FUEL_CELL_TELEMETRY_LEN >= 50
    return mavlink_msg_fuel_cell_telemetry_get_system_substate(&message);
#else
    // The pinned dialect describes the 46-byte base. The V1.1 substate
    // follows the first two extension bytes. MAVLink 2 omits trailing zeros.
    static_assert(MAVLINK_MSG_ID_FUEL_CELL_TELEMETRY_MIN_LEN == 46);
    static_assert(MAVLINK_MSG_ID_FUEL_CELL_TELEMETRY_CRC == 2);
    constexpr unsigned offset = 48;
    const char *payload = _MAV_PAYLOAD(&message);
    const uint16_t low = message.len > offset ? static_cast<uint8_t>(payload[offset]) : 0;
    const uint16_t high = message.len > offset + 1 ? static_cast<uint8_t>(payload[offset + 1]) : 0;
    return low | (high << 8);
#endif
}
}

FuelCellTelemetry::FuelCellTelemetry(QObject *parent)
    : FuelCellTelemetry(QHostAddress(QStringLiteral("192.168.144.110")), kBoxPort, parent)
{
}

FuelCellTelemetry::FuelCellTelemetry(const QHostAddress &boxAddress, quint16 boxPort, QObject *parent)
    : QObject(parent)
    , _boxAddress(boxAddress)
    , _boxPort(boxPort)
{
    _poll.setInterval(kPollIntervalMs);
    connect(&_poll, &QTimer::timeout, this, &FuelCellTelemetry::_requestSample);
    connect(&_socket, &QUdpSocket::readyRead, this, &FuelCellTelemetry::_readSamples);
    _expiry.setSingleShot(true);
    _expiry.setInterval(kTelemetryTimeoutMs);
    _expiry.setTimerType(Qt::PreciseTimer);
    connect(&_expiry, &QTimer::timeout, this, &FuelCellTelemetry::_clear);
    connect(MAVLinkProtocol::instance(), &MAVLinkProtocol::messageReceived, this,
            [this](LinkInterface *, const mavlink_message_t &message) { _receive(message); });
}

void FuelCellTelemetry::setSystemId(int systemId)
{
    if (_systemId == systemId) {
        return;
    }
    _systemId = systemId;
    _restartPolling();
    emit systemIdChanged();
}

bool FuelCellTelemetry::isValidBoxAddress(const QString &address)
{
    const QString text = address.trimmed();
    const QHostAddress host(text);
    return host.protocol() == QAbstractSocket::IPv4Protocol && host.toString() == text
        && host != QHostAddress(QHostAddress::AnyIPv4)
        && host != QHostAddress(QHostAddress::Broadcast) && !host.isMulticast();
}

void FuelCellTelemetry::setBoxAddress(const QString &address)
{
    if (!isValidBoxAddress(address)) {
        return;
    }
    const QHostAddress host(address.trimmed());
    if (_boxAddress != host) {
        _boxAddress = host;
        _restartPolling();
        emit endpointChanged();
    }
}

void FuelCellTelemetry::setBoxPort(int port)
{
    if (port > 0 && port <= 65535 && port != _boxPort) {
        _boxPort = static_cast<quint16>(port);
        _restartPolling();
        emit endpointChanged();
    }
}

void FuelCellTelemetry::_restartPolling()
{
    _poll.stop();
    _socket.close();
    _expiry.stop();
    _lastSampleTime.reset();
    _clear();
    if (_systemId > 0) {
        _requestSample();
        _poll.start();
    }
}

void FuelCellTelemetry::_requestSample()
{
    if (_socket.state() == QAbstractSocket::UnconnectedState
        && !_socket.bind(QHostAddress(QHostAddress::AnyIPv4), 0)) {
        return;
    }
    _socket.writeDatagram(kRequest, _boxAddress, _boxPort);
}

void FuelCellTelemetry::_readSamples()
{
    while (_socket.hasPendingDatagrams()) {
        const QNetworkDatagram datagram = _socket.receiveDatagram(MAVLINK_MAX_PACKET_LEN + 1);
        if (datagram.senderAddress() != _boxAddress || datagram.senderPort() != _boxPort) {
            continue;
        }
        const QByteArray data = datagram.data();
        if (data.size() > MAVLINK_MAX_PACKET_LEN) {
            continue;
        }
        mavlink_message_t buffer{};
        mavlink_status_t status{};
        mavlink_message_t message{};
        for (const auto byte : data) {
            if (mavlink_frame_char_buffer(&buffer, &status, static_cast<uint8_t>(byte), &message, nullptr)
                == MAVLINK_FRAMING_OK) {
                _receive(message);
            }
        }
    }
}

void FuelCellTelemetry::_clear()
{
    if (!_values.isEmpty()) {
        _values.clear();
        emit valuesChanged();
    }
}

void FuelCellTelemetry::_receive(const mavlink_message_t &message)
{
    if (_systemId == 0 || message.sysid != _systemId
        || message.compid != MAV_COMP_ID_ONBOARD_COMPUTER
        || message.msgid != MAVLINK_MSG_ID_FUEL_CELL_TELEMETRY) {
        return;
    }

    mavlink_fuel_cell_telemetry_t sample{};
    mavlink_msg_fuel_cell_telemetry_decode(&message, &sample);
    if (sample.id != 0 || _lastSampleTime == sample.time_boot_ms) {
        return;
    }
    _lastSampleTime = sample.time_boot_ms;
    _expiry.start();

    QVariantMap values;
    const auto add = [&values, &sample](const QString &name, uint32_t flag, double value) {
        if ((sample.valid_flags & flag) && std::isfinite(value)) {
            values.insert(name, value);
        }
    };
    add(QStringLiteral("inletTemperature"), FUEL_CELL_VALID_INLET_TEMPERATURE, sample.inlet_temperature);
    add(QStringLiteral("outletTemperature"), FUEL_CELL_VALID_OUTLET_TEMPERATURE, sample.outlet_temperature);
    add(QStringLiteral("fanDuty"), FUEL_CELL_VALID_FAN_DUTY, sample.fan_duty);
    add(QStringLiteral("inletPressure"), FUEL_CELL_VALID_INLET_PRESSURE, sample.inlet_pressure);
    add(QStringLiteral("stackVoltage"), FUEL_CELL_VALID_STACK_VOLTAGE, sample.stack_voltage);
    add(QStringLiteral("stackCurrent"), FUEL_CELL_VALID_STACK_CURRENT, sample.stack_current);
    add(QStringLiteral("dcdcState"), FUEL_CELL_VALID_DCDC_STATE, sample.dcdc_state);
    add(QStringLiteral("dcdcFault"), FUEL_CELL_VALID_DCDC_FAULT, sample.dcdc_fault);
    add(QStringLiteral("dcdcTemperature"), FUEL_CELL_VALID_DCDC_TEMPERATURE_RAW, sample.dcdc_temperature_raw - 40);
    add(QStringLiteral("systemSubstate"), kValidSystemSubstate, systemSubstate(message));

    if (_values != values) {
        _values = values;
        emit valuesChanged();
    }
}
