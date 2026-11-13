#include "RoukePayloadController.h"
#include "RoukeProtocol.h"
#include "RoukeNativeMavlinkTrace.h"
#include "RoukeRawMavlinkTrace.h"
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QLoggingCategory>
#include <QtNetwork/QNetworkDatagram>
#include <QtTest/QTest>
#include <QtTest/QSignalSpy>
#include <cstring>
#include <limits>

namespace {
// Capture only this controller's diagnostics; preserve QtTest's normal handler
// for assertions and unrelated categories. These UDP tests run on one thread.
class PayloadLogCapture {
public:
    PayloadLogCapture()
    {
        current = this;
        previous = qInstallMessageHandler(handleMessage);
    }
    ~PayloadLogCapture()
    {
        qInstallMessageHandler(previous);
        current = nullptr;
    }
    QStringList messages;

private:
    static void handleMessage(QtMsgType type, const QMessageLogContext &context, const QString &message)
    {
        if (current && (qstrcmp(context.category, "gcs.custom.gimbal.rouke.payload") == 0
                        || qstrcmp(context.category, "gcs.custom.gimbal.rouke.trace") == 0
                        || qstrcmp(context.category, "gcs.custom.gimbal.rouke.raw") == 0)) {
            current->messages.append(message);
        } else if (current && current->previous) {
            current->previous(type, context, message);
        }
    }
    inline static PayloadLogCapture *current = nullptr;
    QtMessageHandler previous = nullptr;
};

class PayloadTraceFilter {
public:
    PayloadTraceFilter() { QLoggingCategory::setFilterRules("gcs.custom.gimbal.rouke.trace.debug=true"); }
    ~PayloadTraceFilter() { QLoggingCategory::setFilterRules({}); }
};

class PayloadRawTraceFilter {
public:
    PayloadRawTraceFilter() {
        QLoggingCategory::setFilterRules("gcs.custom.gimbal.rouke.trace.debug=true\ngcs.custom.gimbal.rouke.raw.debug=true");
    }
    ~PayloadRawTraceFilter() { QLoggingCategory::setFilterRules({}); }
};

// Exact vendor DATA samples, not a claim that their complete outer wire header
// or checksum was supplied. Tests wrap these bytes with the real generated SDK.
QByteArray vendorRelayData(bool heartbeat)
{
    return QByteArray::fromHex(heartbeat
        ? "f0e06f007b22636d64223a22686561727462656174222c22636f6465223a302c2264617461223a7b2269734f73646b4f6e6c696e65223a747275657d2c226d7367223a22222c22736f75726365223a227073646b222c22746172676574223a226d73646b222c2274797065223a22726571227d00f1e1"
        : "22736f75726365223a227073646b222c22746172676574223a226d73646b222c2274797065223a2261636b227d00f1e1");
}

QByteArray loggedHex(const QStringList &lines, const QString &prefix)
{
    QByteArray hex;
    for (const auto &line : lines) {
        if (!line.startsWith(prefix)) continue;
        const int index = line.indexOf(" hex=");
        if (index >= 0) hex += line.mid(index + 5).section(' ', 0, 0).toLatin1();
    }
    return QByteArray::fromHex(hex);
}

// Deliberately independent of the production frame encoder and splitter.
QByteArray rawFrame(const QJsonObject &object)
{
    const QByteArray json = QJsonDocument(object).toJson(QJsonDocument::Compact);
    QByteArray frame = QByteArray::fromHex("f0e0");
    frame.append(static_cast<char>(json.size() & 0xff));
    frame.append(static_cast<char>((json.size() >> 8) & 0xff));
    frame.append(json);
    frame.append(QByteArray::fromHex("00f1e1"));
    return frame;
}

QByteArray fragmentBytes(const QByteArray &fragment, int address = 7, int system = 42,
                         int component = 77, int sequence = 0, int lengthOverride = -1, int reserve = 0)
{
    Q_ASSERT(fragment.size() <= 128);
    if (fragment.size() > 128) return {}; // Keep Release fixtures memory-safe too.
    mavlink_vk_payload_data_relay_t relay{};
    relay.payload_addr = static_cast<uint16_t>(address);
    relay.reserve = static_cast<uint8_t>(reserve);
    relay.data_len = static_cast<uint8_t>(lengthOverride < 0 ? fragment.size() : lengthOverride);
    std::memcpy(relay.data, fragment.constData(), static_cast<size_t>(fragment.size()));
    mavlink_message_t message{};
    mavlink_status_t status{};
    status.current_tx_seq = static_cast<uint8_t>(sequence);
    mavlink_msg_vk_payload_data_relay_encode_status(system, component, &status, &message, &relay);
    uint8_t bytes[MAVLINK_MAX_PACKET_LEN]{};
    const auto count = mavlink_msg_to_send_buffer(bytes, &message);
    return QByteArray(reinterpret_cast<char *>(bytes), count);
}

bool decodeMavlinkWire(const QByteArray &bytes, mavlink_message_t *message)
{
    mavlink_message_t rx{};
    mavlink_status_t parser{}, status{};
    int complete = 0;
    for (const char byte : bytes) {
        if (mavlink_frame_char_buffer(&rx, &parser, uint8_t(byte), message, &status) == MAVLINK_FRAMING_OK) ++complete;
    }
    return complete == 1;
}

QByteArray untrimmedRelayWire(const QByteArray &trimmed)
{
    const int payloadSize = uint8_t(trimmed.at(1));
    QByteArray wire = trimmed.left(10 + payloadSize);
    wire.append(QByteArray(MAVLINK_MSG_ID_VK_PAYLOAD_DATA_RELAY_LEN - payloadSize, '\0'));
    wire[1] = char(MAVLINK_MSG_ID_VK_PAYLOAD_DATA_RELAY_LEN);
    uint16_t crc = crc_calculate(reinterpret_cast<const uint8_t *>(wire.constData() + 1), uint16_t(wire.size() - 1));
    crc_accumulate(MAVLINK_MSG_ID_VK_PAYLOAD_DATA_RELAY_CRC, &crc);
    wire.append(char(crc & 0xff));
    wire.append(char(crc >> 8));
    return wire;
}

QByteArray messageBytes(const QJsonObject &object, int address = 7, int lengthOverride = -1)
{
    const auto frame = rawFrame(object);
    Q_ASSERT(frame.size() <= 128); // Long frames must use explicit fragment fixtures.
    if (frame.size() > 128) return {};
    return fragmentBytes(frame, address, 42, 77, 0, lengthOverride);
}

QJsonObject ack(const QString &command, int code = 0)
{
    return {{"source", "psdk"}, {"target", "msdk"}, {"type", "ack"},
            {"cmd", command}, {"code", code}, {"msg", ""}, {"data", QJsonObject{}}};
}

void sendFragment(QUdpSocket &box, const RoukePayloadController &controller, const QByteArray &fragment,
                  int address = 7, int system = 42, int component = 77, int sequence = 0)
{
    const auto bytes = fragmentBytes(fragment, address, system, component, sequence);
    QCOMPARE(box.writeDatagram(bytes, QHostAddress::LocalHost, controller.boundPort()), qint64(bytes.size()));
}

void sendStream(QUdpSocket &box, const RoukePayloadController &controller,
                const QJsonObject &object, int address = 7)
{
    const auto frame = rawFrame(object);
    for (int offset = 0; offset < frame.size(); offset += 128) {
        sendFragment(box, controller, frame.mid(offset, 128), address);
    }
}

QJsonObject sdCardAck(const QJsonObject &capacity = {{"total_capacity", 32000}, {"free_capacity", 17000}},
                      int code = 0)
{
    auto response = ack("get_sdcard_capacity", code);
    response["data"] = capacity;
    return response;
}

bool receiveRelay(QUdpSocket &box, mavlink_message_t *message, mavlink_vk_payload_data_relay_t *relay)
{
    if (!box.hasPendingDatagrams() && !box.waitForReadyRead(1000)) return false;
    const auto datagram = box.receiveDatagram();
    mavlink_message_t rx{};
    mavlink_status_t status{}, result{};
    int packets = 0;
    for (const char byte : datagram.data()) {
        if (mavlink_frame_char_buffer(&rx, &status, static_cast<uint8_t>(byte), message, &result)
            == MAVLINK_FRAMING_OK) ++packets;
    }
    if (packets != 1 || message->magic != MAVLINK_STX
        || message->msgid != MAVLINK_MSG_ID_VK_PAYLOAD_DATA_RELAY
        || datagram.data().size() != message->len + MAVLINK_NUM_NON_PAYLOAD_BYTES) return false;
    mavlink_msg_vk_payload_data_relay_decode(message, relay);
    return true;
}

void send(QUdpSocket &box, const RoukePayloadController &controller,
          const QJsonObject &object, int address = 7, int lengthOverride = -1)
{
    const auto bytes = messageBytes(object, address, lengthOverride);
    QCOMPARE(box.writeDatagram(bytes, QHostAddress::LocalHost, controller.boundPort()), qint64(bytes.size()));
}

QJsonObject receiveRequest(QUdpSocket &box)
{
    if (!box.hasPendingDatagrams()) box.waitForReadyRead(1000);
    const auto datagram = box.receiveDatagram();
    mavlink_message_t rx{}, message{};
    mavlink_status_t status{}, result{};
    for (const char byte : datagram.data()) {
        if (mavlink_frame_char_buffer(&rx, &status, static_cast<uint8_t>(byte), &message, &result)
            != MAVLINK_FRAMING_OK) continue;
        if (message.magic != MAVLINK_STX || message.msgid != MAVLINK_MSG_ID_VK_PAYLOAD_DATA_RELAY) return {};
        mavlink_vk_payload_data_relay_t relay{};
        mavlink_msg_vk_payload_data_relay_decode(&message, &relay);
        if (relay.payload_addr != 7 || relay.reserve != 0) return {};
        QJsonObject object;
        if (RoukeProtocol::decodeFrame(QByteArray(reinterpret_cast<char *>(relay.data), relay.data_len), &object)) return object;
    }
    return {};
}

QJsonObject receiveStreamRequest(QUdpSocket &box)
{
    QByteArray frame;
    for (int index = 0; index < 16; ++index) {
        mavlink_message_t message{};
        mavlink_vk_payload_data_relay_t relay{};
        if (!receiveRelay(box, &message, &relay) || relay.payload_addr != 7
            || relay.reserve != 0 || relay.data_len < 1 || relay.data_len > 128) return {};
        frame.append(reinterpret_cast<const char *>(relay.data), relay.data_len);
        if (frame.size() < 4) continue;
        const int length = static_cast<quint8>(frame.at(2))
            | (static_cast<quint8>(frame.at(3)) << 8);
        if (frame.size() < length + 7) continue;
        QJsonObject object;
        return RoukeProtocol::decodeFrame(frame, &object) ? object : QJsonObject{};
    }
    return {};
}

QJsonObject parameterReport(const QJsonObject &request, int value)
{
    const QString command = request.value("cmd").toString();
    auto response = ack(command);
    response["data"] = QJsonObject{{command == QStringLiteral("lr1_image_size") ? "size" : "value", value}};
    return response;
}

void completeParameterInitialization(QUdpSocket &box, RoukePayloadController &controller)
{
    QSignalSpy finished(&controller, &RoukePayloadController::commandFinished);
    const QStringList commands{"lr1_iso", "lr1_white_balance", "lr1_focus_value_get", "lr1_image_size"};
    const QList<int> values{2, 3, 100, 1};
    controller.initializeCameraParameters();
    for (int i = 0; i < commands.size(); ++i) {
        QTRY_VERIFY(box.hasPendingDatagrams());
        const auto request = receiveStreamRequest(box);
        QCOMPARE(request.value("cmd").toString(), commands.at(i));
        sendStream(box, controller, parameterReport(request, values.at(i)));
        QTRY_COMPARE(finished.count(), i + 1);
    }
    QTRY_VERIFY(!controller.pending());
    QCOMPARE(controller.cameraParameters().size(), 4);
}
}

class RoukePayloadControllerTest : public QObject
{
    Q_OBJECT
private slots:
    void rawSampleCapKeepsExactMatchCountersAndNativeRepresentation()
    {
        mavlink_message_t heartbeat{}, tail{}, unrelated{};
        QVERIFY(decodeMavlinkWire(untrimmedRelayWire(fragmentBytes(vendorRelayData(true), 0, 42, 77, 0, -1, 0xa1)), &heartbeat));
        QVERIFY(decodeMavlinkWire(fragmentBytes(vendorRelayData(false), 0, 42, 77, 0, -1, 0xc1), &tail));
        auto altered = vendorRelayData(true); altered[20] = char(altered.at(20) ^ 1);
        QVERIFY(decodeMavlinkWire(fragmentBytes(altered, 0, 42, 77, 0, -1, 0xa1), &unrelated));
        RoukeRawMavlinkTrace trace;
        const auto first = trace.observe(heartbeat, 0, false);
        QVERIFY(first.log);
        QCOMPARE(first.match, QString("a1-heartbeat"));
        const auto native = trace.sampleLines(first, heartbeat, "native", 1, " channel=0 linkType=1");
        QVERIFY(native.join('\n').contains("representation=decoded"));
        QVERIFY(native.filter("ROUKE_WIRE_FRAME").isEmpty());
        QCOMPARE(loggedHex(native, "ROUKE_WIRE_PAYLOAD"), QByteArray(_MAV_PAYLOAD(&heartbeat), heartbeat.len));
        QCOMPARE(loggedHex(native, "ROUKE_WIRE_DATA"), vendorRelayData(true));
        QVERIFY(!trace.observe(heartbeat, 10).log); // Same bucket suppressed, still counted.
        QVERIFY(trace.observe(tail, 10).log);      // Other reserve bucket remains visible.
        trace.reset();
        for (quint64 i = 0; i < RoukeRawMavlinkTrace::SampleLimit + 40; ++i) {
            const auto sample = trace.observe(i % 2 ? tail : heartbeat, qint64(i * 1000), i % 2 == 0);
            QCOMPARE(sample.log, i < RoukeRawMavlinkTrace::SampleLimit);
            QCOMPARE(sample.match, i % 2 ? QString("c1-ack-tail") : QString("a1-heartbeat"));
        }
        QCOMPARE(trace.counters().valid53022, quint64(280));
        QCOMPARE(trace.counters().a1Heartbeat, quint64(140));
        QCOMPARE(trace.counters().c1AckTail, quint64(140));
        QCOMPARE(trace.counters().peerRejected, quint64(140));
        QCOMPARE(trace.counters().emitted, quint64(240));
        QCOMPARE(trace.counters().suppressed, quint64(40));
        const auto changed = trace.observe(unrelated, 300000);
        QVERIFY(!changed.log);
        QCOMPARE(changed.match, QString("none"));
        QCOMPARE(trace.counters().reserveA1, quint64(141));
        QCOMPARE(trace.counters().a1Heartbeat, quint64(140));
        QVERIFY(trace.summary("payload", 3).contains("valid53022=281"));
        QVERIFY(trace.summary("payload", 3).contains("emitted=240 suppressed=41 cap=240"));
        trace.reset();
        QCOMPARE(trace.counters().valid53022, quint64(0));
        QVERIFY(trace.observe(heartbeat, 0).log);
    }

    void vendorReserveSamplesKeepHeartbeatAndAckSemantics_data()
    {
        QTest::addColumn<bool>("heartbeat");
        QTest::newRow("a1-heartbeat") << true;
        QTest::newRow("c1-ack-tail") << false;
    }

    void vendorReserveSamplesKeepHeartbeatAndAckSemantics()
    {
        QFETCH(bool, heartbeat);
        QUdpSocket box;
        QVERIFY(box.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        PayloadRawTraceFilter trace;
        PayloadLogCapture logs;
        RoukePayloadController controller;
        QSignalSpy finished(&controller, &RoukePayloadController::commandFinished);
        controller.setConfiguration(true, "127.0.0.1", box.localPort(), 0, 0);
        QVERIFY(controller.requestCameraParameter("iso"));
        QTRY_VERIFY(box.hasPendingDatagrams());
        while (box.hasPendingDatagrams()) box.receiveDatagram();
        logs.messages.clear();
        const QByteArray data = vendorRelayData(heartbeat);
        QCOMPARE(data.size(), heartbeat ? 118 : 48);
        const QByteArray wire = fragmentBytes(data, 0, 42, 77, 0, -1, heartbeat ? 0xa1 : 0xc1);
        mavlink_message_t decoded{};
        QVERIFY(decodeMavlinkWire(wire, &decoded));
        QCOMPARE(decoded.msgid, uint32_t(53022));
        QCOMPARE(MAVLINK_MSG_ID_VK_PAYLOAD_DATA_RELAY_CRC, 37);
        QCOMPARE(box.writeDatagram(wire, QHostAddress::LocalHost, controller.boundPort()), qint64(wire.size()));
        QTRY_COMPARE(logs.messages.filter("ROUKE_WIRE_RX").size(), 1);
        QString output = logs.messages.join('\n');
        QVERIFY(output.contains(heartbeat ? "sampleMatch=a1-heartbeat" : "sampleMatch=c1-ack-tail"));
        QVERIFY(output.contains(heartbeat ? "reserve=a1" : "reserve=c1"));
        QVERIFY(output.contains(QString("dataLen=%1").arg(data.size())));
        QVERIFY(output.contains("decision=outer-pass"));
        QCOMPARE(loggedHex(logs.messages, "ROUKE_WIRE_DATA"), data);
        QCOMPARE(loggedHex(logs.messages, "ROUKE_WIRE_FRAME"), wire);
        QTRY_VERIFY_WITH_TIMEOUT(logs.messages.join('\n').contains("relay=1 invalidRelay=0"), 1500);
        QVERIFY(controller.pending());
        QCOMPARE(controller.responding(), heartbeat);
        QCOMPARE(finished.count(), 0);
        QVERIFY(controller.cameraParameters().isEmpty());
        QCOMPARE(controller.photoCount(), 0);
        for (const auto &line : logs.messages.filter("ROUKE_WIRE_")) QVERIFY2(line.toUtf8().size() < 600, qPrintable(line));
    }

    void differentNonzeroReserveFragmentsReassembleExactVendorAckTail()
    {
        QUdpSocket box;
        QVERIFY(box.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        PayloadRawTraceFilter trace;
        PayloadLogCapture logs;
        RoukePayloadController controller;
        QSignalSpy finished(&controller, &RoukePayloadController::commandFinished);
        controller.setConfiguration(true, "127.0.0.1", box.localPort(), 0, 0);
        QVERIFY(controller.requestCameraParameter("iso"));
        QTRY_VERIFY(box.hasPendingDatagrams());
        while (box.hasPendingDatagrams()) box.receiveDatagram();

        auto response = ack("lr1_iso");
        response["data"] = QJsonObject{{"value", 12}};
        const QByteArray frame = rawFrame(response);
        const QByteArray tail = vendorRelayData(false);
        QCOMPARE(frame.right(tail.size()), tail); // Exactly the merchant's 48 DATA bytes.
        const QByteArray prefix = frame.left(frame.size() - tail.size());
        QVERIFY(!prefix.isEmpty() && prefix.size() <= 128);
        const auto firstWire = fragmentBytes(prefix, 0, 42, 77, 0, -1, 0xa1);
        const auto lastWire = fragmentBytes(tail, 0, 42, 77, 1, -1, 0xc1);
        QCOMPARE(box.writeDatagram(firstWire, QHostAddress::LocalHost, controller.boundPort()), qint64(firstWire.size()));
        QTRY_COMPARE(logs.messages.filter("ROUKE_WIRE_RX").size(), 1);
        QVERIFY(controller.pending());
        QVERIFY(!controller.responding());
        QVERIFY(controller.cameraParameters().isEmpty());
        QCOMPARE(finished.count(), 0);

        QCOMPARE(box.writeDatagram(lastWire, QHostAddress::LocalHost, controller.boundPort()), qint64(lastWire.size()));
        QTRY_COMPARE(finished.count(), 1);
        QVERIFY(!controller.pending());
        QVERIFY(controller.responding());
        QCOMPARE(controller.cameraParameters().value("iso").toInt(), 12);
        QVERIFY(logs.messages.join('\n').contains("sampleMatch=c1-ack-tail"));
        QTRY_VERIFY_WITH_TIMEOUT(logs.messages.join('\n').contains("relay=2 invalidRelay=0"), 1500);
        QCOMPARE(controller.photoCount(), 0);
    }

    void invalidRelayLengthsStillRejected_data()
    {
        QTest::addColumn<int>("dataLength");
        QTest::newRow("zero") << 0;
        QTest::newRow("over-capacity") << 129;
    }

    void invalidRelayLengthsStillRejected()
    {
        QFETCH(int, dataLength);
        QUdpSocket box;
        QVERIFY(box.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        PayloadRawTraceFilter trace;
        PayloadLogCapture logs;
        RoukePayloadController controller;
        controller.setConfiguration(true, "127.0.0.1", box.localPort(), 0, 0);
        QVERIFY(controller.requestCameraParameter("iso"));
        QTRY_VERIFY(box.hasPendingDatagrams());
        while (box.hasPendingDatagrams()) box.receiveDatagram();
        const auto wire = fragmentBytes(vendorRelayData(true), 0, 42, 77, 0, dataLength, 0xa1);
        QCOMPARE(box.writeDatagram(wire, QHostAddress::LocalHost, controller.boundPort()), qint64(wire.size()));
        QTRY_VERIFY_WITH_TIMEOUT(logs.messages.join('\n').contains("relay=1 invalidRelay=1"), 1500);
        QVERIFY(logs.messages.join('\n').contains("decision=data-length"));
        QVERIFY(controller.pending());
        QVERIFY(!controller.responding());
        QVERIFY(controller.cameraParameters().isEmpty());
    }

    void rawIngressObservesForeignPeerBeforeAddressAndPortFilter_data()
    {
        QTest::addColumn<QString>("sourceAddress");
        QTest::addColumn<QString>("rejectionCounter");
        QTest::newRow("wrong-port") << QString("127.0.0.1") << QString("rejectPort=1");
        QTest::newRow("wrong-address") << QString("127.0.0.2") << QString("rejectAddress=1");
    }

    void rawIngressObservesForeignPeerBeforeAddressAndPortFilter()
    {
        QFETCH(QString, sourceAddress);
        QFETCH(QString, rejectionCounter);
        QUdpSocket box, foreign;
        QVERIFY(box.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        QVERIFY(foreign.bind(QHostAddress(sourceAddress), quint16(0)));
        PayloadRawTraceFilter trace;
        PayloadLogCapture logs;
        RoukePayloadController controller;
        controller.setConfiguration(true, "127.0.0.1", box.localPort(), 0, 0);
        logs.messages.clear();
        const auto wire = fragmentBytes(vendorRelayData(true), 0, 42, 77, 0, -1, 0xa1);
        QCOMPARE(foreign.writeDatagram(wire, QHostAddress::LocalHost, controller.boundPort()), qint64(wire.size()));
        QTRY_COMPARE(logs.messages.filter("ROUKE_WIRE_RX").size(), 1);
        const QString output = logs.messages.join('\n');
        QVERIFY(output.contains("sampleMatch=a1-heartbeat"));
        QVERIFY(output.contains(QString("%1:%2").arg(sourceAddress).arg(foreign.localPort())));
        QCOMPARE(loggedHex(logs.messages, "ROUKE_WIRE_FRAME"), wire);
        QTRY_VERIFY_WITH_TIMEOUT(logs.messages.join('\n').contains(rejectionCounter), 1500);
        QVERIFY(logs.messages.join('\n').contains("relay=0 invalidRelay=0"));
        QVERIFY(logs.messages.join('\n').contains("peerRejected=1"));
        QVERIFY(!controller.responding());
        QVERIFY(controller.cameraParameters().isEmpty());
    }

    void rawIngressPreservesWireTailZeros()
    {
        QUdpSocket box;
        QVERIFY(box.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        PayloadRawTraceFilter trace;
        PayloadLogCapture logs;
        RoukePayloadController controller;
        controller.setConfiguration(true, "127.0.0.1", box.localPort(), 0, 0);
        logs.messages.clear();
        const auto wire = untrimmedRelayWire(fragmentBytes(vendorRelayData(true), 0, 42, 77, 0, -1, 0xa1));
        QCOMPARE(wire.size(), 144);
        mavlink_message_t decoded{};
        QVERIFY(decodeMavlinkWire(wire, &decoded));
        QCOMPARE(decoded.len, uint8_t(132));
        QCOMPARE(box.writeDatagram(wire, QHostAddress::LocalHost, controller.boundPort()), qint64(wire.size()));
        QTRY_COMPARE(logs.messages.filter("ROUKE_WIRE_RX").size(), 1);
        QCOMPARE(loggedHex(logs.messages, "ROUKE_WIRE_FRAME"), wire);
        QVERIFY(logs.messages.join('\n').contains("representation=udp-bytes"));
        QCOMPARE(loggedHex(logs.messages, "ROUKE_WIRE_DATA"), vendorRelayData(true));
        QTRY_VERIFY(controller.responding());
    }

    void initializationStagesCoalesceAndConfirmedValuesAreSkipped()
    {
        QUdpSocket box;
        QVERIFY(box.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        RoukePayloadController controller;
        controller.setConfiguration(true, "127.0.0.1", box.localPort(), 0, 7);
        controller.initializeCameraParameters();
        controller.notifyVideoFrameReceived();
        controller.initializeCameraParameters();
        controller.notifyVideoFrameReceived();
        controller.refreshCameraParameters();
        controller.cancelCameraParameterRefresh(); // Cannot cancel initialization.
        completeParameterInitialization(box, controller);
        controller.initializeCameraParameters();
        controller.notifyVideoFrameReceived();
        sendStream(box, controller, sdCardAck());
        QTest::qWait(80);
        QVERIFY(!box.hasPendingDatagrams());
        QVERIFY(!controller.pending());
        QCOMPARE(controller.cameraParameters().value("iso").toInt(), 2);
    }

    void initializationFailureWaitsForRemainingOneShotStages()
    {
        QUdpSocket box;
        QVERIFY(box.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        RoukePayloadController controller;
        controller.setConfiguration(true, "127.0.0.1", box.localPort(), 0, 7);
        QSignalSpy finished(&controller, &RoukePayloadController::commandFinished);
        controller.initializeCameraParameters();
        // A disconnected startup exhausts exactly four attempts. No timer
        // periodically retries the parameter batch after those deadlines.
        const QStringList commands{"lr1_iso", "lr1_white_balance", "lr1_focus_value_get", "lr1_image_size"};
        for (int i = 0; i < 4; ++i) {
            QTRY_VERIFY(box.hasPendingDatagrams());
            QCOMPARE(receiveStreamRequest(box).value("cmd").toString(), commands.at(i));
            QTRY_COMPARE_WITH_TIMEOUT(finished.count(), i + 1, 4200);
        }
        QTest::qWait(80);
        QVERIFY(!box.hasPendingDatagrams());
        controller.initializeCameraParameters();
        QTest::qWait(80);
        QVERIFY(!box.hasPendingDatagrams());
        // The first valid communication report grants one later batch only.
        sendStream(box, controller, sdCardAck());
        for (int i = 0; i < 4; ++i) {
            QTRY_VERIFY(box.hasPendingDatagrams());
            const auto request = receiveStreamRequest(box);
            QCOMPARE(request.value("cmd").toString(), commands.at(i));
            send(box, controller, ack(commands.at(i), 1));
            QTRY_COMPARE(finished.count(), i + 5);
        }
        sendStream(box, controller, sdCardAck());
        QTest::qWait(80);
        QVERIFY(!box.hasPendingDatagrams());
        controller.notifyVideoFrameReceived();
        completeParameterInitialization(box, controller);
        controller.notifyVideoFrameReceived();
        QTest::qWait(80);
        QVERIFY(!box.hasPendingDatagrams());
    }

    void videoStageFillsOnlyUnknownParametersAndManualRefreshIsForced()
    {
        QUdpSocket box;
        QVERIFY(box.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        RoukePayloadController controller;
        controller.setConfiguration(true, "127.0.0.1", box.localPort(), 0, 7);
        QSignalSpy finished(&controller, &RoukePayloadController::commandFinished);
        controller.initializeCameraParameters();
        for (int i = 0; i < 4; ++i) {
            QTRY_VERIFY(box.hasPendingDatagrams());
            const auto request = receiveStreamRequest(box);
            sendStream(box, controller, i == 0 ? parameterReport(request, 2) : ack(request.value("cmd").toString(), 1));
            QTRY_COMPARE(finished.count(), i + 1);
        }
        controller.notifyVideoFrameReceived();
        for (int i = 0; i < 3; ++i) {
            QTRY_VERIFY(box.hasPendingDatagrams());
            const auto request = receiveStreamRequest(box);
            QVERIFY(request.value("cmd").toString() != "lr1_iso");
            sendStream(box, controller, parameterReport(request, 1));
            QTRY_COMPARE(finished.count(), i + 5);
        }
        controller.refreshCameraParameters();
        QTRY_VERIFY(box.hasPendingDatagrams());
        const auto request = receiveStreamRequest(box);
        QCOMPARE(request.value("cmd").toString(), QString("lr1_iso"));
        controller.cancelCameraParameterRefresh();
        sendStream(box, controller, parameterReport(request, 4));
        QTRY_VERIFY(!controller.pending());
        QTest::qWait(80);
        QVERIFY(!box.hasPendingDatagrams());
        QCOMPARE(controller.cameraParameters().value("iso").toInt(), 4);
    }

    void endpointSessionRetiresQueuedInitializationAndLateResponses()
    {
        QUdpSocket first, second;
        QVERIFY(first.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        QVERIFY(second.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        RoukePayloadController controller;
        controller.setConfiguration(true, "127.0.0.1", first.localPort(), 0, 7);
        controller.initializeCameraParameters();
        QTRY_VERIFY(first.hasPendingDatagrams());
        const auto stale = receiveStreamRequest(first);
        controller.setConfiguration(true, "127.0.0.1", second.localPort(), 0, 7);
        controller.initializeCameraParameters();
        sendStream(first, controller, parameterReport(stale, 12));
        completeParameterInitialization(second, controller);
        QCOMPARE(controller.cameraParameters().value("iso").toInt(), 2);
        QVERIFY(!first.hasPendingDatagrams());
        controller.setConfiguration(false, "127.0.0.1", second.localPort(), 0, 7);
        QVERIFY(controller.cameraParameters().isEmpty());
        QVERIFY(controller.cameraParameterVerification().isEmpty());
        controller.setConfiguration(true, "127.0.0.1", second.localPort(), 0, 7);
        completeParameterInitialization(second, controller);
    }

    void readableWriteRequiresOneForcedVerification_data()
    {
        QTest::addColumn<QString>("key");
        QTest::addColumn<int>("target");
        QTest::addColumn<int>("actual");
        QTest::addColumn<QString>("outcome");
        QTest::newRow("iso-equal-number-is-only-report") << QString("iso") << 8 << 8 << QString("reported");
        QTest::newRow("iso-physical-value") << QString("iso") << 8 << 1600 << QString("reported");
        QTest::newRow("wb-match") << QString("whiteBalance") << 4 << 4 << QString("verified");
        QTest::newRow("focus-match") << QString("focusValue") << 200 << 200 << QString("verified");
        QTest::newRow("size-match") << QString("imageSize") << 2 << 2 << QString("verified");
        QTest::newRow("different-report") << QString("whiteBalance") << 8 << 5 << QString("mismatch");
        QTest::newRow("invalid-report") << QString("whiteBalance") << 8 << 80 << QString("unverified");
        QTest::newRow("missing-report") << QString("iso") << 8 << -1 << QString("unverified");
    }

    void readableWriteRequiresOneForcedVerification()
    {
        QFETCH(QString, key);
        QFETCH(int, target);
        QFETCH(int, actual);
        QFETCH(QString, outcome);
        QUdpSocket box;
        QVERIFY(box.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        RoukePayloadController controller;
        controller.setConfiguration(true, "127.0.0.1", box.localPort(), 0, 7);
        completeParameterInitialization(box, controller);
        const QVariant old = controller.cameraParameters().value(key);
        QVERIFY(controller.setCameraParameter(key, target));
        QCOMPARE(controller.cameraParameterVerification().value(key).toString(), QString("applying"));
        const auto write = receiveStreamRequest(box);
        QSignalSpy finished(&controller, &RoukePayloadController::commandFinished);
        send(box, controller, ack(write.value("cmd").toString()));
        QTRY_COMPARE(finished.count(), 1);
        QCOMPARE(controller.cameraParameters().value(key), old);
        QCOMPARE(controller.cameraParameterVerification().value(key).toString(), QString("verifying"));
        controller.cancelCameraParameterRefresh(); // Cannot cancel verification.
        QTRY_VERIFY(box.hasPendingDatagrams());
        const auto read = receiveStreamRequest(box);
        if (key == "focusValue") QCOMPARE(read.value("cmd").toString(), QString("lr1_focus_value_get"));
        else QCOMPARE(read.value("data").toObject().value(key == "imageSize" ? "operate" : "action").toInt(-1), 0);
        sendStream(box, controller, actual < 0 ? ack(read.value("cmd").toString()) : parameterReport(read, actual));
        QTRY_COMPARE(finished.count(), 2);
        QCOMPARE(controller.cameraParameterVerification().value(key).toString(), outcome);
        QCOMPARE(controller.cameraParameters().value(key), outcome == "unverified" ? old : QVariant(actual));
        QCOMPARE(finished.last().at(1).toBool(), outcome == "verified" || outcome == "reported");
        QTest::qWait(80);
        QVERIFY(!box.hasPendingDatagrams());
    }

    void verificationTimeoutRetainsCacheAndManualReadCanFinishVerification()
    {
        QUdpSocket box;
        QVERIFY(box.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        RoukePayloadController controller;
        controller.setConfiguration(true, "127.0.0.1", box.localPort(), 0, 7);
        completeParameterInitialization(box, controller);
        QVERIFY(controller.setCameraParameter("iso", 8));
        receiveStreamRequest(box);
        send(box, controller, ack("lr1_iso"));
        QTRY_VERIFY(box.hasPendingDatagrams());
        const auto read = receiveStreamRequest(box);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.pending(), 4200);
        QCOMPARE(controller.cameraParameters().value("iso").toInt(), 2);
        QCOMPARE(controller.cameraParameterVerification().value("iso").toString(), QString("unverified"));
        QTest::qWait(80);
        QVERIFY(!box.hasPendingDatagrams());
        QVERIFY(controller.requestCameraParameter("iso"));
        receiveStreamRequest(box);
        sendStream(box, controller, parameterReport(read, 1600));
        QTRY_VERIFY(!controller.pending());
        QCOMPARE(controller.cameraParameters().value("iso").toInt(), 1600);
        QCOMPARE(controller.cameraParameterVerification().value("iso").toString(), QString("reported"));
    }

    void writeTimeoutNeverRetriesOrReadsBackAndKeepsWriteUncertainty()
    {
        QUdpSocket box;
        QVERIFY(box.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        RoukePayloadController controller;
        controller.setConfiguration(true, "127.0.0.1", box.localPort(), 0, 7);
        completeParameterInitialization(box, controller);
        QVERIFY(controller.setCameraParameter("iso", 8));
        receiveStreamRequest(box);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.pending(), 4200);
        QVERIFY(!controller.cameraParameters().contains("iso"));
        QCOMPARE(controller.cameraParameterVerification().value("iso").toString(), QString("unverified"));
        QTest::qWait(80);
        QVERIFY(!box.hasPendingDatagrams());
        QVERIFY(controller.requestCameraParameter("iso"));
        const auto read = receiveStreamRequest(box);
        sendStream(box, controller, parameterReport(read, 1600));
        QTRY_VERIFY(!controller.pending());
        QCOMPARE(controller.cameraParameterVerification().value("iso").toString(), QString("reported"));
        QCOMPARE(controller.cameraParameters().value("iso").toInt(), 1600);
        QVERIFY(controller.setCameraParameter("iso", 9));
        receiveStreamRequest(box);
        send(box, controller, ack("lr1_iso"));
        QTRY_VERIFY(!controller.pending());
        QVERIFY(!controller.cameraParameters().contains("iso"));
        QCOMPARE(controller.cameraParameterVerification().value("iso").toString(), QString("unverified"));
        QTest::qWait(80);
        QVERIFY(!box.hasPendingDatagrams());
    }

    void rejectedWriteRetainsTargetForManualVerification()
    {
        QUdpSocket box;
        QVERIFY(box.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        RoukePayloadController controller;
        controller.setConfiguration(true, "127.0.0.1", box.localPort(), 0, 7);
        completeParameterInitialization(box, controller);
        QVERIFY(controller.setCameraParameter("whiteBalance", 8));
        receiveStreamRequest(box);
        send(box, controller, ack("lr1_white_balance", 1));
        QTRY_VERIFY(!controller.pending());
        QCOMPARE(controller.cameraParameters().value("whiteBalance").toInt(), 3);
        QCOMPARE(controller.cameraParameterVerification().value("whiteBalance").toString(), QString("unverified"));
        QTest::qWait(80);
        QVERIFY(!box.hasPendingDatagrams());
        QVERIFY(controller.requestCameraParameter("whiteBalance"));
        const auto read = receiveStreamRequest(box);
        sendStream(box, controller, parameterReport(read, 2));
        QTRY_VERIFY(!controller.pending());
        QCOMPARE(controller.cameraParameterVerification().value("whiteBalance").toString(), QString("mismatch"));
    }

    void manualRefreshRechecksTheLastSubmittedTarget()
    {
        QUdpSocket box;
        QVERIFY(box.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        RoukePayloadController controller;
        controller.setConfiguration(true, "127.0.0.1", box.localPort(), 0, 7);
        completeParameterInitialization(box, controller);
        QVERIFY(controller.setCameraParameter("whiteBalance", 8));
        receiveStreamRequest(box);
        send(box, controller, ack("lr1_white_balance"));
        QTRY_VERIFY(box.hasPendingDatagrams());
        const auto read = receiveStreamRequest(box);
        sendStream(box, controller, parameterReport(read, 8));
        QTRY_VERIFY(!controller.pending());
        QCOMPARE(controller.cameraParameterVerification().value("whiteBalance").toString(), QString("verified"));
        QVERIFY(controller.requestCameraParameter("whiteBalance"));
        receiveStreamRequest(box);
        sendStream(box, controller, parameterReport(read, 5));
        QTRY_VERIFY(!controller.pending());
        QCOMPARE(controller.cameraParameters().value("whiteBalance").toInt(), 5);
        QCOMPARE(controller.cameraParameterVerification().value("whiteBalance").toString(), QString("mismatch"));
        QVERIFY(controller.requestCameraParameter("whiteBalance"));
        receiveStreamRequest(box);
        send(box, controller, ack("lr1_white_balance", 1));
        QTRY_VERIFY(!controller.pending());
        QCOMPARE(controller.cameraParameters().value("whiteBalance").toInt(), 5);
        QCOMPARE(controller.cameraParameterVerification().value("whiteBalance").toString(), QString("unverified"));
        QVERIFY(controller.requestCameraParameter("whiteBalance"));
        receiveStreamRequest(box);
        sendStream(box, controller, parameterReport(read, 8));
        QTRY_VERIFY(!controller.pending());
        QCOMPARE(controller.cameraParameterVerification().value("whiteBalance").toString(), QString("verified"));
    }

    void recordingStopPrecedesQueuedVerificationAndReentrantEndpointReset()
    {
        QUdpSocket box, replacement;
        QVERIFY(box.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        QVERIFY(replacement.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        RoukePayloadController controller;
        controller.setConfiguration(true, "127.0.0.1", box.localPort(), 0, 7);
        completeParameterInitialization(box, controller);
        QVERIFY(controller.setCameraParameter("iso", 8));
        receiveStreamRequest(box);
        QVERIFY(controller.setRecording(false));
        send(box, controller, ack("lr1_iso"));
        QTRY_VERIFY(box.hasPendingDatagrams());
        const auto stop = receiveStreamRequest(box);
        QCOMPARE(stop.value("cmd").toString(), QString("lr1_record"));
        QCOMPARE(stop.value("data").toObject().value("action").toInt(), 1);
        QCOMPARE(controller.cameraParameterVerification().value("iso").toString(), QString("verifying"));
        bool reset = false;
        connect(&controller, &RoukePayloadController::commandFinished, &controller,
                [&](const QString &command, bool success) {
                    if (!reset && command == "lr1_record" && success) {
                        reset = true;
                        controller.setConfiguration(true, "127.0.0.1", replacement.localPort(), 0, 7);
                        controller.initializeCameraParameters();
                    }
                });
        send(box, controller, ack("lr1_record"));
        QTRY_VERIFY(reset);
        QVERIFY(controller.cameraParameters().isEmpty());
        QVERIFY(controller.cameraParameterVerification().isEmpty());
        completeParameterInitialization(replacement, controller);
        QVERIFY(!box.hasPendingDatagrams());
    }

    void onlyDocumentedReadbacksFollowSuccessfulActions()
    {
        QUdpSocket box;
        QVERIFY(box.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        RoukePayloadController controller;
        controller.setConfiguration(true, "127.0.0.1", box.localPort(), 0, 7);
        completeParameterInitialization(box, controller);
        QVERIFY(controller.setCameraParameter("triggerPhoto", 0));
        receiveStreamRequest(box);
        send(box, controller, ack("lr1_trigger_photo"));
        QTRY_VERIFY(!controller.pending());
        QTest::qWait(80);
        QVERIFY(!box.hasPendingDatagrams());
        for (const QString &command : {QString("lr1_focus"), QString("lr1_focus_mode")}) {
            QVERIFY(command == "lr1_focus" ? controller.autoFocus() : controller.setCameraParameter("focusMode", 0));
            receiveStreamRequest(box);
            send(box, controller, ack(command));
            QTRY_VERIFY(box.hasPendingDatagrams());
            const auto read = receiveStreamRequest(box);
            QCOMPARE(read.value("cmd").toString(), QString("lr1_focus_value_get"));
            QVERIFY(!controller.cameraParameters().contains("focusValue"));
            QCOMPARE(controller.cameraParameterVerification().value("focusValue").toString(), QString("verifying"));
            sendStream(box, controller, parameterReport(read, 123));
            QTRY_VERIFY(!controller.pending());
            QCOMPARE(controller.cameraParameters().value("focusValue").toInt(), 123);
        }
    }

    void nativeTraceSamplesChannelsIndependentlyAndResetsDestroyedLinks()
    {
        RoukeNativeMavlinkTrace trace;
        QObject second;
        auto *first = new QObject;
        QVERIFY(!trace.observe(nullptr, 0, 0).log);
        QVERIFY(!trace.observe(first, -1, 0).log);
        QVERIFY(!trace.observe(first, RoukeNativeMavlinkTrace::ChannelCount, 0).log);
        auto sample = trace.observe(first, 0, 0);
        QVERIFY(sample.log);
        QCOMPARE(sample.count, quint64(1));
        for (int i = 1; i < 20; ++i) QVERIFY(!trace.observe(first, 0, i * 50).log);
        sample = trace.observe(&second, 1, 999);
        QVERIFY(sample.log); // Another channel is never hidden by channel 0.
        QCOMPARE(sample.count, quint64(1));
        sample = trace.observe(first, 0, 1000);
        QVERIFY(sample.log);
        QCOMPARE(sample.count, quint64(21));
        delete first;
        first = new QObject;
        sample = trace.observe(first, 0, 1001);
        QVERIFY(sample.log);
        QCOMPARE(sample.count, quint64(1));
        sample = trace.observe(&second, 0, 1002);
        QVERIFY(sample.log); // Reassigning a live channel also resets it.
        QCOMPARE(sample.count, quint64(1));
        trace.reset();
        QVERIFY(trace.observe(&second, 0, 0).log);
        delete first;
    }

    void unrelatedMavlinkHeadersAreBoundedAndNeverConfirmPayloadState()
    {
        QUdpSocket box;
        QVERIFY(box.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        PayloadLogCapture logs;
        RoukePayloadController controller;
        controller.setConfiguration(true, "127.0.0.1", box.localPort(), 0, 7);
        logs.messages.clear();
        QVERIFY(controller.requestCameraParameter("iso"));
        receiveStreamRequest(box);
        const auto sendHeartbeat = [&](int system) {
            mavlink_message_t message{};
            mavlink_status_t status{};
            mavlink_heartbeat_t heartbeat{};
            if (system == 2) status.flags = MAVLINK_STATUS_FLAG_OUT_MAVLINK1;
            mavlink_msg_heartbeat_encode_status(static_cast<uint8_t>(system), 1, &status, &message, &heartbeat);
            uint8_t bytes[MAVLINK_MAX_PACKET_LEN]{};
            const auto count = mavlink_msg_to_send_buffer(bytes, &message);
            QCOMPARE(box.writeDatagram(reinterpret_cast<const char *>(bytes), count,
                                      QHostAddress::LocalHost, controller.boundPort()), qint64(count));
        };
        // Seventeen different header tuples exceed the sixteen-entry budget;
        // an already tracked tuple must still accumulate after the limit.
        for (int system = 1; system <= 17; ++system) sendHeartbeat(system);
        sendHeartbeat(1);
        sendHeartbeat(1);
        QTest::qWait(30);
        QVERIFY(!controller.responding());
        QVERIFY(controller.pending());
        QVERIFY(controller.cameraParameters().isEmpty());
        QVERIFY(logs.messages.isEmpty()); // No logging at telemetry frequency.
        QTRY_VERIFY_WITH_TIMEOUT(!controller.pending(), 4500);
        QCOMPARE(logs.messages.size(), 19); // Three stages and sixteen bounded header records.
        const QString summary = logs.messages.join('\n');
        QVERIFY(summary.contains("peer=19"));
        QVERIFY(summary.contains("other=19 relay=0"));
        QVERIFY(summary.contains("badCrc=0"));
        QVERIFY(summary.contains("id=0 magic=253 sys=1 comp=1 count=3"));
        QVERIFY(summary.contains("id=0 magic=254 sys=2 comp=1 count=1"));
        QVERIFY(!summary.contains("sys=17 "));
        QVERIFY(summary.contains("otherOverflow=1"));
        QVERIFY(summary.contains("valid=0"));
        for (const QString &line : logs.messages) {
            QVERIFY(line.toUtf8().size() < 600); // Leave room for Android's timestamp/category context.
            QVERIFY(line.contains("session=1 report=1"));
        }
        QVERIFY(!controller.responding());
        // Header accounting never loosens the relay filter or blocks a later
        // genuine 53022 value report from the same peer.
        QVERIFY(controller.requestCameraParameter("iso"));
        receiveStreamRequest(box);
        auto response = ack("lr1_iso");
        response["data"] = QJsonObject{{"value", 5}};
        sendStream(box, controller, response);
        QTRY_VERIFY(!controller.pending());
        QVERIFY(controller.responding());
        QCOMPARE(controller.cameraParameters().value("iso").toInt(), 5);
    }

    void explicitTraceReportsSafeValuesAndThrottlesSpeed()
    {
        QUdpSocket box;
        QVERIFY(box.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        PayloadLogCapture logs;
        RoukePayloadController controller;
        controller.setConfiguration(true, "127.0.0.1", box.localPort(), 0, 7);
        logs.messages.clear();
        QVERIFY(controller.requestCameraParameter("iso"));
        receiveStreamRequest(box);
        auto response = ack("lr1_iso");
        response["data"] = QJsonObject{{"value", 12}, {"private_note", "do-not-log-this-data"}};
        response["msg"] = "do-not-log-this-message";
        sendStream(box, controller, response);
        QTRY_VERIFY(!controller.pending());
        QVERIFY(logs.messages.isEmpty()); // The new trace category defaults off.

        PayloadTraceFilter trace;
        QVERIFY(controller.requestCameraParameter("iso"));
        receiveStreamRequest(box);
        const QByteArray frame = rawFrame(response);
        for (int offset = 0; offset < frame.size(); offset += 128) {
            sendFragment(box, controller, frame.mid(offset, 128), 7, 42, 77, 51 + offset / 128);
        }
        QTRY_VERIFY(!controller.pending());
        QCOMPARE(controller.cameraParameters().value("iso").toInt(), 12);
        QString output = logs.messages.join('\n');
        QVERIFY(output.contains("ROUKE_TX cmd \"lr1_iso\" operation read"));
        QVERIFY(output.contains("action=0"));
        QVERIFY(output.contains("ROUKE_TX_FRAGMENT"));
        QVERIFY(output.contains("ROUKE_RX cmd \"lr1_iso\" ack true accepted true"));
        QVERIFY(output.contains("value=12"));
        QVERIFY(output.contains("seq=51,data_len=128"));
        QVERIFY(output.contains("seq=52,data_len="));
        QVERIFY(output.contains("ROUKE_RX_RESULT cmd \"lr1_iso\" success true"));
        QVERIFY(!output.contains("do-not-log"));
        QVERIFY(!output.contains("private_note"));
        QVERIFY(!output.contains("{\""));

        logs.messages.clear();
        QVERIFY(controller.setCameraParameter("iso", 8));
        receiveStreamRequest(box);
        send(box, controller, ack("lr1_iso"));
        QTRY_VERIFY(!controller.pending());
        output = logs.messages.join('\n');
        QVERIFY(output.contains("operation write"));
        QVERIFY(output.contains("value=8"));
        QVERIFY(output.contains("action=1"));
        QCOMPARE(controller.cameraParameters().value("iso").toInt(), 8);

        logs.messages.clear();
        QVERIFY(controller.requestSdCardStatus());
        receiveStreamRequest(box);
        sendStream(box, controller, sdCardAck());
        QTRY_VERIFY(!controller.sdCardQueryPending());
        output = logs.messages.join('\n');
        QVERIFY(output.contains("lane storage serial 2"));
        QVERIFY(output.contains("lane storage frameSerial 2 currentSerial 2 pendingMatch true"));
        QVERIFY(output.contains("accepted true code 0"));
        logs.messages.clear();
        QVERIFY(controller.centerGimbal());
        receiveStreamRequest(box);
        send(box, controller, ack("gimbal_center"));
        QTRY_VERIFY(!controller.gimbalCenterPending());
        output = logs.messages.join('\n');
        QVERIFY(output.contains("lane center serial 2"));
        QVERIFY(output.contains("lane center frameSerial 2 currentSerial 2 pendingMatch true"));

        logs.messages.clear();
        for (int i = 1; i <= 20; ++i) {
            QVERIFY(controller.sendGimbalSpeed(35 * (i % 15 + 1), -35 * (i % 15 + 1)));
            receiveStreamRequest(box);
            send(box, controller, ack("gimbal_speed_ctrl"));
        }
        QTest::qWait(30);
        QCOMPARE(logs.messages.filter("ROUKE_TX cmd").size(), 1);
        QCOMPARE(logs.messages.filter("ROUKE_TX_FRAGMENT").size(), 2);
        QCOMPARE(logs.messages.filter("ROUKE_RX cmd").size(), 1);
        QVERIFY(controller.sendGimbalSpeed(0, 0));
        receiveStreamRequest(box);
        QCOMPARE(logs.messages.filter("ROUKE_TX cmd").size(), 2); // Stop is immediate, not sampled away.
    }

    void diagnosticsSummarizeRejectedPacketsWithoutLoggingPayloads()
    {
        QUdpSocket box, foreign;
        QVERIFY(box.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        QVERIFY(foreign.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        PayloadLogCapture logs;
        RoukePayloadController controller;
        controller.setConfiguration(true, "127.0.0.1", box.localPort(), 0, 7);
        QVERIFY(logs.messages.join('\n').contains(QString("boundLocalPort %1").arg(controller.boundPort())));
        QVERIFY(logs.messages.join('\n').contains("ROUKE_DIAGNOSTICS schema=5 msgid=53022 len=132 crc=37 scope=build-contract"));
        QVERIFY(logs.messages.join('\n').contains("raw53022=1 reservePolicy=ignored"));
        QVERIFY(logs.messages.join('\n').contains(QString("remotePort %1").arg(box.localPort())));
        QVERIFY(logs.messages.join('\n').contains("payloadAddress 7"));
        logs.messages.clear();
        QVERIFY(controller.takePhoto());
        receiveStreamRequest(box);
        send(foreign, controller, ack("lr1_take_photo"));
        QVERIFY(box.writeDatagram("invalid-mavlink", QHostAddress::LocalHost, controller.boundPort()) > 0);
        auto corrupt = messageBytes(ack("lr1_take_photo"));
        corrupt[corrupt.size() - 1] = static_cast<char>(corrupt.at(corrupt.size() - 1) ^ 1);
        QCOMPARE(box.writeDatagram(corrupt, QHostAddress::LocalHost, controller.boundPort()), qint64(corrupt.size()));
        send(box, controller, ack("lr1_take_photo"), 7, 129);
        auto malformed = ack("lr1_take_photo");
        malformed["code"] = 3; // Valid MAVLink, rejected inner envelope.
        malformed["msg"] = "private-payload-note";
        sendStream(box, controller, malformed);
        auto wrongRoute = ack("lr1_take_photo");
        wrongRoute["target"] = "osdk";
        send(box, controller, wrongRoute);
        send(box, controller, ack("lr1_take_photo"), 8);
        // Ordinary speed traffic produces no per-packet or per-command logs.
        for (int i = 0; i < 20; ++i) {
            QVERIFY(controller.sendGimbalSpeed(35 * (i % 15 + 1), -35 * (i % 15 + 1)));
            receiveStreamRequest(box);
            send(box, controller, ack("gimbal_speed_ctrl"));
        }
        QTRY_VERIFY(controller.responding());
        QVERIFY(logs.messages.isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(!controller.pending(), 4500);
        QCOMPARE(logs.messages.size(), 3);
        const QString summary = logs.messages.join('\n');
        QVERIFY(summary.contains("reason=timeout cmd=lr1_take_photo"));
        QVERIFY(summary.contains("rejectPort=1"));
        QVERIFY(summary.contains("badCrc=1"));
        QVERIFY(summary.contains("incomplete=2"));
        QVERIFY(summary.contains("invalidRelay=1"));
        QVERIFY(!summary.contains("resync=0"));
        QVERIFY(summary.contains("rejectRoute=1"));
        QVERIFY(summary.contains("rejectPayload=1"));
        QVERIFY(summary.contains("matched=20"));
        QVERIFY(!summary.contains("private-payload-note"));
        QVERIFY(!summary.contains("{\""));
        QCOMPARE(controller.photoCount(), 0);
    }

    void mixedDatagramAndRejectedInnerFramesStillCountOuterRelay()
    {
        QUdpSocket box;
        QVERIFY(box.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        PayloadTraceFilter trace;
        PayloadLogCapture logs;
        RoukePayloadController controller;
        controller.setConfiguration(true, "127.0.0.1", box.localPort(), 0, 7);
        logs.messages.clear();
        QVERIFY(controller.requestCameraParameter("iso"));
        receiveStreamRequest(box);
        mavlink_message_t heartbeatMessage{};
        mavlink_status_t heartbeatStatus{};
        mavlink_heartbeat_t heartbeat{};
        mavlink_msg_heartbeat_encode_status(1, 1, &heartbeatStatus, &heartbeatMessage, &heartbeat);
        uint8_t wire[MAVLINK_MAX_PACKET_LEN]{};
        const auto count = mavlink_msg_to_send_buffer(wire, &heartbeatMessage);
        QByteArray datagram(reinterpret_cast<const char *>(wire), count);
        auto response = ack("lr1_iso");
        response["data"] = QJsonObject{{"value", 12}};
        datagram += messageBytes(response);
        QCOMPARE(box.writeDatagram(datagram, QHostAddress::LocalHost, controller.boundPort()), qint64(datagram.size()));
        QTRY_VERIFY(!controller.pending());
        QCOMPARE(controller.cameraParameters().value("iso").toInt(), 12);
        QTRY_VERIFY_WITH_TIMEOUT(logs.messages.join('\n').contains("other=1 relay=1 invalidRelay=0"), 1500);
        QString output = logs.messages.join('\n');
        QVERIFY(output.contains(QString("udp=1 bytes=%1 peer=1").arg(datagram.size())));
        QVERIFY(output.contains("valid=1 matched=1"));
        QCOMPARE(logs.messages.filter("ROUKE_RX_RELAY_FIRST").size(), 1);
        QVERIFY(!output.contains("lastRelayMs=-1"));

        logs.messages.clear();
        // Valid MAVLink with invalid inner bytes, then invalid relay length.
        // Neither can acknowledge a camera request, but both reached 53022.
        const QByteArray badInner = fragmentBytes("not-an-inner-frame");
        QCOMPARE(box.writeDatagram(badInner, QHostAddress::LocalHost, controller.boundPort()), qint64(badInner.size()));
        send(box, controller, response, 7, 129);
        QTRY_VERIFY_WITH_TIMEOUT(logs.messages.join('\n').contains("other=1 relay=3 invalidRelay=1"), 1500);
        output = logs.messages.join('\n');
        QVERIFY(output.contains("valid=1 matched=1"));
        QVERIFY(!output.contains("resync=0"));
        QVERIFY(logs.messages.filter("ROUKE_RX_RELAY_FIRST").isEmpty());
        QCOMPARE(controller.cameraParameters().value("iso").toInt(), 12);
        for (const QString &line : logs.messages) QVERIFY(line.toUtf8().size() < 600);
    }

    void receiveSnapshotsAreOptInPeriodicAndResetWithConfiguration()
    {
        QUdpSocket box, nextBox, foreign;
        QVERIFY(box.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        QVERIFY(nextBox.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        QVERIFY(foreign.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        PayloadLogCapture logs;
        RoukePayloadController controller;
        controller.setConfiguration(true, "127.0.0.1", box.localPort(), 0, 7);
        logs.messages.clear();
        QTest::qWait(1100);
        QVERIFY(logs.messages.isEmpty()); // Snapshot timer does not opt in to debug output.
        PayloadTraceFilter trace;
        QTRY_VERIFY_WITH_TIMEOUT(logs.messages.filter("ROUKE_RX_SNAPSHOT").size() == 1, 1500);
        QString output = logs.messages.join('\n');
        QVERIFY(output.contains("reason=periodic"));
        QVERIFY(output.contains("udp=0 bytes=0 peer=0"));
        QVERIFY(output.contains("lastUdpMs=-1 lastPeerMs=-1 lastRelayMs=-1 lastMatchedMs=-1"));
        for (const QString &line : logs.messages) QVERIFY(line.contains("session=1 report=1"));

        for (int i = 0; i < 20; ++i) send(box, controller, ack("gimbal_speed_ctrl"));
        QTRY_VERIFY(controller.responding());
        QTest::qWait(200);
        QCOMPARE(logs.messages.filter("ROUKE_RX_SNAPSHOT").size(), 1); // Burst never starts extra snapshots.
        QCOMPARE(logs.messages.filter("ROUKE_RX_RELAY_FIRST").size(), 1);
        QTRY_VERIFY_WITH_TIMEOUT(logs.messages.join('\n').contains("other=0 relay=20 invalidRelay=0"), 1500);
        QCOMPARE(logs.messages.filter("ROUKE_RX_SNAPSHOT").size(), 2);

        logs.messages.clear();
        send(foreign, controller, ack("gimbal_speed_ctrl"));
        QTRY_VERIFY_WITH_TIMEOUT(logs.messages.join('\n').contains("rejectPort=1"), 1500);
        output = logs.messages.join('\n');
        QVERIFY(output.contains(QString("lastSource=127.0.0.1:%1").arg(foreign.localPort())));
        QVERIFY(output.contains("peer=20"));
        QVERIFY(output.contains("relay=20")); // Rejected peer is visible but never decoded/admitted.

        logs.messages.clear();
        controller.setConfiguration(true, "127.0.0.1", nextBox.localPort(), 0, 7);
        output = logs.messages.join('\n');
        QVERIFY(output.contains(QString("ROUKE_RX_CONFIG session=2 peer=127.0.0.1:%1 localPort=%2")
                                    .arg(nextBox.localPort()).arg(controller.boundPort())));
        QVERIFY(output.contains("session=2 report=1 reason=configured"));
        QVERIFY(output.contains("udp=0 bytes=0 peer=0"));
        QVERIFY(output.contains("lastUdpMs=-1 lastPeerMs=-1 lastRelayMs=-1 lastMatchedMs=-1"));
        QVERIFY(!controller.responding());
        send(nextBox, controller, ack("gimbal_speed_ctrl"));
        QTRY_VERIFY(controller.responding());
        QCOMPARE(logs.messages.filter("ROUKE_RX_RELAY_FIRST").size(), 1);
        QVERIFY(logs.messages.filter("ROUKE_RX_RELAY_FIRST").first().contains("session=2"));

        controller.setConfiguration(false, {}, 0, 0, -1);
        logs.messages.clear();
        QTest::qWait(1100);
        QVERIFY(logs.messages.isEmpty()); // Disabled endpoint has no periodic timer output.
    }

    void zeroReceiveTimeoutProducesShortCorrelatedStages()
    {
        QUdpSocket box;
        QVERIFY(box.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        PayloadLogCapture logs;
        RoukePayloadController controller;
        controller.setConfiguration(true, "127.0.0.1", box.localPort(), 0, 7);
        logs.messages.clear();
        QVERIFY(controller.requestCameraParameter("iso"));
        receiveStreamRequest(box);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.pending(), 4500);
        QCOMPARE(logs.messages.size(), 3);
        const QString output = logs.messages.join('\n');
        QVERIFY(output.contains("reason=timeout cmd=lr1_iso"));
        QVERIFY(output.contains("udp=0 bytes=0 peer=0"));
        QVERIFY(output.contains("other=0 relay=0 invalidRelay=0"));
        QVERIFY(output.contains("lastRelayMs=-1"));
        QVERIFY(output.contains("valid=0 matched=0"));
        for (const QString &line : logs.messages) {
            QVERIFY(line.contains("session=1 report=1"));
            QVERIFY(line.toUtf8().size() < 600);
        }
        QVERIFY(controller.cameraParameters().isEmpty());
    }

    void singleRecordingButtonStopsWhileStartIsPending()
    {
        QUdpSocket box;
        QVERIFY(box.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        RoukePayloadController controller;
        controller.setConfiguration(true, "127.0.0.1", box.localPort(), 0, 7);
        QVERIFY(!controller.recordingStopRequired());
        QVERIFY(controller.setRecording(!controller.recordingStopRequired()));
        QCOMPARE(receiveStreamRequest(box).value("data").toObject().value("action").toInt(), 0);
        QVERIFY(controller.recordingStopRequired());
        QVERIFY(!controller.recordingKnown());
        QVERIFY(controller.setRecording(!controller.recordingStopRequired()));
        QVERIFY(controller.recordingStopQueued());
        QVERIFY(!box.hasPendingDatagrams());
        send(box, controller, ack("lr1_record"));
        QTRY_VERIFY(!controller.recordingStopQueued());
        QVERIFY(controller.recordingStopRequired());
        QVERIFY(controller.recording());
        QCOMPARE(receiveStreamRequest(box).value("data").toObject().value("action").toInt(), 1);
        // Repeated clicks during STOP cannot enqueue another STOP or a START.
        for (int i = 0; i < 3; ++i) QVERIFY(controller.setRecording(!controller.recordingStopRequired()));
        QVERIFY(!controller.recordingStopQueued());
        QVERIFY(!box.hasPendingDatagrams());
        send(box, controller, ack("lr1_record"));
        QTRY_VERIFY(!controller.pending());
        QVERIFY(!controller.recordingStopRequired());
        QVERIFY(controller.recordingKnown());
        QVERIFY(!controller.recording());
        QVERIFY(!box.hasPendingDatagrams());
    }

    void singleRecordingButtonStartTimeoutNeverRestarts()
    {
        QUdpSocket box;
        QVERIFY(box.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        RoukePayloadController controller;
        controller.setConfiguration(true, "127.0.0.1", box.localPort(), 0, 7);
        QVERIFY(controller.setRecording(true));
        receiveStreamRequest(box);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.pending(), 4500);
        QVERIFY(controller.recordingStopRequired());
        QVERIFY(!controller.recordingKnown());
        QVERIFY(!controller.setRecording(true)); // Explicit API also respects the latch.
        QVERIFY(!box.hasPendingDatagrams());
        for (int i = 0; i < 2; ++i) {
            QVERIFY(controller.setRecording(!controller.recordingStopRequired()));
            QCOMPARE(receiveStreamRequest(box).value("data").toObject().value("action").toInt(), 1);
            send(box, controller, ack("lr1_record")); // Could still be a delayed START ACK.
            QTRY_VERIFY(!controller.pending());
            QVERIFY(controller.recordingStopRequired());
            QVERIFY(!controller.recordingKnown());
        }
    }

    void singleRecordingButtonRejectedStartAndStop()
    {
        QUdpSocket box;
        QVERIFY(box.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        RoukePayloadController controller;
        controller.setConfiguration(true, "127.0.0.1", box.localPort(), 0, 7);
        QVERIFY(controller.setRecording(true));
        receiveStreamRequest(box);
        send(box, controller, ack("lr1_record", 1));
        QTRY_VERIFY(!controller.pending());
        QVERIFY(!controller.recordingStopRequired());
        QVERIFY(!controller.recording());
        QVERIFY(controller.setRecording(!controller.recordingStopRequired()));
        QCOMPARE(receiveStreamRequest(box).value("data").toObject().value("action").toInt(), 0);
        send(box, controller, ack("lr1_record"));
        QTRY_VERIFY(controller.recording());
        QVERIFY(controller.setRecording(!controller.recordingStopRequired()));
        receiveStreamRequest(box);
        send(box, controller, ack("lr1_record", 1));
        QTRY_VERIFY(!controller.pending());
        QVERIFY(controller.recordingStopRequired());
        QVERIFY(controller.recording()); // Refused STOP did not report a stopped camera.
        QVERIFY(controller.setRecording(!controller.recordingStopRequired()));
        QCOMPARE(receiveStreamRequest(box).value("data").toObject().value("action").toInt(), 1);
        send(box, controller, ack("lr1_record"));
        QTRY_VERIFY(!controller.recordingStopRequired());
        QVERIFY(!controller.recording());
    }

    void rejectedStartCannotCancelQueuedStopIntent()
    {
        QUdpSocket box;
        QVERIFY(box.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        RoukePayloadController controller;
        controller.setConfiguration(true, "127.0.0.1", box.localPort(), 0, 7);
        QVERIFY(controller.setRecording(true));
        receiveStreamRequest(box);
        QVERIFY(controller.setRecording(false));
        send(box, controller, ack("lr1_record", 1));
        QTRY_VERIFY(!controller.recordingStopQueued());
        QVERIFY(controller.pending());
        QVERIFY(controller.recordingStopRequired());
        QCOMPARE(receiveStreamRequest(box).value("data").toObject().value("action").toInt(), 1);
        send(box, controller, ack("lr1_record"));
        QTRY_VERIFY(!controller.pending());
        QVERIFY(!controller.recordingStopRequired());
    }

    void singleRecordingButtonRetriesStopAfterStopTimeout()
    {
        QUdpSocket box;
        QVERIFY(box.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        RoukePayloadController controller;
        controller.setConfiguration(true, "127.0.0.1", box.localPort(), 0, 7);
        QVERIFY(controller.setRecording(true));
        receiveStreamRequest(box);
        send(box, controller, ack("lr1_record"));
        QTRY_VERIFY(controller.recording());
        QVERIFY(controller.setRecording(false));
        receiveStreamRequest(box);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.pending(), 4500);
        QVERIFY(controller.recordingStopRequired());
        QVERIFY(!controller.recordingKnown());
        QVERIFY(controller.setRecording(!controller.recordingStopRequired()));
        QCOMPARE(receiveStreamRequest(box).value("data").toObject().value("action").toInt(), 1);
        send(box, controller, ack("lr1_record"));
        QTRY_VERIFY(!controller.recordingStopRequired());
        QVERIFY(controller.recordingKnown());
        QVERIFY(!controller.recording());
        QVERIFY(controller.setRecording(!controller.recordingStopRequired()));
        QCOMPARE(receiveStreamRequest(box).value("data").toObject().value("action").toInt(), 0);
    }

    void recordingStopIntentFollowsThePhysicalEndpoint()
    {
        QUdpSocket first, second, localReservation;
        QVERIFY(first.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        QVERIFY(second.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        QVERIFY(localReservation.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        const auto changedLocalPort = localReservation.localPort();
        localReservation.close();
        RoukePayloadController controller;
        controller.setConfiguration(true, "127.0.0.1", first.localPort(), 0, 7);
        QVERIFY(controller.setRecording(true));
        receiveStreamRequest(first);
        send(first, controller, ack("lr1_record"));
        QTRY_VERIFY(controller.recording());
        controller.setConfiguration(false, "127.0.0.1", first.localPort(), 0, 7);
        QVERIFY(controller.recordingStopRequired());
        QVERIFY(!controller.recordingKnown());
        controller.setConfiguration(true, "127.0.0.1", first.localPort(), changedLocalPort, 7);
        QVERIFY(controller.recordingStopRequired()); // Changing only the local socket preserves ownership.
        controller.setConfiguration(true, "127.0.0.1", first.localPort(), 0, 8);
        QVERIFY(!controller.recordingStopRequired()); // A different payload address is independent.
        controller.setConfiguration(true, "127.0.0.1", second.localPort(), 0, 7);
        QVERIFY(!controller.recordingStopRequired());
        QVERIFY(controller.setRecording(!controller.recordingStopRequired()));
        QCOMPARE(receiveStreamRequest(second).value("data").toObject().value("action").toInt(), 0);
        controller.setConfiguration(true, "127.0.0.1", first.localPort(), 0, 7);
        QVERIFY(controller.recordingStopRequired());
        QVERIFY(controller.setRecording(!controller.recordingStopRequired()));
        QCOMPARE(receiveStreamRequest(first).value("data").toObject().value("action").toInt(), 1);
        send(first, controller, ack("lr1_record"));
        QTRY_VERIFY(!controller.recordingStopRequired());
        controller.setConfiguration(true, "127.0.0.1", second.localPort(), 0, 7);
        QVERIFY(controller.recordingStopRequired()); // The interrupted second endpoint also keeps STOP.
    }

    void failedRecordingSubmissionDoesNotInventASession()
    {
        RoukePayloadController controller;
        controller.setConfiguration(true, "127.0.0.1", 0, 0, 7);
        QVERIFY(!controller.setRecording(true));
        QVERIFY(!controller.recordingStopRequired());
        QVERIFY(!controller.recordingKnown());
        QVERIFY(!controller.setRecording(false));
        QVERIFY(!controller.recordingStopRequired());
    }

    void sdCardCapacity_data()
    {
        QTest::addColumn<QJsonObject>("data");
        QTest::addColumn<int>("code");
        QTest::addColumn<bool>("known");
        QTest::newRow("present") << QJsonObject({{"total_capacity", 32000}, {"free_capacity", 17000}}) << 0 << true;
        QTest::newRow("full-is-present") << QJsonObject({{"total_capacity", 32000}, {"free_capacity", 0}}) << 0 << true;
        QTest::newRow("long-not-int32") << QJsonObject({{"total_capacity", 4294967296.0}, {"free_capacity", 1}}) << 0 << true;
        QTest::newRow("zero-is-unknown") << QJsonObject({{"total_capacity", 0}, {"free_capacity", 0}}) << 0 << false;
        QTest::newRow("missing") << QJsonObject{} << 0 << false;
        QTest::newRow("missing-free") << QJsonObject({{"total_capacity", 32000}}) << 0 << false;
        QTest::newRow("negative") << QJsonObject({{"total_capacity", 32000}, {"free_capacity", -1}}) << 0 << false;
        QTest::newRow("free-exceeds-total") << QJsonObject({{"total_capacity", 32000}, {"free_capacity", 32001}}) << 0 << false;
        QTest::newRow("fractional") << QJsonObject({{"total_capacity", 32000.5}, {"free_capacity", 1}}) << 0 << false;
        QTest::newRow("string") << QJsonObject({{"total_capacity", "32000"}, {"free_capacity", 1}}) << 0 << false;
        QTest::newRow("bool") << QJsonObject({{"total_capacity", true}, {"free_capacity", 0}}) << 0 << false;
        QTest::newRow("not-exact-json-integer") << QJsonObject({{"total_capacity", 9007199254740992.0}, {"free_capacity", 1}}) << 0 << false;
        QTest::newRow("rejected") << QJsonObject({{"total_capacity", 32000}, {"free_capacity", 17000}}) << 1 << false;
    }

    void sdCardCapacity()
    {
        QFETCH(QJsonObject, data);
        QFETCH(int, code);
        QFETCH(bool, known);
        QUdpSocket box;
        QVERIFY(box.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        RoukePayloadController controller;
        QSignalSpy finished(&controller, &RoukePayloadController::commandFinished);
        controller.setConfiguration(true, "127.0.0.1", box.localPort(), 0, 7);
        QVERIFY(!controller.sdCardStatusKnown());
        QVERIFY(!controller.sdCardInserted());
        QVERIFY(controller.requestSdCardStatus());
        QVERIFY(!controller.requestSdCardStatus());
        QVERIFY(!controller.pending());
        const auto request = receiveStreamRequest(box);
        QCOMPARE(request.value("cmd").toString(), QString("get_sdcard_capacity"));
        QCOMPARE(request.value("data").toObject(), QJsonObject{});
        sendStream(box, controller, sdCardAck(data, code));
        QTRY_VERIFY(!controller.sdCardQueryPending());
        QCOMPARE(controller.sdCardStatusKnown(), known);
        QCOMPARE(controller.sdCardInserted(), known);
        QCOMPARE(controller.lastError(), QString());
        QVERIFY(finished.isEmpty());
    }

    void sdCardQueryDoesNotOwnCameraLane()
    {
        QUdpSocket box;
        QVERIFY(box.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        RoukePayloadController controller;
        controller.setConfiguration(true, "127.0.0.1", box.localPort(), 0, 7);
        QVERIFY(controller.setCameraParameter("iso", 4));
        receiveStreamRequest(box);
        QVERIFY(controller.requestSdCardStatus());
        receiveStreamRequest(box);
        QCOMPARE(controller.pendingParameter(), QString("iso"));
        sendStream(box, controller, sdCardAck());
        QTRY_VERIFY(controller.sdCardInserted());
        QVERIFY(controller.pending());
        QVERIFY(controller.setRecording(false));
        send(box, controller, ack("lr1_iso"));
        QTRY_COMPARE(controller.pendingCommand(), QString("lr1_record"));
        const auto stop = receiveStreamRequest(box);
        QCOMPARE(stop.value("data").toObject().value("action").toInt(), 1);
        QCOMPARE(controller.cameraParameters().value("iso").toInt(), 4);
        send(box, controller, ack("lr1_record"));
        QTRY_VERIFY(!controller.pending());
        QVERIFY(controller.requestSdCardStatus());
        receiveStreamRequest(box);
        QVERIFY(controller.setRecording(false)); // SD query itself never queues STOP.
        QCOMPARE(receiveStreamRequest(box).value("cmd").toString(), QString("lr1_record"));
        QVERIFY(controller.sdCardQueryPending());
        send(box, controller, ack("lr1_record"));
        QTRY_VERIFY(!controller.pending());
        QVERIFY(controller.sdCardQueryPending());
    }

    void sdCardFailureIsIndependentAndCanRecover()
    {
        QUdpSocket box;
        QVERIFY(box.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        RoukePayloadController controller;
        controller.setConfiguration(true, "127.0.0.1", box.localPort(), 0, 7);
        QVERIFY(!controller.setCameraParameter("iso", -1));
        const auto cameraError = controller.lastError();
        QVERIFY(controller.requestSdCardStatus());
        receiveStreamRequest(box);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.sdCardQueryPending(), 4500);
        QVERIFY(!controller.sdCardStatusKnown());
        QVERIFY(!controller.sdCardInserted());
        QVERIFY(!controller.sdCardError().isEmpty());
        QCOMPARE(controller.lastError(), cameraError);
        sendStream(box, controller, sdCardAck()); // No outstanding query: ignore late ACK.
        QTest::qWait(30);
        QVERIFY(!controller.sdCardStatusKnown());
        QVERIFY(controller.requestSdCardStatus());
        receiveStreamRequest(box);
        sendStream(box, controller, sdCardAck());
        QTRY_VERIFY(controller.sdCardInserted());
        QVERIFY(controller.sdCardError().isEmpty());
        QCOMPARE(controller.lastError(), cameraError);
        QVERIFY(controller.requestSdCardStatus());
        receiveStreamRequest(box);
        sendStream(box, controller, sdCardAck({}, 1));
        QTRY_VERIFY(!controller.sdCardQueryPending());
        QVERIFY(!controller.sdCardStatusKnown());
        QVERIFY(!controller.sdCardInserted());
        QCOMPARE(controller.lastError(), cameraError);
    }

    void sdCardRouteAndSplitSerialAreChecked()
    {
        QUdpSocket box;
        QVERIFY(box.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        RoukePayloadController controller;
        controller.setConfiguration(true, "127.0.0.1", box.localPort(), 0, 7);
        QVERIFY(controller.requestSdCardStatus());
        receiveStreamRequest(box);
        sendStream(box, controller, sdCardAck(), 8);
        auto wrongTarget = sdCardAck();
        wrongTarget["target"] = "osdk";
        sendStream(box, controller, wrongTarget);
        auto wrongSource = sdCardAck();
        wrongSource["source"] = "osdk";
        sendStream(box, controller, wrongSource);
        QTest::qWait(30);
        QVERIFY(controller.sdCardQueryPending());
        QVERIFY(!controller.sdCardStatusKnown());
        sendStream(box, controller, sdCardAck());
        QTRY_VERIFY(!controller.sdCardQueryPending());
        const auto duplicate = rawFrame(sdCardAck());
        sendFragment(box, controller, duplicate.left(80));
        QTest::qWait(30);
        QVERIFY(controller.requestSdCardStatus());
        receiveStreamRequest(box);
        sendFragment(box, controller, duplicate.mid(80));
        QTest::qWait(30);
        QVERIFY(controller.sdCardQueryPending());
        sendStream(box, controller, sdCardAck());
        QTRY_VERIFY(!controller.sdCardQueryPending());
        QVERIFY(controller.sdCardInserted());
    }

    void sdCardFreshnessAndEndpointReset()
    {
        QUdpSocket box;
        QVERIFY(box.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        RoukePayloadController controller;
        controller.setConfiguration(true, "127.0.0.1", box.localPort(), 0, 7);
        QVERIFY(controller.requestSdCardStatus());
        receiveStreamRequest(box);
        sendStream(box, controller, sdCardAck());
        QTRY_VERIFY(controller.sdCardInserted());
        QTimer heartbeat;
        heartbeat.setInterval(1000);
        connect(&heartbeat, &QTimer::timeout, this, [&]() { send(box, controller, ack("lr1_focus")); });
        heartbeat.start();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.sdCardStatusKnown(), 27000);
        QVERIFY(controller.responding()); // Camera traffic cannot refresh SD telemetry.
        QVERIFY(!controller.sdCardInserted());
        heartbeat.stop();
        QVERIFY(controller.requestSdCardStatus());
        receiveStreamRequest(box);
        sendStream(box, controller, sdCardAck());
        QTRY_VERIFY(controller.sdCardInserted());
        controller.setConfiguration(true, "127.0.0.1", box.localPort(), 0, 8);
        QVERIFY(!controller.sdCardStatusKnown());
        QVERIFY(!controller.sdCardInserted());
        controller.setConfiguration(false, "127.0.0.1", box.localPort(), 0, 8);
        QVERIFY(!controller.sdCardQueryPending());
    }

    void cameraParameters_data()
    {
        QTest::addColumn<QString>("key");
        QTest::addColumn<int>("value");
        QTest::addColumn<QString>("command");
        QTest::addColumn<QJsonObject>("data");
        QTest::newRow("iso") << QString("iso") << 34 << QString("lr1_iso")
            << QJsonObject({{"action", 1}, {"value", 34}});
        QTest::newRow("white-balance") << QString("whiteBalance") << 14 << QString("lr1_white_balance")
            << QJsonObject({{"action", 1}, {"value", 14}});
        QTest::newRow("focus-mode") << QString("focusMode") << 4 << QString("lr1_focus_mode")
            << QJsonObject({{"mode", 4}});
        QTest::newRow("shutter-index") << QString("shutterSpeed") << 5 << QString("lr1_shutter_speed")
            << QJsonObject({{"shutter_speed", 5}});
        QTest::newRow("focus-raw") << QString("focusValue") << 1200 << QString("lr1_focus_value_set")
            << QJsonObject({{"value", 1200}});
        QTest::newRow("aperture-index") << QString("aperture") << 3 << QString("lr1_aperture")
            << QJsonObject({{"aperture_index", 3}});
        QTest::newRow("quality") << QString("imageQuality") << 4 << QString("lr1_image_quality")
            << QJsonObject({{"quality", 4}});
        QTest::newRow("size-zero-is-large") << QString("imageSize") << 0 << QString("lr1_image_size")
            << QJsonObject({{"operate", 1}, {"size", 0}});
        QTest::newRow("trigger-off") << QString("triggerPhoto") << 0 << QString("lr1_trigger_photo")
            << QJsonObject({{"enable", 0}});
        QTest::newRow("trigger-on") << QString("triggerPhoto") << 1 << QString("lr1_trigger_photo")
            << QJsonObject({{"enable", 1}});
        QTest::newRow("distance-metres") << QString("shootingDistance") << 20 << QString("lr1_shooting_distance")
            << QJsonObject({{"distance_m", 20}});
        QTest::newRow("threshold-centimetres") << QString("distanceThreshold") << 15000 << QString("lr1_distance_threshold")
            << QJsonObject({{"distance_cm", 15000}});
    }

    void cameraParameters()
    {
        QFETCH(QString, key);
        QFETCH(int, value);
        QFETCH(QString, command);
        QFETCH(QJsonObject, data);
        QUdpSocket box;
        QVERIFY(box.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        RoukePayloadController controller;
        QSignalSpy finished(&controller, &RoukePayloadController::commandFinished);
        controller.setConfiguration(true, "127.0.0.1", box.localPort(), 0, 7);
        QVERIFY(controller.cameraParameters().isEmpty());
        QVERIFY(!box.hasPendingDatagrams()); // In particular, no automatic trigger enable.
        QVERIFY(controller.setCameraParameter(key, value));
        QCOMPARE(controller.pendingParameter(), key);
        QVERIFY(!controller.cameraParameters().contains(key));
        const auto request = receiveStreamRequest(box);
        QCOMPARE(request.value("source").toString(), QString("msdk"));
        QCOMPARE(request.value("target").toString(), QString("psdk"));
        QCOMPARE(request.value("cmd").toString(), command);
        QCOMPARE(request.value("data").toObject(), data);
        send(box, controller, ack(command));
        QTRY_VERIFY(!controller.pending());
        QCOMPARE(controller.pendingParameter(), QString());
        QCOMPARE(controller.cameraParameters().value(key).toInt(), value);
        QVERIFY(finished.last().at(1).toBool());
        controller.setConfiguration(false, "127.0.0.1", box.localPort(), 0, 7);
        QVERIFY(controller.cameraParameters().isEmpty());
    }

    void cameraParameterReads_data()
    {
        QTest::addColumn<QString>("key");
        QTest::addColumn<QString>("command");
        QTest::addColumn<QJsonObject>("requestData");
        QTest::addColumn<QJsonObject>("responseData");
        QTest::newRow("iso") << QString("iso") << QString("lr1_iso")
            << QJsonObject({{"action", 0}, {"value", 0}}) << QJsonObject({{"value", 1600}});
        QTest::newRow("white-balance") << QString("whiteBalance") << QString("lr1_white_balance")
            << QJsonObject({{"action", 0}, {"value", 0}}) << QJsonObject({{"value", 3}});
        QTest::newRow("focus-value") << QString("focusValue") << QString("lr1_focus_value_get")
            << QJsonObject{} << QJsonObject({{"value", 1200}});
        QTest::newRow("image-size") << QString("imageSize") << QString("lr1_image_size")
            << QJsonObject({{"operate", 0}, {"size", 0}}) << QJsonObject({{"size", 2}});
    }

    void isoPhysicalReportReassemblesAndContinuesInitialization()
    {
        QUdpSocket box;
        QVERIFY(box.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        RoukePayloadController controller;
        QSignalSpy finished(&controller, &RoukePayloadController::commandFinished);
        controller.setConfiguration(true, "127.0.0.1", box.localPort(), 0, 7);
        controller.initializeCameraParameters();
        QTRY_VERIFY(box.hasPendingDatagrams());
        const auto request = receiveStreamRequest(box);
        QCOMPARE(request.value("cmd").toString(), QString("lr1_iso"));
        QCOMPARE(request.value("data").toObject().value("action").toInt(-1), 0);
        auto response = ack("lr1_iso");
        response["msg"] = QString::fromUtf8("\xe8\x8e\xb7\xe5\x8f\x96ISO"); // 获取ISO, as captured.
        response["data"] = QJsonObject{{"action", 0}, {"value", 1600}};
        const auto frame = rawFrame(response);
        QCOMPARE(frame.size(), 129); // The field capture's 128-byte + 1-byte ACK.
        sendFragment(box, controller, frame.left(128), 7, 255, 190, 106);
        QTest::qWait(30);
        QVERIFY(controller.pending());
        QCOMPARE(finished.count(), 0);
        QVERIFY(!controller.cameraParameters().contains("iso"));
        sendFragment(box, controller, frame.mid(128), 7, 255, 190, 107);
        QTRY_COMPARE(finished.count(), 1);
        QVERIFY(finished.last().at(1).toBool());
        QCOMPARE(controller.cameraParameters().value("iso").toInt(), 1600);

        const QStringList commands{"lr1_white_balance", "lr1_focus_value_get", "lr1_image_size"};
        const QList<int> values{0, 3214, 0};
        for (int i = 0; i < commands.size(); ++i) {
            QTRY_VERIFY(box.hasPendingDatagrams());
            const auto next = receiveStreamRequest(box);
            QCOMPARE(next.value("cmd").toString(), commands.at(i));
            sendStream(box, controller, parameterReport(next, values.at(i)));
            QTRY_COMPARE(finished.count(), i + 2);
            QVERIFY(finished.last().at(1).toBool());
        }
        QTRY_VERIFY(!controller.pending());
        QCOMPARE(controller.cameraParameters().size(), 4);
        QTest::qWait(50);
        QVERIFY(!box.hasPendingDatagrams());
    }

    void isoReadAcceptsReportedIntegerDomain_data()
    {
        QTest::addColumn<int>("value");
        QTest::newRow("zero") << 0;
        QTest::newRow("above-write-index-range") << 35;
        QTest::newRow("maximum-wire-integer") << std::numeric_limits<int>::max();
    }

    void isoReadAcceptsReportedIntegerDomain()
    {
        QFETCH(int, value);
        QUdpSocket box;
        QVERIFY(box.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        RoukePayloadController controller;
        QSignalSpy finished(&controller, &RoukePayloadController::commandFinished);
        controller.setConfiguration(true, "127.0.0.1", box.localPort(), 0, 7);
        QVERIFY(controller.requestCameraParameter("iso"));
        const auto request = receiveStreamRequest(box);
        sendStream(box, controller, parameterReport(request, value));
        QTRY_COMPARE(finished.count(), 1);
        QVERIFY(finished.last().at(1).toBool());
        QCOMPARE(controller.cameraParameters().value("iso").toInt(), value);
        QVERIFY(controller.lastError().isEmpty());
    }

    void isoReadIgnoresExplicitWriteOperation_data()
    {
        QTest::addColumn<int>("code");
        QTest::addColumn<QJsonValue>("action");
        QTest::newRow("successful-set") << 0 << QJsonValue(1);
        QTest::newRow("rejected-set") << 1 << QJsonValue(1);
        QTest::newRow("malformed-operation") << 0 << QJsonValue(QString("0"));
    }

    void isoReadIgnoresExplicitWriteOperation()
    {
        QFETCH(int, code);
        QFETCH(QJsonValue, action);
        QUdpSocket box;
        QVERIFY(box.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        RoukePayloadController controller;
        QSignalSpy finished(&controller, &RoukePayloadController::commandFinished);
        controller.setConfiguration(true, "127.0.0.1", box.localPort(), 0, 7);
        QVERIFY(controller.requestCameraParameter("iso"));
        receiveStreamRequest(box);
        auto response = ack("lr1_iso", code);
        response["data"] = QJsonObject{{"action", action}, {"value", 1600}};
        sendStream(box, controller, response);
        QTRY_VERIFY(controller.responding());
        QVERIFY(controller.pending());
        QCOMPARE(finished.count(), 0);
        QVERIFY(!controller.cameraParameters().contains("iso"));

        response["code"] = 0;
        response["data"] = QJsonObject{{"action", 0}, {"value", 1600}};
        sendStream(box, controller, response);
        QTRY_COMPARE(finished.count(), 1);
        QVERIFY(finished.last().at(1).toBool());
        QCOMPARE(controller.cameraParameters().value("iso").toInt(), 1600);
    }

    void cameraParameterReads()
    {
        QFETCH(QString, key);
        QFETCH(QString, command);
        QFETCH(QJsonObject, requestData);
        QFETCH(QJsonObject, responseData);
        QUdpSocket box;
        QVERIFY(box.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        RoukePayloadController controller;
        QSignalSpy finished(&controller, &RoukePayloadController::commandFinished);
        controller.setConfiguration(true, "127.0.0.1", box.localPort(), 0, 7);
        QVERIFY(controller.requestCameraParameter(key));
        QCOMPARE(controller.pendingParameter(), key);
        const auto request = receiveStreamRequest(box);
        QCOMPARE(request.value("cmd").toString(), command);
        QCOMPARE(request.value("data").toObject(), requestData);
        auto response = ack(command);
        response["data"] = responseData;
        const auto frame = rawFrame(response);
        for (int offset = 0; offset < frame.size(); offset += 128) sendFragment(box, controller, frame.mid(offset, 128));
        QTRY_VERIFY(!controller.pending());
        QCOMPARE(controller.cameraParameters().value(key).toInt(), responseData.begin().value().toInt());
        QVERIFY(finished.last().at(1).toBool());
        // A failed read preserves the previous confirmed report.
        QVERIFY(controller.requestCameraParameter(key));
        receiveStreamRequest(box);
        send(box, controller, ack(command));
        QTRY_VERIFY(!controller.pending());
        QCOMPARE(controller.cameraParameters().value(key).toInt(), responseData.begin().value().toInt());
        QVERIFY(!finished.last().at(1).toBool());
        QVERIFY(!controller.lastError().isEmpty());
    }

    void parameterReadRecoversAfterTimeout_data()
    {
        cameraParameterReads_data();
    }

    void parameterReadRecoversAfterTimeout()
    {
        QFETCH(QString, key);
        QFETCH(QString, command);
        QFETCH(QJsonObject, requestData);
        QFETCH(QJsonObject, responseData);
        QUdpSocket box;
        QVERIFY(box.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        RoukePayloadController controller;
        QSignalSpy finished(&controller, &RoukePayloadController::commandFinished);
        controller.setConfiguration(true, "127.0.0.1", box.localPort(), 0, 7);
        QVERIFY(controller.requestCameraParameter(key));
        QCOMPARE(receiveStreamRequest(box).value("data").toObject(), requestData);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.pending(), 4500);
        QVERIFY(!finished.last().at(1).toBool());

        QVERIFY(controller.requestCameraParameter(key));
        receiveStreamRequest(box);
        auto response = ack(command);
        response["data"] = responseData;
        sendStream(box, controller, response);
        QTRY_VERIFY(!controller.pending());
        QVERIFY(finished.last().at(1).toBool());
        QCOMPARE(controller.cameraParameters().value(key).toInt(), responseData.begin().value().toInt());
        QVERIFY(controller.lastError().isEmpty());
        QVERIFY(!box.hasPendingDatagrams()); // No hidden retry or follow-up write.
    }

    void readAfterUncertainWriteRequiresValueAndPreservesWriteLock_data()
    {
        QTest::addColumn<QString>("key");
        QTest::addColumn<QString>("command");
        QTest::addColumn<QString>("field");
        QTest::newRow("iso") << QString("iso") << QString("lr1_iso") << QString("value");
        QTest::newRow("white-balance") << QString("whiteBalance") << QString("lr1_white_balance") << QString("value");
        QTest::newRow("image-size") << QString("imageSize") << QString("lr1_image_size") << QString("size");
    }

    void readAfterUncertainWriteRequiresValueAndPreservesWriteLock()
    {
        QFETCH(QString, key);
        QFETCH(QString, command);
        QFETCH(QString, field);
        QUdpSocket box;
        QVERIFY(box.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        RoukePayloadController controller;
        QSignalSpy finished(&controller, &RoukePayloadController::commandFinished);
        controller.setConfiguration(true, "127.0.0.1", box.localPort(), 0, 7);
        QVERIFY(controller.setCameraParameter(key, 1));
        receiveStreamRequest(box);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.pending(), 4500);

        QVERIFY(controller.requestCameraParameter(key));
        receiveStreamRequest(box);
        const int completionCount = finished.count();
        sendStream(box, controller, ack(command)); // Delayed empty SET ACK, not a GET result.
        QTRY_VERIFY(controller.responding());
        QVERIFY(controller.pending());
        QCOMPARE(finished.count(), completionCount);
        QVERIFY(!controller.cameraParameters().contains(key));
        auto invalid = ack(command);
        invalid["data"] = QJsonObject{{field, -1}};
        sendStream(box, controller, invalid);
        QTest::qWait(20);
        QVERIFY(controller.pending());
        QCOMPARE(finished.count(), completionCount);

        auto report = ack(command);
        const int reportedValue = key == "iso" ? 1600 : 2;
        report["data"] = QJsonObject{{field, reportedValue}};
        sendStream(box, controller, report);
        QTRY_VERIFY(!controller.pending());
        QVERIFY(finished.last().at(1).toBool());
        QCOMPARE(controller.cameraParameters().value(key).toInt(), reportedValue);

        // Reporting a value did not prove the timed-out write outcome. A new
        // same-command SET still cannot consume an ambiguous delayed ACK.
        QVERIFY(controller.setCameraParameter(key, 3));
        receiveStreamRequest(box);
        sendStream(box, controller, ack(command));
        QTRY_VERIFY(!controller.pending());
        QVERIFY(!finished.last().at(1).toBool());
        QVERIFY(!controller.cameraParameters().contains(key));
    }

    void readTimeoutDoesNotPoisonLaterWrite_data()
    {
        QTest::addColumn<QString>("key");
        QTest::addColumn<QString>("command");
        QTest::addColumn<QString>("field");
        QTest::addColumn<bool>("emptyAck");
        const QList<QPair<QString, QString>> parameters = {
            {"iso", "lr1_iso"}, {"whiteBalance", "lr1_white_balance"}, {"imageSize", "lr1_image_size"},
        };
        for (const auto &parameter : parameters) {
            for (bool empty : {false, true}) {
                const QString row = parameter.first + (empty ? "-empty-set-ack" : "-target-value-report");
                QTest::newRow(qPrintable(row)) << parameter.first << parameter.second
                    << QString(parameter.first == "imageSize" ? "size" : "value") << empty;
            }
        }
    }

    void readTimeoutDoesNotPoisonLaterWrite()
    {
        QFETCH(QString, key);
        QFETCH(QString, command);
        QFETCH(QString, field);
        QFETCH(bool, emptyAck);
        QUdpSocket box;
        QVERIFY(box.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        RoukePayloadController controller;
        QSignalSpy finished(&controller, &RoukePayloadController::commandFinished);
        controller.setConfiguration(true, "127.0.0.1", box.localPort(), 0, 7);
        QVERIFY(controller.requestCameraParameter(key)); // Offline automatic read.
        receiveStreamRequest(box);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.pending(), 4500);
        QVERIFY(!controller.responding());
        QVERIFY(controller.setCameraParameter(key, 2)); // Device comes online.
        QCOMPARE(receiveStreamRequest(box).value("data").toObject().value(field).toInt(), 2);
        const int completionCount = finished.count();
        auto oldRead = ack(command);
        oldRead["data"] = QJsonObject{{field, 1}};
        sendStream(box, controller, oldRead);
        QTRY_VERIFY(controller.responding());
        QVERIFY(controller.pending());
        QCOMPARE(finished.count(), completionCount);
        QVERIFY(!controller.cameraParameters().contains(key));
        oldRead["data"] = QJsonObject{{field, -1}};
        sendStream(box, controller, oldRead);
        QTest::qWait(20);
        QVERIFY(controller.pending());

        auto response = ack(command);
        if (!emptyAck) response["data"] = QJsonObject{{field, 2}};
        sendStream(box, controller, response);
        QTRY_VERIFY(!controller.pending());
        QVERIFY(finished.last().at(1).toBool());
        QCOMPARE(controller.cameraParameters().value(key).toInt(), 2);
        QVERIFY(controller.lastError().isEmpty());
        QVERIFY(!box.hasPendingDatagrams());
    }

    void lateReadDoesNotExtendWriteDeadlineOrClearWriteUncertainty()
    {
        QUdpSocket box;
        QVERIFY(box.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        RoukePayloadController controller;
        QSignalSpy finished(&controller, &RoukePayloadController::commandFinished);
        controller.setConfiguration(true, "127.0.0.1", box.localPort(), 0, 7);
        QVERIFY(controller.requestCameraParameter("iso"));
        receiveStreamRequest(box);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.pending(), 4500);
        QVERIFY(controller.setCameraParameter("iso", 3));
        receiveStreamRequest(box);
        QTest::qWait(2700);
        auto oldRead = ack("lr1_iso");
        oldRead["data"] = QJsonObject{{"value", 1}};
        sendStream(box, controller, oldRead);
        QTRY_VERIFY(controller.responding());
        QVERIFY(controller.pending());
        // A late GET must not restart the original 3.5-second SET deadline.
        QTRY_VERIFY_WITH_TIMEOUT(!controller.pending(), 1600);
        QVERIFY(!finished.last().at(1).toBool());
        QVERIFY(!controller.cameraParameters().contains("iso"));

        QVERIFY(controller.requestCameraParameter("iso"));
        receiveStreamRequest(box);
        auto report = ack("lr1_iso");
        report["data"] = QJsonObject{{"value", 2}};
        sendStream(box, controller, report);
        QTRY_VERIFY(!controller.pending());
        QCOMPARE(controller.cameraParameters().value("iso").toInt(), 2);
        for (bool emptyAck : {false, true}) {
            QVERIFY(controller.setCameraParameter("iso", 3));
            receiveStreamRequest(box);
            auto response = ack("lr1_iso");
            if (!emptyAck) response["data"] = QJsonObject{{"value", 3}};
            sendStream(box, controller, response);
            QTRY_VERIFY(!controller.pending());
            QVERIFY(!finished.last().at(1).toBool());
            QVERIFY(!controller.cameraParameters().contains("iso"));
        }
    }

    void parameterReportCannotCompletePhotoCommand()
    {
        QUdpSocket box;
        QVERIFY(box.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        RoukePayloadController controller;
        controller.setConfiguration(true, "127.0.0.1", box.localPort(), 0, 7);
        QVERIFY(controller.takePhoto());
        receiveStreamRequest(box);
        auto report = ack("lr1_iso");
        report["data"] = QJsonObject{{"value", 4}};
        sendStream(box, controller, report);
        QTRY_VERIFY(controller.responding());
        QCOMPARE(controller.pendingCommand(), QString("lr1_take_photo"));
        QCOMPARE(controller.photoCount(), 0);
        QVERIFY(!controller.cameraParameters().contains("iso"));
        sendStream(box, controller, ack("lr1_take_photo"));
        QTRY_VERIFY(!controller.pending());
        QCOMPARE(controller.photoCount(), 1);
    }

    void invalidCameraParametersSendNothing()
    {
        QUdpSocket box;
        QVERIFY(box.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        RoukePayloadController controller;
        controller.setConfiguration(true, "127.0.0.1", box.localPort(), 0, 7);
        const QList<QPair<QString, QVariant>> invalid = {
            {"iso", -1}, {"iso", 35}, {"iso", 1600}, {"whiteBalance", 15}, {"focusMode", 5},
            {"imageQuality", 0}, {"imageQuality", 5}, {"imageSize", 4}, {"triggerPhoto", 2},
            {"shootingDistance", 19}, {"shootingDistance", 41},
            {"distanceThreshold", 29}, {"distanceThreshold", 15001},
            {"shutterSpeed", -1}, {"aperture", -1}, {"focusValue", -1},
            {"iso", 2.5}, {"iso", QString("2")}, {"iso", true}, {"missing", 0},
            {"aperture", 2147483648.0}, {"focusValue", std::numeric_limits<double>::infinity()},
        };
        for (const auto &item : invalid) {
            QVERIFY2(!controller.setCameraParameter(item.first, item.second), qPrintable(item.first));
            QVERIFY(!controller.pending());
        }
        QVERIFY(!controller.requestCameraParameter("triggerPhoto"));
        QVERIFY(!controller.requestCameraParameter("aperture"));
        QVERIFY(!controller.requestCameraParameter("missing"));
        QTest::qWait(30);
        QVERIFY(!box.hasPendingDatagrams());
    }

    void cameraParameterFailuresKeepStateHonest()
    {
        QUdpSocket box;
        QVERIFY(box.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        RoukePayloadController controller;
        QSignalSpy finished(&controller, &RoukePayloadController::commandFinished);
        controller.setConfiguration(true, "127.0.0.1", box.localPort(), 0, 7);
        QVERIFY(controller.setCameraParameter("iso", 7));
        receiveStreamRequest(box);
        send(box, controller, ack("lr1_iso"));
        QTRY_VERIFY(!controller.pending());
        QCOMPARE(controller.cameraParameters().value("iso").toInt(), 7);

        QVERIFY(controller.setCameraParameter("iso", 8));
        receiveStreamRequest(box);
        send(box, controller, ack("lr1_iso", 1));
        QTRY_VERIFY(!controller.pending());
        QCOMPARE(controller.cameraParameters().value("iso").toInt(), 7);
        QVERIFY(!finished.last().at(1).toBool());

        QVERIFY(controller.setCameraParameter("iso", 9));
        receiveStreamRequest(box);
        QVERIFY(controller.setRecording(false)); // Stop waits for camera ACK.
        QTRY_VERIFY_WITH_TIMEOUT(controller.pendingCommand() == QString("lr1_record"), 4500);
        QVERIFY(!controller.cameraParameters().contains("iso"));
        QCOMPARE(controller.pendingParameter(), QString());
        receiveStreamRequest(box);
        send(box, controller, ack("lr1_record"));
        QTRY_VERIFY(!controller.pending());

        QVERIFY(controller.setCameraParameter("iso", 10));
        receiveStreamRequest(box);
        send(box, controller, ack("lr1_iso")); // Could be the previous delayed setter.
        QTRY_VERIFY(!controller.pending());
        QVERIFY(!controller.cameraParameters().contains("iso"));
        QVERIFY(!finished.last().at(1).toBool());
    }

    void malformedParameterReadValuesAreNotConfirmed_data()
    {
        QTest::addColumn<QJsonValue>("value");
        QTest::newRow("missing") << QJsonValue(QJsonValue::Undefined);
        QTest::newRow("null") << QJsonValue();
        QTest::newRow("string") << QJsonValue(QString("2"));
        QTest::newRow("bool") << QJsonValue(true);
        QTest::newRow("fractional") << QJsonValue(2.5);
        QTest::newRow("negative") << QJsonValue(-1);
        QTest::newRow("integer-overflow") << QJsonValue(2147483648.0);
    }

    void ambiguousFocusWriteInvalidatesLaterRead()
    {
        QUdpSocket box;
        QVERIFY(box.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        RoukePayloadController controller;
        controller.setConfiguration(true, "127.0.0.1", box.localPort(), 0, 7);
        QVERIFY(controller.setCameraParameter("focusValue", 1200));
        receiveStreamRequest(box);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.pending(), 4500);
        QVERIFY(controller.requestCameraParameter("focusValue"));
        receiveStreamRequest(box);
        auto response = ack("lr1_focus_value_get");
        response["data"] = QJsonObject{{"value", 1300}};
        const auto frame = rawFrame(response);
        for (int offset = 0; offset < frame.size(); offset += 128) sendFragment(box, controller, frame.mid(offset, 128));
        QTRY_VERIFY(!controller.pending());
        QCOMPARE(controller.cameraParameters().value("focusValue").toInt(), 1300);
        QVERIFY(controller.setCameraParameter("focusValue", 1400));
        receiveStreamRequest(box);
        send(box, controller, ack("lr1_focus_value_set"));
        QTRY_VERIFY(!controller.pending());
        QVERIFY(!controller.cameraParameters().contains("focusValue"));
        QVERIFY(!controller.lastError().isEmpty());
    }

    void focusModeAndAutofocusInvalidateRawPosition()
    {
        QUdpSocket box;
        QVERIFY(box.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        RoukePayloadController controller;
        controller.setConfiguration(true, "127.0.0.1", box.localPort(), 0, 7);
        for (bool changeMode : {true, false}) {
            QVERIFY(controller.setCameraParameter("focusValue", 1200));
            receiveStreamRequest(box);
            send(box, controller, ack("lr1_focus_value_set"));
            QTRY_VERIFY(!controller.pending());
            QVERIFY(controller.cameraParameters().contains("focusValue"));
            QVERIFY(changeMode ? controller.setCameraParameter("focusMode", 0) : controller.autoFocus());
            receiveStreamRequest(box);
            send(box, controller, ack(changeMode ? "lr1_focus_mode" : "lr1_focus"));
            QTRY_VERIFY(!controller.pending());
            QVERIFY(!controller.cameraParameters().contains("focusValue"));
        }
    }

    void uncertainFocusModeInvalidatesRawPosition()
    {
        QUdpSocket box;
        QVERIFY(box.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        RoukePayloadController controller;
        controller.setConfiguration(true, "127.0.0.1", box.localPort(), 0, 7);
        QVERIFY(controller.setCameraParameter("focusValue", 1200));
        receiveStreamRequest(box);
        send(box, controller, ack("lr1_focus_value_set"));
        QTRY_VERIFY(!controller.pending());
        QVERIFY(controller.cameraParameters().contains("focusValue"));

        QVERIFY(controller.setCameraParameter("focusMode", 0));
        receiveStreamRequest(box);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.pending(), 4500);
        QVERIFY(!controller.cameraParameters().contains("focusMode"));
        QVERIFY(!controller.cameraParameters().contains("focusValue"));

        // Even a later raw-position confirmation must be retired when another
        // mode request has an ambiguous ACK in the same endpoint session.
        QVERIFY(controller.setCameraParameter("focusValue", 1300));
        receiveStreamRequest(box);
        send(box, controller, ack("lr1_focus_value_set"));
        QTRY_VERIFY(!controller.pending());
        QVERIFY(controller.cameraParameters().contains("focusValue"));
        QVERIFY(controller.setCameraParameter("focusMode", 1));
        receiveStreamRequest(box);
        send(box, controller, ack("lr1_focus_mode"));
        QTRY_VERIFY(!controller.pending());
        QVERIFY(!controller.cameraParameters().contains("focusMode"));
        QVERIFY(!controller.cameraParameters().contains("focusValue"));
    }

    void malformedParameterReadValuesAreNotConfirmed()
    {
        QFETCH(QJsonValue, value);
        QUdpSocket box;
        QVERIFY(box.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        RoukePayloadController controller;
        QSignalSpy finished(&controller, &RoukePayloadController::commandFinished);
        controller.setConfiguration(true, "127.0.0.1", box.localPort(), 0, 7);
        QVERIFY(controller.requestCameraParameter("iso"));
        receiveStreamRequest(box);
        auto response = ack("lr1_iso");
        response["data"] = QJsonObject{{"value", value}};
        const auto frame = rawFrame(response);
        for (int offset = 0; offset < frame.size(); offset += 128) sendFragment(box, controller, frame.mid(offset, 128));
        QTRY_VERIFY(!controller.pending());
        QVERIFY(!controller.cameraParameters().contains("iso"));
        QVERIFY(!finished.last().at(1).toBool());
    }

    void responseExpiryRetainsLastConfirmedCameraParameters()
    {
        QUdpSocket box;
        QVERIFY(box.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        RoukePayloadController controller;
        controller.setConfiguration(true, "127.0.0.1", box.localPort(), 0, 7);
        QVERIFY(controller.setCameraParameter("triggerPhoto", 0));
        receiveStreamRequest(box);
        send(box, controller, ack("lr1_trigger_photo"));
        QTRY_VERIFY(!controller.pending());
        QVERIFY(controller.cameraParameters().contains("triggerPhoto"));
        QTRY_VERIFY_WITH_TIMEOUT(!controller.responding(), 16000);
        QCOMPARE(controller.cameraParameters().value("triggerPhoto").toInt(), 0);
    }

    void commandFragments_data()
    {
        QTest::addColumn<int>("frameLength");
        QTest::newRow("one-full-relay") << 128;
        QTest::newRow("one-byte-continuation") << 129;
        QTest::newRow("gimbal-speed-128-plus-27") << 155;
        QTest::newRow("exact-two-relays") << 256;
        QTest::newRow("three-relays") << 321;
    }

    void commandFragments()
    {
        QFETCH(int, frameLength);
        QUdpSocket box;
        QVERIFY(box.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        RoukePayloadController controller;
        controller.setConfiguration(true, "127.0.0.1", box.localPort(), 0, 0);
        const QString command = frameLength == 155 ? QStringLiteral("gimbal_speed_ctrl")
                                                   : QStringLiteral("stream_fixture");
        QJsonObject data = frameLength == 155
            ? QJsonObject{{"yaw_speed", 350}, {"pitch_speed", 175}, {"roll_speed", 0}}
            : QJsonObject{{"padding", QString()}};
        QJsonObject request{{"source", "msdk"}, {"target", "psdk"}, {"type", "req"},
                            {"cmd", command}, {"code", 0}, {"msg", ""}, {"data", data}};
        if (frameLength != 155) {
            const int padding = frameLength - rawFrame(request).size();
            QVERIFY(padding >= 0);
            data["padding"] = QString(padding, QLatin1Char('x'));
            request["data"] = data;
        }
        const QByteArray expected = rawFrame(request);
        QCOMPARE(expected.size(), frameLength);
        QVERIFY(controller.sendCommand(command, data));
        const bool isSpeed = command == QStringLiteral("gimbal_speed_ctrl");
        QCOMPARE(controller.pending(), !isSpeed);
        if (!isSpeed) QVERIFY(!controller.autoFocus()); // Camera requests stay serialized.
        QByteArray received;
        const int packetCount = (frameLength + 127) / 128;
        for (int index = 0; index < packetCount; ++index) {
            mavlink_message_t message{};
            mavlink_vk_payload_data_relay_t relay{};
            QVERIFY(receiveRelay(box, &message, &relay));
            QCOMPARE(message.sysid, uint8_t(255));
            QCOMPARE(message.compid, uint8_t(MAV_COMP_ID_MISSIONPLANNER));
            QCOMPARE(message.seq, uint8_t(index));
            QCOMPARE(relay.payload_addr, uint16_t(0));
            QCOMPARE(relay.reserve, uint8_t(0));
            QCOMPARE(int(relay.data_len), qMin(128, frameLength - index * 128));
            for (int i = relay.data_len; i < 128; ++i) QCOMPARE(relay.data[i], uint8_t(0));
            received.append(reinterpret_cast<const char *>(relay.data), relay.data_len);
        }
        QCOMPARE(received, expected);
        QTest::qWait(30);
        QVERIFY(!box.hasPendingDatagrams()); // No empty final fragment or automatic retry.
        QCOMPARE(controller.pending(), !isSpeed);
        send(box, controller, ack(command), 0);
        QTRY_VERIFY(!controller.pending());
        QTRY_VERIFY(controller.responding());
    }

    void speedProtocolSteps_data()
    {
        QTest::addColumn<int>("input");
        QTest::addColumn<int>("wire");
        QTest::newRow("stop") << 0 << 0;
        QTest::newRow("first-step") << 35 << 35;
        QTest::newRow("first-step-upper-bound") << 69 << 35;
        QTest::newRow("second-step") << 70 << 70;
        QTest::newRow("second-step-upper-bound") << 104 << 70;
        QTest::newRow("third-step") << 105 << 105;
        QTest::newRow("below-maximum") << 524 << 490;
        QTest::newRow("maximum") << 525 << 525;
    }

    void speedProtocolSteps()
    {
        QFETCH(int, input);
        QFETCH(int, wire);
        QUdpSocket box;
        QVERIFY(box.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        RoukePayloadController controller;
        controller.setConfiguration(true, "127.0.0.1", box.localPort(), 0, 7);
        QVERIFY(controller.sendGimbalSpeed(input, -input));
        QCOMPARE(receiveStreamRequest(box).value("data").toObject(),
                 QJsonObject({{"yaw_speed", wire}, {"pitch_speed", -wire}, {"roll_speed", 0}}));
        // The generic command entry point uses the same signed quantization.
        QVERIFY(controller.sendCommand("gimbal_speed_ctrl",
                {{"yaw_speed", -input}, {"pitch_speed", input}, {"roll_speed", 0}}));
        QCOMPARE(receiveStreamRequest(box).value("data").toObject(),
                 QJsonObject({{"yaw_speed", -wire}, {"pitch_speed", wire}, {"roll_speed", 0}}));
        QVERIFY(!controller.pending());
    }

    void invalidSpeedNeverWritesOrClearsStopObligation_data()
    {
        QTest::addColumn<int>("invalid");
        QTest::newRow("old-yaw-default") << 10;
        QTest::newRow("old-pitch-default") << 5;
        QTest::newRow("positive-below-minimum") << 34;
        QTest::newRow("negative-below-minimum") << -34;
        QTest::newRow("positive-above-maximum") << 526;
        QTest::newRow("negative-above-maximum") << -526;
        QTest::newRow("integer-maximum") << std::numeric_limits<int>::max();
        QTest::newRow("integer-minimum") << std::numeric_limits<int>::min();
    }

    void invalidSpeedNeverWritesOrClearsStopObligation()
    {
        QFETCH(int, invalid);
        QUdpSocket box;
        QVERIFY(box.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        RoukePayloadController controller;
        controller.setConfiguration(true, "127.0.0.1", box.localPort(), 0, 7);
        QVERIFY(controller.takePhoto());
        receiveStreamRequest(box);
        QVERIFY(controller.sendGimbalSpeed(350, -175));
        receiveStreamRequest(box);
        QVERIFY(!controller.sendGimbalSpeed(invalid, 0));
        QVERIFY(!controller.sendGimbalSpeed(0, invalid));
        QVERIFY(!controller.sendCommand("gimbal_speed_ctrl",
                {{"yaw_speed", invalid}, {"pitch_speed", 0}, {"roll_speed", 0}}));
        QVERIFY(!controller.sendCommand("gimbal_speed_ctrl",
                {{"yaw_speed", 0}, {"pitch_speed", invalid}, {"roll_speed", 0}}));
        QVERIFY(!controller.lastError().isEmpty());
        QCOMPARE(controller.pendingCommand(), QString("lr1_take_photo"));
        QTest::qWait(30);
        QVERIFY(!box.hasPendingDatagrams());
        // Rejecting invalid input must not lose the obligation to stop a prior
        // successful nonzero command before centering.
        QVERIFY(controller.centerGimbal());
        QCOMPARE(receiveStreamRequest(box).value("data").toObject(),
                 QJsonObject({{"yaw_speed", 0}, {"pitch_speed", 0}, {"roll_speed", 0}}));
        QCOMPARE(receiveStreamRequest(box).value("cmd").toString(), QString("gimbal_center"));
        QCOMPARE(controller.pendingCommand(), QString("lr1_take_photo"));
    }

    void speedAckLevelsDoNotReplaceWireTargetsOrCameraPending()
    {
        QUdpSocket box;
        QVERIFY(box.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        RoukePayloadController controller;
        controller.setConfiguration(true, "127.0.0.1", box.localPort(), 0, 7);
        QSignalSpy rejected(&controller, &RoukePayloadController::gimbalControlError);
        QSignalSpy state(&controller, &RoukePayloadController::stateChanged);
        QVERIFY(controller.takePhoto());
        receiveStreamRequest(box);
        for (const int level : {1, 5, 10, 15}) {
            QVERIFY(controller.sendGimbalSpeed(level * 35, -level * 35));
            QCOMPARE(receiveStreamRequest(box).value("data").toObject(),
                     QJsonObject({{"yaw_speed", level * 35}, {"pitch_speed", -level * 35}, {"roll_speed", 0}}));
            auto response = ack("gimbal_speed_ctrl");
            response["data"] = QJsonObject{{"yaw_speed", level}, {"pitch_speed", -level}, {"roll_speed", 0}};
            state.clear();
            sendStream(box, controller, response);
            QTRY_VERIFY(!state.isEmpty());
            QVERIFY(controller.responding());
            QCOMPARE(rejected.size(), 0);
            QCOMPARE(controller.pendingCommand(), QString("lr1_take_photo"));
            QVERIFY(controller.lastError().isEmpty());
            QVERIFY(!box.hasPendingDatagrams()); // ACK values never become a new speed command.
        }
        QVERIFY(controller.sendGimbalSpeed(0, 0));
        QCOMPARE(receiveStreamRequest(box).value("data").toObject(),
                 QJsonObject({{"yaw_speed", 0}, {"pitch_speed", 0}, {"roll_speed", 0}}));
        send(box, controller, ack("lr1_take_photo"));
        QTRY_COMPARE(controller.photoCount(), 1);
    }

    void speedAndStopBypassCameraPendingWithoutInterleaving()
    {
        QUdpSocket box;
        QVERIFY(box.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        RoukePayloadController controller;
        controller.setConfiguration(true, "127.0.0.1", box.localPort(), 0, 7);
        QVERIFY(controller.takePhoto());
        QVERIFY(controller.sendGimbalSpeed(350, -175));
        QVERIFY(controller.sendGimbalSpeed(0, 0));
        QCOMPARE(receiveStreamRequest(box).value("cmd").toString(), QString("lr1_take_photo"));
        auto speed = receiveStreamRequest(box);
        QCOMPARE(speed.value("cmd").toString(), QString("gimbal_speed_ctrl"));
        QCOMPARE(speed.value("data").toObject(), QJsonObject({{"yaw_speed", 350}, {"pitch_speed", -175}, {"roll_speed", 0}}));
        speed = receiveStreamRequest(box);
        QCOMPARE(speed.value("data").toObject(), QJsonObject({{"yaw_speed", 0}, {"pitch_speed", 0}, {"roll_speed", 0}}));
        QCOMPARE(controller.pendingCommand(), QString("lr1_take_photo"));
        send(box, controller, ack("gimbal_speed_ctrl"));
        QTRY_VERIFY(controller.responding());
        QVERIFY(controller.pending());
        QCOMPARE(controller.photoCount(), 0);
        send(box, controller, ack("lr1_take_photo"));
        QTRY_COMPARE(controller.photoCount(), 1);
        QVERIFY(!box.hasPendingDatagrams());
    }

    void speedAndCenterPreservePartialCameraAck()
    {
        QUdpSocket box;
        QVERIFY(box.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        RoukePayloadController controller;
        controller.setConfiguration(true, "127.0.0.1", box.localPort(), 0, 7);
        QVERIFY(controller.takePhoto());
        receiveRequest(box);
        auto response = ack("lr1_take_photo");
        response["msg"] = QString(80, QLatin1Char('x'));
        const QByteArray frame = rawFrame(response);
        sendFragment(box, controller, frame.left(128));
        QTest::qWait(30);
        QVERIFY(controller.sendGimbalSpeed(350, 175));
        QCOMPARE(receiveStreamRequest(box).value("cmd").toString(), QString("gimbal_speed_ctrl"));
        QVERIFY(controller.centerGimbal());
        QCOMPARE(receiveStreamRequest(box).value("data").toObject().value("yaw_speed").toInt(-1), 0);
        QCOMPARE(receiveRequest(box).value("cmd").toString(), QString("gimbal_center"));
        QVERIFY(controller.gimbalCenterPending());
        sendFragment(box, controller, frame.mid(128));
        QTRY_COMPARE(controller.photoCount(), 1);
        QVERIFY(controller.gimbalCenterPending());
        send(box, controller, ack("gimbal_center"));
        QTRY_VERIFY(!controller.gimbalCenterPending());
    }

    void cameraPreservesPartialCenterAckAndCenterIsIndependent()
    {
        QUdpSocket box;
        QVERIFY(box.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        RoukePayloadController controller;
        controller.setConfiguration(true, "127.0.0.1", box.localPort(), 0, 7);
        QSignalSpy completed(&controller, &RoukePayloadController::gimbalCenterFinished);
        QVERIFY(controller.centerGimbal());
        QCOMPARE(receiveRequest(box).value("cmd").toString(), QString("gimbal_center"));
        QVERIFY(!controller.pending());
        QVERIFY(!controller.centerGimbal());
        QVERIFY(!box.hasPendingDatagrams());
        auto response = ack("gimbal_center");
        response["msg"] = QString(80, QLatin1Char('x'));
        const QByteArray frame = rawFrame(response);
        sendFragment(box, controller, frame.left(128));
        QTest::qWait(30);
        QVERIFY(controller.setRecording(true));
        receiveRequest(box);
        sendFragment(box, controller, frame.mid(128));
        QTRY_COMPARE(completed.count(), 1);
        QVERIFY(completed.at(0).at(0).toBool());
        QVERIFY(controller.pending());
        QVERIFY(!controller.recordingKnown());
        send(box, controller, ack("lr1_record"));
        QTRY_VERIFY(controller.recordingKnown());
        QVERIFY(controller.recording());
    }

    void repeatedSpeedRejectionSignalsOnceAndPreservesCameraError()
    {
        QUdpSocket box;
        QVERIFY(box.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        RoukePayloadController controller;
        controller.setConfiguration(true, "127.0.0.1", box.localPort(), 0, 7);
        QSignalSpy rejected(&controller, &RoukePayloadController::gimbalControlError);
        QVERIFY(controller.takePhoto());
        receiveRequest(box);
        auto photoResponse = ack("lr1_take_photo", 1);
        photoResponse["msg"] = "camera storage full";
        send(box, controller, photoResponse);
        QTRY_VERIFY(!controller.pending());
        const QString cameraError = controller.lastError();
        QVERIFY(controller.sendGimbalSpeed(350, 175));
        receiveStreamRequest(box);
        connect(&controller, &RoukePayloadController::gimbalControlError, &controller, [&controller]() {
            QVERIFY(controller.sendGimbalSpeed(0, 0));
        });
        send(box, controller, ack("gimbal_speed_ctrl", 1));
        QTRY_COMPARE(rejected.count(), 1);
        QCOMPARE(receiveStreamRequest(box).value("data").toObject().value("yaw_speed").toInt(-1), 0);
        send(box, controller, ack("gimbal_speed_ctrl", 1));
        send(box, controller, ack("gimbal_speed_ctrl"));
        QTest::qWait(30);
        QCOMPARE(rejected.count(), 1);
        QCOMPARE(controller.lastError(), cameraError);
        QVERIFY(!box.hasPendingDatagrams());
    }

    void centerTimeoutDoesNotInvalidateCameraStateAndLateAckIsAmbiguous()
    {
        QUdpSocket box;
        QVERIFY(box.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        RoukePayloadController controller;
        controller.setConfiguration(true, "127.0.0.1", box.localPort(), 0, 7);
        QSignalSpy completed(&controller, &RoukePayloadController::gimbalCenterFinished);
        QVERIFY(controller.setRecording(true));
        receiveRequest(box);
        send(box, controller, ack("lr1_record"));
        QTRY_VERIFY(controller.recordingKnown());
        QVERIFY(controller.centerGimbal());
        receiveRequest(box);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.gimbalCenterPending(), 4500);
        QCOMPARE(completed.count(), 1);
        QVERIFY(!completed.at(0).at(0).toBool());
        QVERIFY(controller.recordingKnown());
        QVERIFY(controller.recording());
        QVERIFY(!box.hasPendingDatagrams());
        QVERIFY(controller.centerGimbal());
        receiveRequest(box);
        send(box, controller, ack("gimbal_center"));
        QTRY_COMPARE(completed.count(), 2);
        QVERIFY(!completed.at(1).at(0).toBool());
        QVERIFY(!controller.lastError().isEmpty());
        QVERIFY(controller.recordingKnown());
    }

    void duplicateCenterAckCannotCompleteReentrantNewRequest()
    {
        QUdpSocket box;
        QVERIFY(box.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        RoukePayloadController controller;
        controller.setConfiguration(true, "127.0.0.1", box.localPort(), 0, 7);
        QSignalSpy completed(&controller, &RoukePayloadController::gimbalCenterFinished);
        QVERIFY(controller.centerGimbal());
        receiveRequest(box);
        connect(&controller, &RoukePayloadController::gimbalCenterFinished, &controller, [&controller](bool success) {
            if (success) QVERIFY(controller.centerGimbal());
        });
        const QByteArray bytes = messageBytes(ack("gimbal_center"));
        box.writeDatagram(bytes + bytes, QHostAddress::LocalHost, controller.boundPort());
        QTRY_COMPARE(completed.count(), 1);
        QVERIFY(controller.gimbalCenterPending());
        receiveRequest(box);
        QVERIFY(!box.hasPendingDatagrams());
    }

    void unsolicitedCenterAckPublishesLivenessWithoutCompletingReentrantRequest()
    {
        QUdpSocket box;
        QVERIFY(box.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        RoukePayloadController controller;
        controller.setConfiguration(true, "127.0.0.1", box.localPort(), 0, 7);
        QSignalSpy state(&controller, &RoukePayloadController::stateChanged);
        QSignalSpy finished(&controller, &RoukePayloadController::gimbalCenterFinished);
        bool requested = false;
        bool submitted = false;
        connect(&controller, &RoukePayloadController::stateChanged, &controller, [&]() {
            if (controller.responding() && !requested) {
                requested = true; // centerGimbal itself emits stateChanged.
                submitted = controller.centerGimbal();
            }
        });
        // A valid unsolicited ACK proves endpoint liveness. Publishing that
        // state may synchronously start a new request, which this ACK cannot own.
        send(box, controller, ack("gimbal_center"));
        QTRY_VERIFY(controller.responding());
        QVERIFY(!state.isEmpty());
        QVERIFY(requested);
        QVERIFY(submitted);
        QVERIFY(controller.gimbalCenterPending());
        QCOMPARE(finished.size(), 0);
        QCOMPARE(receiveStreamRequest(box).value("cmd").toString(), QString("gimbal_center"));
        send(box, controller, ack("gimbal_center"));
        QTRY_VERIFY(!controller.gimbalCenterPending());
        QCOMPARE(finished.size(), 1);
        QCOMPARE(finished.first().first().toBool(), true);
    }

    void staleCenterAckPublishesLivenessWithoutCompletingCurrentRequest()
    {
        QUdpSocket box;
        QVERIFY(box.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        RoukePayloadController controller;
        controller.setConfiguration(true, "127.0.0.1", box.localPort(), 0, 7);
        const QByteArray frame = rawFrame(ack("gimbal_center"));
        sendFragment(box, controller, frame.left(20));
        QTest::qWait(30); // Stamp the inner frame with the pre-request serial.
        QVERIFY(!controller.responding());
        QVERIFY(controller.centerGimbal());
        receiveStreamRequest(box);
        QSignalSpy state(&controller, &RoukePayloadController::stateChanged);
        QSignalSpy finished(&controller, &RoukePayloadController::gimbalCenterFinished);
        sendFragment(box, controller, frame.mid(20));
        QTRY_VERIFY(controller.responding());
        QCOMPARE(state.size(), 1);
        QVERIFY(controller.gimbalCenterPending());
        QCOMPARE(finished.size(), 0);
        send(box, controller, ack("gimbal_center"));
        QTRY_VERIFY(!controller.gimbalCenterPending());
        QCOMPARE(finished.size(), 1);
        QCOMPARE(finished.first().first().toBool(), true);
    }

    void endpointChangeStopsOldPeerAndCancelsCenter()
    {
        QUdpSocket first, second;
        QVERIFY(first.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        QVERIFY(second.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        RoukePayloadController controller;
        controller.setConfiguration(true, "127.0.0.1", first.localPort(), 0, 7);
        QVERIFY(controller.sendGimbalSpeed(350, -175));
        receiveStreamRequest(first);
        controller.setConfiguration(true, "127.0.0.1", second.localPort(), 0, 7);
        QCOMPARE(receiveStreamRequest(first).value("data").toObject(), QJsonObject({{"yaw_speed", 0}, {"pitch_speed", 0}, {"roll_speed", 0}}));
        QVERIFY(!second.hasPendingDatagrams());
        QVERIFY(controller.centerGimbal());
        receiveRequest(second);
        QSignalSpy completed(&controller, &RoukePayloadController::gimbalCenterFinished);
        controller.setConfiguration(false, "127.0.0.1", second.localPort(), 0, 7);
        QCOMPARE(completed.count(), 1);
        QVERIFY(!completed.at(0).at(0).toBool());
        QVERIFY(!controller.gimbalCenterPending());
        QVERIFY(!controller.sendGimbalSpeed(35, 35));
    }

    void fragmentedUtf8AckWaitsForOneByteTail()
    {
        QUdpSocket box;
        QVERIFY(box.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        RoukePayloadController controller;
        controller.setConfiguration(true, "127.0.0.1", box.localPort(), 0, 7);
        QVERIFY(controller.takePhoto());
        receiveRequest(box);
        auto response = ack("lr1_take_photo", 1);
        const QString chinese = QString::fromUtf8("\xe6\xb5\x8b\xe8\xaf\x95\xe5\x9b\x9e\xe6\x89\xa7");
        response["msg"] = chinese;
        response["msg"] = chinese + QString(257 - rawFrame(response).size(), QLatin1Char('x'));
        const QByteArray frame = rawFrame(response);
        QCOMPARE(frame.size(), 257);
        const int utf8Split = frame.indexOf(chinese.toUtf8()) + 1;
        QVERIFY(utf8Split > 1 && utf8Split < 128);
        // End the first fragment inside a Chinese UTF-8 character.
        sendFragment(box, controller, frame.left(utf8Split));
        int offset = utf8Split;
        int sequence = 1;
        while (offset < frame.size() - 1) {
            const int count = qMin(128, frame.size() - 1 - offset);
            sendFragment(box, controller, frame.mid(offset, count), 7, 42, 77, sequence++);
            offset += count;
        }
        QTest::qWait(40);
        QVERIFY(controller.pending());
        QVERIFY(!controller.responding());
        sendFragment(box, controller, frame.right(1), 7, 42, 77, sequence);
        QTRY_VERIFY(!controller.pending());
        QCOMPARE(controller.lastError(), response.value("msg").toString());
        QCOMPARE(controller.photoCount(), 0);
        QVERIFY(controller.responding());
    }

    void oversizedJsonFailsBeforeAnyDatagramIsWritten()
    {
        QUdpSocket box;
        QVERIFY(box.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        RoukePayloadController controller;
        controller.setConfiguration(true, "127.0.0.1", box.localPort(), 0, 0);
        QVERIFY(!controller.sendCommand("stream_fixture", {{"padding", QString(65535, QLatin1Char('x'))}}));
        QVERIFY(!controller.pending());
        QVERIFY(!controller.lastError().isEmpty());
        QTest::qWait(30);
        QVERIFY(!box.hasPendingDatagrams());
    }

    void responseStreamsAreIsolated_data()
    {
        QTest::addColumn<int>("address");
        QTest::addColumn<int>("system");
        QTest::addColumn<int>("component");
        QTest::newRow("different-address") << 8 << 42 << 77;
        QTest::newRow("different-system") << 7 << 43 << 77;
        QTest::newRow("different-component") << 7 << 42 << 78;
    }

    void responseStreamsAreIsolated()
    {
        QFETCH(int, address);
        QFETCH(int, system);
        QFETCH(int, component);
        QUdpSocket box;
        QVERIFY(box.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        RoukePayloadController controller;
        controller.setConfiguration(true, "127.0.0.1", box.localPort(), 0, 7);
        QVERIFY(controller.takePhoto());
        receiveRequest(box);
        auto response = ack("lr1_take_photo");
        response["msg"] = QString(80, QLatin1Char('x'));
        const QByteArray frame = rawFrame(response);
        QVERIFY(frame.size() > 128 && frame.size() <= 256);
        sendFragment(box, controller, frame.left(128));
        sendFragment(box, controller, frame.mid(128), address, system, component, 1);
        QTest::qWait(40);
        QVERIFY(controller.pending());
        QCOMPARE(controller.photoCount(), 0);
        sendFragment(box, controller, frame.mid(128), 7, 42, 77, 1);
        QTRY_VERIFY(!controller.pending());
        QCOMPARE(controller.photoCount(), 1);
    }

    void lostAndMalformedFragmentsResynchronizeAtNextFrame()
    {
        QUdpSocket box;
        QVERIFY(box.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        RoukePayloadController controller;
        controller.setConfiguration(true, "127.0.0.1", box.localPort(), 0, 7);
        QVERIFY(controller.takePhoto());
        receiveRequest(box);
        auto response = ack("lr1_take_photo");
        response["msg"] = QString(220, QLatin1Char('x'));
        const QByteArray frame = rawFrame(response);
        QVERIFY(frame.size() > 256 && frame.size() <= 384);
        sendFragment(box, controller, frame.left(128));
        sendFragment(box, controller, frame.mid(256), 7, 42, 77, 2); // Lost middle.
        auto malformed = rawFrame(ack("lr1_take_photo"));
        malformed[malformed.size() - 1] = char(0);
        sendFragment(box, controller, malformed, 7, 42, 77, 3);
        QTest::qWait(40);
        QVERIFY(controller.pending());
        QVERIFY(!controller.responding());
        sendFragment(box, controller, frame.left(128), 7, 42, 77, 4);
        sendFragment(box, controller, frame.mid(128, 128), 7, 42, 77, 5);
        sendFragment(box, controller, frame.mid(256), 7, 42, 77, 6);
        QTRY_VERIFY(!controller.pending());
        QCOMPARE(controller.photoCount(), 1);
    }

    void partialResponseExpiresAndConfigurationChangeClearsIt()
    {
        QUdpSocket box;
        QVERIFY(box.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        RoukePayloadController controller;
        controller.setConfiguration(true, "127.0.0.1", box.localPort(), 0, 7);
        const QByteArray frame = rawFrame(ack("lr1_take_photo"));
        sendFragment(box, controller, frame.left(50));
        QTest::qWait(3650); // No command is pending, so its ACK timeout cannot mask expiry.
        sendFragment(box, controller, frame.mid(50), 7, 42, 77, 1);
        QTest::qWait(40);
        QVERIFY(!controller.responding());
        send(box, controller, ack("lr1_take_photo"));
        QTRY_VERIFY(controller.responding());
        QCOMPARE(controller.photoCount(), 0); // An unsolicited ACK is not a photo result.

        sendFragment(box, controller, frame.left(50));
        QTest::qWait(30);
        controller.setConfiguration(false, "127.0.0.1", box.localPort(), 0, 7);
        controller.setConfiguration(true, "127.0.0.1", box.localPort(), 0, 7);
        sendFragment(box, controller, frame.mid(50), 7, 42, 77, 1);
        QTest::qWait(40);
        QVERIFY(!controller.responding());
        send(box, controller, ack("lr1_take_photo"));
        QTRY_VERIFY(controller.responding());
        QCOMPARE(controller.photoCount(), 0);
    }

    void duplicateAckInOneDatagramCannotConfirmNewlyQueuedStop()
    {
        QUdpSocket box;
        QVERIFY(box.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        RoukePayloadController controller;
        controller.setConfiguration(true, "127.0.0.1", box.localPort(), 0, 7);
        QVERIFY(controller.setRecording(true));
        receiveRequest(box);
        QVERIFY(controller.setRecording(false));
        const QByteArray packet = messageBytes(ack("lr1_record"));
        const QByteArray datagram = packet + packet;
        QCOMPARE(box.writeDatagram(datagram, QHostAddress::LocalHost, controller.boundPort()), qint64(datagram.size()));
        QTRY_VERIFY(controller.recording());
        QTest::qWait(30);
        QVERIFY(controller.pending());
        QVERIFY(!controller.recordingStopQueued());
        QCOMPARE(receiveRequest(box).value("data").toObject().value("action").toInt(), 1);
        send(box, controller, ack("lr1_record"));
        QTRY_VERIFY(!controller.pending());
        QVERIFY(!controller.recording());
        QVERIFY(controller.recordingKnown());
    }

    void partialDuplicateAckCannotCarryOverIntoQueuedStop()
    {
        QUdpSocket box;
        QVERIFY(box.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        RoukePayloadController controller;
        controller.setConfiguration(true, "127.0.0.1", box.localPort(), 0, 7);
        QVERIFY(controller.setRecording(true));
        receiveRequest(box);
        QVERIFY(controller.setRecording(false));
        const QByteArray frame = rawFrame(ack("lr1_record"));
        const int prefixSize = 128 - frame.size();
        QVERIFY(prefixSize > 4 && prefixSize < frame.size());
        // One relay contains a complete START ACK and the beginning of its
        // duplicate. Processing START immediately sends the queued STOP.
        sendFragment(box, controller, frame + frame.left(prefixSize));
        QTRY_VERIFY(controller.recording());
        QCOMPARE(receiveRequest(box).value("data").toObject().value("action").toInt(), 1);
        sendFragment(box, controller, frame.mid(prefixSize), 7, 42, 77, 1);
        QTest::qWait(40);
        QVERIFY(controller.pending());
        QVERIFY(controller.recording());
        sendFragment(box, controller, frame, 7, 42, 77, 2);
        QTRY_VERIFY(!controller.pending());
        QVERIFY(!controller.recording());
        QVERIFY(controller.recordingKnown());
    }

    void noAddressCannotSendAndDiscoveryDoesNotAuthorize()
    {
        QUdpSocket box;
        QVERIFY(box.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        RoukePayloadController controller;
        controller.setConfiguration(true, "127.0.0.1", box.localPort(), 0, -1);
        QVERIFY(!controller.available());
        QVERIFY(!controller.takePhoto());
        QVERIFY(!box.hasPendingDatagrams());
        send(box, controller, ack("lr1_take_photo"), 7);
        QTRY_COMPARE(controller.discoveredPayloadAddress(), 7);
        QVERIFY(!controller.available());
        QVERIFY(!controller.responding());
        QCOMPARE(controller.photoCount(), 0);
    }

    void photoRequiresMatchingAddressRouteAndCommand()
    {
        QUdpSocket box, stranger;
        QVERIFY(box.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        QVERIFY(stranger.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        RoukePayloadController controller;
        controller.setConfiguration(true, "127.0.0.1", box.localPort(), 0, 7);
        QVERIFY(controller.available());
        QVERIFY(controller.takePhoto());
        QCOMPARE(receiveRequest(box).value("cmd").toString(), QString("lr1_take_photo"));
        QVERIFY(controller.pending());
        QCOMPARE(controller.photoCount(), 0);
        QVERIFY(!controller.autoFocus()); // Serialized until ACK or timeout.
        send(stranger, controller, ack("lr1_take_photo"));
        send(box, controller, ack("lr1_take_photo"), 8);
        auto wrongRoute = ack("lr1_take_photo");
        wrongRoute["source"] = "osdk";
        send(box, controller, wrongRoute);
        wrongRoute["source"] = "psdk";
        wrongRoute["target"] = "osdk";
        send(box, controller, wrongRoute);
        send(box, controller, ack("lr1_focus"));
        QTest::qWait(60);
        QVERIFY(controller.pending());
        QCOMPARE(controller.photoCount(), 0);
        send(box, controller, ack("lr1_take_photo"));
        QTRY_VERIFY(!controller.pending());
        QCOMPARE(controller.photoCount(), 1);
        QVERIFY(controller.responding());
        send(box, controller, ack("lr1_take_photo"));
        QTest::qWait(30);
        QCOMPARE(controller.photoCount(), 1); // Duplicate unsolicited ACK is inert.
    }

    void recordingOnlyBecomesKnownOnSuccessAndStopWorksFromUnknown()
    {
        QUdpSocket box;
        QVERIFY(box.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        RoukePayloadController controller;
        controller.setConfiguration(true, "127.0.0.1", box.localPort(), 0, 7);
        QVERIFY(controller.setRecording(true));
        QCOMPARE(receiveRequest(box).value("data").toObject().value("action").toInt(), 0);
        QVERIFY(!controller.recordingKnown());
        send(box, controller, ack("lr1_record", 1));
        QTRY_VERIFY(!controller.pending());
        QVERIFY(!controller.recordingKnown());
        QVERIFY(!controller.lastError().isEmpty());
        QVERIFY(controller.setRecording(false));
        QCOMPARE(receiveRequest(box).value("data").toObject().value("action").toInt(), 1);
        send(box, controller, ack("lr1_record"));
        QTRY_VERIFY(controller.recordingKnown());
        QVERIFY(!controller.recording());
        QVERIFY(controller.setRecording(true));
        receiveRequest(box);
        send(box, controller, ack("lr1_record"));
        QTRY_VERIFY(controller.recording());
        QVERIFY(controller.recordingKnown());
    }

    void timeoutClearsStateWithoutRetryAndLateAckIsIgnored()
    {
        QUdpSocket box;
        QVERIFY(box.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        RoukePayloadController controller;
        controller.setConfiguration(true, "127.0.0.1", box.localPort(), 0, 7);
        QVERIFY(controller.setRecording(true));
        receiveRequest(box);
        send(box, controller, ack("lr1_record"));
        QTRY_VERIFY(controller.recordingKnown());
        QVERIFY(controller.setRecording(false));
        receiveRequest(box);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.pending(), 4500);
        QVERIFY(!controller.recordingKnown());
        QVERIFY(!controller.lastError().isEmpty());
        QVERIFY(!box.hasPendingDatagrams());
        send(box, controller, ack("lr1_record"));
        QTest::qWait(30);
        QVERIFY(!controller.recordingKnown());
    }

    void explicitStopWaitsForStartAckAndIsClearedByEndpointChange()
    {
        QUdpSocket box;
        QVERIFY(box.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        RoukePayloadController controller;
        controller.setConfiguration(true, "127.0.0.1", box.localPort(), 0, 7);
        QVERIFY(controller.setRecording(true));
        receiveRequest(box);
        QVERIFY(controller.setRecording(false));
        QVERIFY(controller.recordingStopQueued());
        QVERIFY(!box.hasPendingDatagrams());
        send(box, controller, ack("lr1_record"));
        QTRY_VERIFY(controller.recording());
        QVERIFY(controller.pending());
        QVERIFY(!controller.recordingStopQueued());
        QCOMPARE(receiveRequest(box).value("data").toObject().value("action").toInt(), 1);
        send(box, controller, ack("lr1_record"));
        QTRY_VERIFY(!controller.pending());
        QVERIFY(controller.recordingKnown());
        QVERIFY(!controller.recording());
        QVERIFY(controller.setRecording(true));
        receiveRequest(box);
        QVERIFY(controller.setRecording(false));
        controller.setConfiguration(true, "127.0.0.1", box.localPort(), 0, 8);
        QVERIFY(!controller.pending());
        QVERIFY(!controller.recordingStopQueued());
        send(box, controller, ack("lr1_record"));
        QTest::qWait(30);
        QVERIFY(!box.hasPendingDatagrams());
    }

    void endpointChangeAndDisableRetirePendingAcknowledgements()
    {
        QUdpSocket first, second;
        QVERIFY(first.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        QVERIFY(second.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        RoukePayloadController controller;
        controller.setConfiguration(true, "127.0.0.1", first.localPort(), 0, 7);
        QVERIFY(controller.setView(5));
        receiveRequest(first);
        controller.setConfiguration(true, "127.0.0.1", second.localPort(), 0, 7);
        QVERIFY(!controller.pending());
        QVERIFY(!controller.viewKnown());
        send(first, controller, ack("lr1_switch_view"));
        QTest::qWait(30);
        QVERIFY(!controller.viewKnown());
        QVERIFY(controller.setView(4));
        const auto command = receiveRequest(second);
        QCOMPARE(command.value("data").toObject().value("view_type").toInt(), 4);
        send(second, controller, ack("lr1_switch_view"));
        QTRY_VERIFY(controller.viewKnown());
        QCOMPARE(controller.viewType(), 4);
        QVERIFY(!controller.setView(0));
        controller.setConfiguration(false, "127.0.0.1", second.localPort(), 0, 7);
        QVERIFY(!controller.available());
        QVERIFY(!controller.viewKnown());
        QVERIFY(!controller.centerGimbal());
    }

    void lateStartAckCannotConfirmQueuedStopAfterTimeout()
    {
        QUdpSocket box;
        QVERIFY(box.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        RoukePayloadController controller;
        controller.setConfiguration(true, "127.0.0.1", box.localPort(), 0, 7);
        QVERIFY(controller.setRecording(true));
        receiveRequest(box);
        QVERIFY(controller.setRecording(false));
        QVERIFY(controller.recordingStopQueued());
        QTRY_VERIFY_WITH_TIMEOUT(!controller.recordingStopQueued(), 4500);
        QVERIFY(controller.pending());
        QCOMPARE(receiveRequest(box).value("data").toObject().value("action").toInt(), 1);
        // This ACK could be the delayed START. It must never confirm STOP.
        send(box, controller, ack("lr1_record"));
        QTRY_VERIFY(!controller.pending());
        QVERIFY(!controller.recordingKnown());
        QVERIFY(!controller.lastError().isEmpty());
        QVERIFY(controller.setRecording(false));
        receiveRequest(box);
        send(box, controller, ack("lr1_record"));
        QTRY_VERIFY(!controller.pending());
        QVERIFY(!controller.recordingKnown()); // Conservative for this endpoint session.
    }

    void corruptTruncatedAndOversizedPacketsCannotCompleteCommand()
    {
        QUdpSocket box;
        QVERIFY(box.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        RoukePayloadController controller;
        controller.setConfiguration(true, "127.0.0.1", box.localPort(), 0, 7);
        QVERIFY(controller.autoFocus());
        receiveRequest(box);
        auto bytes = messageBytes(ack("lr1_focus"));
        bytes[bytes.size() - 1] = bytes.at(bytes.size() - 1) ^ 1;
        box.writeDatagram(bytes, QHostAddress::LocalHost, controller.boundPort());
        send(box, controller, ack("lr1_focus"), 7, 129);
        bytes = messageBytes(ack("lr1_focus"));
        box.writeDatagram(bytes.left(12), QHostAddress::LocalHost, controller.boundPort());
        box.writeDatagram(bytes.mid(12), QHostAddress::LocalHost, controller.boundPort());
        QTest::qWait(50);
        QVERIFY(controller.pending());
        QVERIFY(!controller.responding());
        send(box, controller, ack("lr1_focus"));
        QTRY_VERIFY(!controller.pending());
    }

    void bindFailureAndInvalidEndpointAreVisible()
    {
        QUdpSocket occupied;
        QVERIFY(occupied.bind(QHostAddress::AnyIPv4, quint16(0), QUdpSocket::DontShareAddress));
        RoukePayloadController controller;
        controller.setConfiguration(true, "127.0.0.1", 8888, occupied.localPort(), 7);
        QVERIFY(!controller.available());
        QVERIFY(!controller.lastError().isEmpty());
        controller.setConfiguration(true, "invalid", 8888, 0, 7);
        QVERIFY(!controller.available());
        QVERIFY(!controller.lastError().isEmpty());
    }
};

QTEST_GUILESS_MAIN(RoukePayloadControllerTest)
#include "RoukePayloadControllerTest.moc"
