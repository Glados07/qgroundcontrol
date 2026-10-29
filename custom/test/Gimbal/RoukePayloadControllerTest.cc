#include "RoukePayloadController.h"
#include "RoukeProtocol.h"
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtNetwork/QNetworkDatagram>
#include <QtTest/QTest>
#include <QtTest/QSignalSpy>
#include <cstring>

namespace {
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
                         int component = 77, int sequence = 0, int lengthOverride = -1)
{
    Q_ASSERT(fragment.size() <= 128);
    mavlink_vk_payload_data_relay_t relay{};
    relay.payload_addr = static_cast<uint16_t>(address);
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

QByteArray messageBytes(const QJsonObject &object, int address = 7, int lengthOverride = -1)
{
    const auto frame = rawFrame(object);
    Q_ASSERT(frame.size() <= 128); // Long frames must use explicit fragment fixtures.
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
}

class RoukePayloadControllerTest : public QObject
{
    Q_OBJECT
private slots:
    void commandFragments_data()
    {
        QTest::addColumn<int>("frameLength");
        QTest::newRow("one-full-relay") << 128;
        QTest::newRow("one-byte-continuation") << 129;
        QTest::newRow("documented-gimbal-speed-128-plus-24") << 152;
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
        const QString command = frameLength == 152 ? QStringLiteral("gimbal_speed_ctrl")
                                                   : QStringLiteral("stream_fixture");
        QJsonObject data = frameLength == 152
            ? QJsonObject{{"yaw_speed", 10}, {"pitch_speed", 5}, {"roll_speed", 0}}
            : QJsonObject{{"padding", QString()}};
        QJsonObject request{{"source", "msdk"}, {"target", "psdk"}, {"type", "req"},
                            {"cmd", command}, {"code", 0}, {"msg", ""}, {"data", data}};
        if (frameLength != 152) {
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

    void speedAndStopBypassCameraPendingWithoutInterleaving()
    {
        QUdpSocket box;
        QVERIFY(box.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        RoukePayloadController controller;
        controller.setConfiguration(true, "127.0.0.1", box.localPort(), 0, 7);
        QVERIFY(controller.takePhoto());
        QVERIFY(controller.sendGimbalSpeed(10, -5));
        QVERIFY(controller.sendGimbalSpeed(0, 0));
        QCOMPARE(receiveStreamRequest(box).value("cmd").toString(), QString("lr1_take_photo"));
        auto speed = receiveStreamRequest(box);
        QCOMPARE(speed.value("cmd").toString(), QString("gimbal_speed_ctrl"));
        QCOMPARE(speed.value("data").toObject(), QJsonObject({{"yaw_speed", 10}, {"pitch_speed", -5}, {"roll_speed", 0}}));
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
        QVERIFY(controller.sendGimbalSpeed(10, 5));
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
        QVERIFY(controller.sendGimbalSpeed(10, 5));
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

    void endpointChangeStopsOldPeerAndCancelsCenter()
    {
        QUdpSocket first, second;
        QVERIFY(first.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        QVERIFY(second.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        RoukePayloadController controller;
        controller.setConfiguration(true, "127.0.0.1", first.localPort(), 0, 7);
        QVERIFY(controller.sendGimbalSpeed(10, -5));
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
        QVERIFY(!controller.sendGimbalSpeed(1, 1));
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
