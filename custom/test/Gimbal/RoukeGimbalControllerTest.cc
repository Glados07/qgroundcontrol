#include "RoukeGimbalController.h"
#include "RoukePayloadController.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QLoggingCategory>
#include <QtNetwork/QNetworkDatagram>
#include <QtTest/QTest>
#include <cstring>
#include <memory>

namespace {
class GimbalTraceCapture {
public:
    GimbalTraceCapture()
    {
        current = this;
        previous = qInstallMessageHandler(handleMessage);
        QLoggingCategory::setFilterRules("gcs.custom.gimbal.rouke.channels.debug=true");
    }
    ~GimbalTraceCapture()
    {
        QLoggingCategory::setFilterRules({});
        qInstallMessageHandler(previous);
        current = nullptr;
    }
    QStringList messages;

private:
    static void handleMessage(QtMsgType type, const QMessageLogContext &context, const QString &message)
    {
        if (current && qstrcmp(context.category, "gcs.custom.gimbal.rouke.channels") == 0) {
            current->messages.append(message);
        } else if (current && current->previous) {
            current->previous(type, context, message);
        }
    }
    inline static GimbalTraceCapture *current = nullptr;
    QtMessageHandler previous = nullptr;
};

// Independently reconstruct the documented byte stream, so tests exercise the
// actual two-packet speed request without using the production frame decoder.
class PayloadPeer
{
public:
    QUdpSocket socket;
    QString error;

    QList<QJsonObject> readAvailable()
    {
        QList<QJsonObject> result;
        while (socket.hasPendingDatagrams()) {
            const QByteArray bytes = socket.receiveDatagram().data();
            mavlink_message_t rx{}, message{};
            mavlink_status_t parser{}, status{};
            int count = 0;
            for (const char byte : bytes) {
                if (mavlink_frame_char_buffer(&rx, &parser, static_cast<uint8_t>(byte),
                                             &message, &status) == MAVLINK_FRAMING_OK) ++count;
            }
            if (count != 1 || message.magic != MAVLINK_STX
                || message.msgid != MAVLINK_MSG_ID_VK_PAYLOAD_DATA_RELAY) {
                error = "Invalid MAVLink datagram";
                continue;
            }
            mavlink_vk_payload_data_relay_t relay{};
            mavlink_msg_vk_payload_data_relay_decode(&message, &relay);
            if (relay.payload_addr != 0 || relay.reserve != 0 || !relay.data_len || relay.data_len > 128) {
                error = "Invalid relay envelope";
                continue;
            }
            _stream.append(reinterpret_cast<const char *>(relay.data), relay.data_len);
            while (_stream.size() >= 4) {
                if (!_stream.startsWith(QByteArray::fromHex("f0e0"))) {
                    error = "Interleaved or invalid inner frames";
                    _stream.clear();
                    break;
                }
                const int length = static_cast<uint8_t>(_stream.at(2))
                    | (static_cast<uint8_t>(_stream.at(3)) << 8);
                if (_stream.size() < length + 7) break;
                if (_stream.mid(4 + length, 3) != QByteArray::fromHex("00f1e1")) {
                    error = "Invalid inner frame trailer";
                    _stream.clear();
                    break;
                }
                QJsonParseError jsonError;
                const auto document = QJsonDocument::fromJson(_stream.mid(4, length), &jsonError);
                if (jsonError.error != QJsonParseError::NoError || !document.isObject()) {
                    error = "Invalid JSON request";
                } else {
                    result.append(document.object());
                }
                _stream.remove(0, length + 7);
            }
        }
        return result;
    }

    QJsonObject nextRequest()
    {
        QElapsedTimer deadline;
        deadline.start();
        while (_queued.isEmpty() && deadline.elapsed() < 500) {
            _queued.append(readAvailable());
            if (_queued.isEmpty()) socket.waitForReadyRead(20);
        }
        return _queued.isEmpty() ? QJsonObject{} : _queued.takeFirst();
    }

    QList<QJsonObject> takeAll()
    {
        auto result = _queued;
        _queued.clear();
        result.append(readAvailable());
        return result;
    }

    void acknowledge(const RoukePayloadController &controller, const QString &command, int code = 0)
    {
        const QJsonObject object{{"source", "psdk"}, {"target", "msdk"}, {"type", "ack"},
                                 {"cmd", command}, {"msg", ""}, {"code", code}, {"data", QJsonObject{}}};
        const QByteArray json = QJsonDocument(object).toJson(QJsonDocument::Compact);
        QByteArray frame = QByteArray::fromHex("f0e0");
        frame.append(char(json.size() & 0xff));
        frame.append(char((json.size() >> 8) & 0xff));
        frame.append(json);
        frame.append(QByteArray::fromHex("00f1e1"));
        for (int offset = 0; offset < frame.size(); offset += 128) {
            const auto fragment = frame.mid(offset, 128);
            mavlink_vk_payload_data_relay_t relay{};
            relay.data_len = static_cast<uint8_t>(fragment.size());
            std::memcpy(relay.data, fragment.constData(), static_cast<size_t>(fragment.size()));
            mavlink_message_t message{};
            mavlink_msg_vk_payload_data_relay_encode_status(42, 77, &_tx, &message, &relay);
            uint8_t bytes[MAVLINK_MAX_PACKET_LEN]{};
            const auto size = mavlink_msg_to_send_buffer(bytes, &message);
            QCOMPARE(socket.writeDatagram(reinterpret_cast<const char *>(bytes), size,
                                          QHostAddress::LocalHost, controller.boundPort()), qint64(size));
        }
    }

private:
    QByteArray _stream;
    QList<QJsonObject> _queued;
    mavlink_status_t _tx{};
};

void verifySpeed(const QJsonObject &request, int yaw, int pitch)
{
    QCOMPARE(request.value("source").toString(), QString("msdk"));
    QCOMPARE(request.value("target").toString(), QString("psdk"));
    QCOMPARE(request.value("type").toString(), QString("req"));
    QCOMPARE(request.value("cmd").toString(), QString("gimbal_speed_ctrl"));
    QCOMPARE(request.value("data").toObject(),
             (QJsonObject{{"yaw_speed", yaw}, {"pitch_speed", pitch}, {"roll_speed", 0}}));
}
}

class RoukeGimbalControllerTest : public QObject
{
    Q_OBJECT
private slots:
    void init()
    {
        _peer.reset(new PayloadPeer);
        QVERIFY(_peer->socket.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
        _controller.reset(new RoukePayloadController);
        _controller->setConfiguration(true, "127.0.0.1", _peer->socket.localPort(), 0, 0);
        QVERIFY(_controller->available());
        _gimbal.reset(new RoukeGimbalController(_controller.get()));
    }

    void cleanup()
    {
        QVERIFY2(_peer->error.isEmpty(), qPrintable(_peer->error));
        _gimbal.reset();
        _controller.reset();
        _peer.reset();
    }

    void startupRequiresNeutralAndButtonRelease()
    {
        _gimbal->updateChannels(2000, 1000, 2000);
        QTest::qWait(100);
        QVERIFY(_peer->takeAll().isEmpty());
        _gimbal->updateChannels(1500, 1500, 2000);
        QVERIFY(_peer->takeAll().isEmpty()); // High at startup is not a button edge.
        _gimbal->updateChannels(1500, 1500, 1000);
        _gimbal->updateChannels(2000, 1000, 1000);
        verifySpeed(_peer->nextRequest(), 10, -5);
        QVERIFY(!_controller->gimbalCenterPending());
    }

    void tracePreservesGateTransitionsAndSamplesAnalogInput()
    {
        GimbalTraceCapture logs;
        for (int i = 0; i < 20; ++i) _gimbal->updateChannels(1900 + i, 1100 - i, 1900);
        QCOMPARE(logs.messages.filter("await-neutral").size(), 1);
        QVERIFY(_peer->takeAll().isEmpty());
        _gimbal->updateChannels(1500, 1500, 1000);
        QCOMPARE(logs.messages.filter("neutral-armed").size(), 1);
        for (int i = 0; i < 20; ++i) _gimbal->updateChannels(1700 + i, 1300 - i, 0);
        QCOMPARE(logs.messages.filter("gate motion").size(), 1);
        QVERIFY(logs.messages.join('\n').contains("centerButtonValid false"));
        verifySpeed(_peer->nextRequest(), 3, -1);
        _gimbal->updateChannels(1500, 1500, 1000);
        QCOMPARE(logs.messages.filter("ROUKE_RC_STOP").size(), 1);
        verifySpeed(_peer->nextRequest(), 0, 0);
        _gimbal->updateChannels(1500, 1500, 1900);
        QCOMPARE(logs.messages.filter("center-edge").size(), 1);
        QVERIFY(logs.messages.join('\n').contains("ROUKE_RC_CENTER submitted true"));
        QCOMPARE(_peer->nextRequest().value("cmd").toString(), QString("gimbal_center"));
        _peer->acknowledge(*_controller, "gimbal_center");
        QTRY_VERIFY(!_controller->gimbalCenterPending());
        _gimbal->updateChannels(1500, 1500, 1000);
        _gimbal->updateChannels(1900, 1100, 1000);
        QTest::qWait(450);
        QVERIFY(logs.messages.join('\n').contains("ROUKE_RC_CANCEL inputAgeMs"));
        QVERIFY(logs.messages.join('\n').contains("targetYaw 10 targetPitch -5")
                || logs.messages.join('\n').contains("targetYaw 9 targetPitch -4"));
    }

    void channelMapping_data()
    {
        QTest::addColumn<int>("yawChannel");
        QTest::addColumn<int>("pitchChannel");
        QTest::addColumn<bool>("reversed");
        QTest::addColumn<int>("yaw");
        QTest::addColumn<int>("pitch");
        QTest::newRow("maximum") << 1950 << 1950 << false << 10 << 5;
        QTest::newRow("minimum") << 1050 << 1050 << false << -10 << -5;
        QTest::newRow("reasonable-clamp") << 2100 << 900 << false << 10 << -5;
        QTest::newRow("deadband-inclusive") << 1400 << 1600 << false << 0 << 0;
        QTest::newRow("neutral") << 1500 << 1500 << false << 0 << 0;
        QTest::newRow("just-outside") << 1601 << 1399 << false << 1 << -1;
        QTest::newRow("half-speed") << 1775 << 1225 << false << 5 << -3;
        QTest::newRow("yaw-only") << 2000 << 1500 << false << 10 << 0;
        QTest::newRow("pitch-only") << 1500 << 1000 << false << 0 << -5;
        QTest::newRow("reversed") << 2000 << 1000 << true << -10 << 5;
    }

    void channelMapping()
    {
        QFETCH(int, yawChannel);
        QFETCH(int, pitchChannel);
        QFETCH(bool, reversed);
        QFETCH(int, yaw);
        QFETCH(int, pitch);
        _gimbal->setConfiguration(10, 5, reversed, reversed);
        _gimbal->updateChannels(1500, 1500, 1000);
        _gimbal->updateChannels(qint16(yawChannel), qint16(pitchChannel), 1000);
        if (yaw || pitch) verifySpeed(_peer->nextRequest(), yaw, pitch);
        else {
            QTest::qWait(70);
            QVERIFY(_peer->takeAll().isEmpty());
        }
    }

    void refreshesLatestSpeedWithoutAck()
    {
        _gimbal->updateChannels(1500, 1500, 1000);
        _gimbal->updateChannels(2000, 2000, 1000);
        verifySpeed(_peer->nextRequest(), 10, 5);
        _gimbal->updateChannels(1000, 1500, 1000);
        QTest::qWait(220);
        const auto requests = _peer->takeAll();
        QVERIFY(requests.size() >= 3);
        QVERIFY(requests.size() <= 6); // Approximately 20 Hz, no per-input backlog.
        for (const auto &request : requests) verifySpeed(request, -10, 0);
        QVERIFY(!_controller->pending());
    }

    void neutralUpdatesDoNotPostponeStopRetries()
    {
        _gimbal->updateChannels(1500, 1500, 1000);
        _gimbal->updateChannels(2000, 2000, 1000);
        verifySpeed(_peer->nextRequest(), 10, 5);
        for (int i = 0; i < 8; ++i) {
            _gimbal->updateChannels(1500, 1500, 1000);
            QTest::qWait(20);
        }
        const auto requests = _peer->takeAll();
        QCOMPARE(requests.size(), 3);
        for (const auto &request : requests) verifySpeed(request, 0, 0);
        QTest::qWait(80);
        QVERIFY(_peer->takeAll().isEmpty());
    }

    void staleInputStopsAndRequiresNeutralAgain()
    {
        _gimbal->updateChannels(1500, 1500, 1000);
        _gimbal->updateChannels(2000, 2000, 1000);
        verifySpeed(_peer->nextRequest(), 10, 5);
        QTest::qWait(510);
        int stops = 0;
        bool stopped = false;
        for (const auto &request : _peer->takeAll()) {
            const bool zero = request.value("data").toObject().value("yaw_speed").toInt() == 0;
            QVERIFY(!stopped || zero);
            if (zero) {
                stopped = true;
                ++stops;
                verifySpeed(request, 0, 0);
            }
        }
        QCOMPARE(stops, 3);
        _gimbal->updateChannels(2000, 2000, 1000);
        QTest::qWait(80);
        QVERIFY(_peer->takeAll().isEmpty());
        _gimbal->updateChannels(1500, 1500, 1000);
        _gimbal->updateChannels(1000, 1000, 1000);
        verifySpeed(_peer->nextRequest(), -10, -5);
    }

    void invalidInputStops_data()
    {
        QTest::addColumn<int>("channel");
        QTest::newRow("CH11") << 0;
        QTest::newRow("CH12") << 1;
    }

    void invalidInputStops()
    {
        QFETCH(int, channel);
        _gimbal->updateChannels(1500, 1500, 1000);
        _gimbal->updateChannels(2000, 1000, 1000);
        verifySpeed(_peer->nextRequest(), 10, -5);
        qint16 values[] = {2000, 1000, 1000};
        values[channel] = 899;
        _gimbal->updateChannels(values[0], values[1], values[2]);
        verifySpeed(_peer->nextRequest(), 0, 0);
        QTest::qWait(130);
        const auto stops = _peer->takeAll();
        QCOMPARE(stops.size(), 2);
        for (const auto &stop : stops) verifySpeed(stop, 0, 0);
        _gimbal->updateChannels(2000, 1000, 1000);
        QTest::qWait(60);
        QVERIFY(_peer->takeAll().isEmpty());
    }

    void invalidCenterChannelDoesNotBlockAxes_data()
    {
        QTest::addColumn<int>("button");
        QTest::newRow("unmapped-zero") << 0;
        QTest::newRow("below-range") << 899;
        QTest::newRow("above-range") << 2101;
        QTest::newRow("negative") << -1;
    }

    void invalidCenterChannelDoesNotBlockAxes()
    {
        QFETCH(int, button);
        _gimbal->updateChannels(2000, 1000, qint16(button));
        QTest::qWait(60);
        QVERIFY(_peer->takeAll().isEmpty()); // Axes still need fresh neutral.
        _gimbal->updateChannels(1500, 1500, qint16(button));
        _gimbal->updateChannels(2000, 1000, qint16(button));
        verifySpeed(_peer->nextRequest(), 10, -5);
        _gimbal->updateChannels(1000, 2000, qint16(button));
        QTest::qWait(60); // Refresh is timer-driven; keep the Qt event loop running.
        verifySpeed(_peer->nextRequest(), -10, 5);
        QVERIFY(!_controller->gimbalCenterPending());

        // The independent axis freshness watchdog remains effective.
        QTest::qWait(510);
        int stops = 0;
        for (const auto &request : _peer->takeAll()) {
            if (request.value("data").toObject().value("yaw_speed").toInt() == 0) {
                verifySpeed(request, 0, 0);
                ++stops;
            } else {
                verifySpeed(request, -10, 5);
            }
        }
        QCOMPARE(stops, 3);
    }

    void recoveredCenterButtonNeedsFreshRelease()
    {
        _gimbal->updateChannels(1500, 1500, 1000);
        _gimbal->updateChannels(2000, 1000, 1000);
        verifySpeed(_peer->nextRequest(), 10, -5);
        _gimbal->updateChannels(2000, 1000, 0);
        QTest::qWait(60);
        verifySpeed(_peer->nextRequest(), 10, -5);
        _gimbal->updateChannels(2000, 1000, 2000);
        QTest::qWait(60);
        verifySpeed(_peer->nextRequest(), 10, -5);
        QVERIFY(!_controller->gimbalCenterPending());
        _gimbal->updateChannels(2000, 1000, 1000);
        _gimbal->updateChannels(2000, 1000, 2000);
        verifySpeed(_peer->nextRequest(), 0, 0);
        QCOMPARE(_peer->nextRequest().value("cmd").toString(), QString("gimbal_center"));
        QVERIFY(_controller->gimbalCenterPending());
    }

    void centerEdgeStopsThenRequiresFreshNeutralAfterAck()
    {
        _gimbal->updateChannels(1500, 1500, 1000);
        _gimbal->updateChannels(2000, 1000, 1000);
        verifySpeed(_peer->nextRequest(), 10, -5);
        _gimbal->updateChannels(2000, 1000, 2000);
        verifySpeed(_peer->nextRequest(), 0, 0);
        QCOMPARE(_peer->nextRequest().value("cmd").toString(), QString("gimbal_center"));
        QVERIFY(_controller->gimbalCenterPending());
        _gimbal->updateChannels(1500, 1500, 2000);
        QTest::qWait(120);
        QVERIFY(_peer->takeAll().isEmpty()); // Holding CH14 must not repeat centering.
        _peer->acknowledge(*_controller, "gimbal_center");
        QTRY_VERIFY(!_controller->gimbalCenterPending());
        _gimbal->updateChannels(2000, 1000, 2000);
        QTest::qWait(60);
        QVERIFY(_peer->takeAll().isEmpty()); // Neutral received before ACK is insufficient.
        _gimbal->updateChannels(1500, 1500, 1000);
        _gimbal->updateChannels(2000, 1000, 1000);
        verifySpeed(_peer->nextRequest(), 10, -5);
        _gimbal->updateChannels(2000, 1000, 2000);
        verifySpeed(_peer->nextRequest(), 0, 0);
        QCOMPARE(_peer->nextRequest().value("cmd").toString(), QString("gimbal_center"));
    }

    void pendingPhotoDoesNotBlockStop()
    {
        _gimbal->updateChannels(1500, 1500, 1000);
        _gimbal->updateChannels(2000, 1000, 1000);
        verifySpeed(_peer->nextRequest(), 10, -5);
        QVERIFY(_controller->takePhoto());
        QCOMPARE(_peer->nextRequest().value("cmd").toString(), QString("lr1_take_photo"));
        QVERIFY(_controller->pending());
        _gimbal->updateChannels(1500, 1500, 1000);
        verifySpeed(_peer->nextRequest(), 0, 0);
        QCOMPARE(_controller->pendingCommand(), QString("lr1_take_photo"));
        _peer->acknowledge(*_controller, "lr1_take_photo");
        QTRY_VERIFY(!_controller->pending());
        QCOMPARE(_controller->photoCount(), 1);
    }

    void configurationChangeStopsAndRearmsWithNewDirection()
    {
        _gimbal->updateChannels(1500, 1500, 1000);
        _gimbal->updateChannels(2000, 1000, 1000);
        verifySpeed(_peer->nextRequest(), 10, -5);
        _gimbal->setConfiguration(20, 8, true, true);
        verifySpeed(_peer->nextRequest(), 0, 0);
        QTest::qWait(130);
        for (const auto &request : _peer->takeAll()) verifySpeed(request, 0, 0);
        _gimbal->updateChannels(2000, 1000, 1000);
        QTest::qWait(60);
        QVERIFY(_peer->takeAll().isEmpty());
        _gimbal->updateChannels(1500, 1500, 1000);
        _gimbal->updateChannels(2000, 1000, 1000);
        verifySpeed(_peer->nextRequest(), -20, 8);
    }

private:
    std::unique_ptr<PayloadPeer> _peer;
    std::unique_ptr<RoukePayloadController> _controller;
    std::unique_ptr<RoukeGimbalController> _gimbal;
};

QTEST_GUILESS_MAIN(RoukeGimbalControllerTest)
#include "RoukeGimbalControllerTest.moc"
