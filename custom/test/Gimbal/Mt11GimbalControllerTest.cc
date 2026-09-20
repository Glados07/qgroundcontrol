/****************************************************************************
 *
 * MT11 RC rotation regression tests using the real SDK over loopback UDP.
 *
 ****************************************************************************/

#include "Mt11GimbalController.h"
#include "Mt11Protocol.h"
#include "Mt11Sdk.h"

#include <QtNetwork/QNetworkDatagram>
#include <QtNetwork/QUdpSocket>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>

namespace {

QByteArray ack(const QByteArray& payload, quint8 control = 2)
{
    QByteArray bytes = QByteArray::fromHex("5566020100000007");
    bytes[2] = char(control);
    bytes[3] = char(payload.size());
    bytes.append(payload);
    quint16 crc = 0;
    for (const char byte : bytes) {
        crc ^= quint16(quint8(byte)) << 8;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
        }
    }
    bytes.append(char(crc & 0xff));
    bytes.append(char(crc >> 8));
    return bytes;
}

} // namespace

class Mt11GimbalControllerTest : public QObject
{
    Q_OBJECT

private:
    QUdpSocket _server;
    Mt11Sdk* _sdk = nullptr;
    Mt11GimbalController* _controller = nullptr;

    void expectCommand(int yaw, int pitch)
    {
        QTRY_VERIFY_WITH_TIMEOUT(_server.hasPendingDatagrams(), 1000);
        const auto packet = Mt11Protocol::decodePacket(_server.receiveDatagram().data());
        QVERIFY(packet.valid);
        QCOMPARE(packet.control, quint8(1));
        QCOMPARE(packet.command, quint8(0x07));
        QCOMPARE(packet.payload.size(), 2);
        QCOMPARE(int(qint8(packet.payload.at(0))), yaw);
        QCOMPARE(int(qint8(packet.payload.at(1))), pitch);
    }

    void arm()
    {
        _controller->setAvailable(true);
        _controller->updateChannels(1500, 1500);
        expectCommand(0, 0);
    }

private slots:
    void init()
    {
        QVERIFY(_server.bind(QHostAddress(QHostAddress::LocalHost), 0));
        _sdk = new Mt11Sdk;
        _sdk->setEndpoint(QStringLiteral("127.0.0.1"), _server.localPort());
        _controller = new Mt11GimbalController(_sdk);
    }

    void cleanup()
    {
        delete _controller;
        delete _sdk;
        _server.close();
    }

    void channelMapping_data()
    {
        QTest::addColumn<int>("ch11");
        QTest::addColumn<int>("ch12");
        QTest::addColumn<int>("yaw");
        QTest::addColumn<int>("pitch");
        QTest::newRow("yaw-right") << 1950 << 1500 << 100 << 0;
        QTest::newRow("yaw-left") << 1050 << 1500 << -100 << 0;
        QTest::newRow("pitch-up") << 1500 << 1950 << 0 << 100;
        QTest::newRow("pitch-down") << 1500 << 1050 << 0 << -100;
        QTest::newRow("simultaneous") << 1950 << 1050 << 100 << -100;
        QTest::newRow("half-deflection") << 1725 << 1275 << 47 << -47;
        QTest::newRow("just-outside-deadband") << 1526 << 1474 << 1 << -1;
        QTest::newRow("saturated") << 2100 << 900 << 100 << -100;
        QTest::newRow("neutral") << 1500 << 1500 << 0 << 0;
        QTest::newRow("deadband-inclusive") << 1475 << 1525 << 0 << 0;
    }

    void channelMapping()
    {
        QFETCH(int, ch11);
        QFETCH(int, ch12);
        QFETCH(int, yaw);
        QFETCH(int, pitch);
        arm();
        _controller->updateChannels(1950, 1950);
        expectCommand(100, 100);
        _controller->updateChannels(qint16(ch11), qint16(ch12));
        expectCommand(yaw, pitch);
    }

    void startupAndRecoveryRequireBothAxesNeutral()
    {
        _controller->updateChannels(1950, 1050);
        QVERIFY(!_server.hasPendingDatagrams());
        _controller->setAvailable(true);
        _controller->updateChannels(1950, 1500);
        _controller->updateChannels(1500, 1050);
        QVERIFY(!_server.hasPendingDatagrams());
        arm();
        _controller->updateChannels(1050, 1950);
        expectCommand(-100, 100);
        _controller->setAvailable(false);
        for (int i = 0; i < 3; ++i) expectCommand(0, 0);
        _controller->setAvailable(true);
        _controller->updateChannels(1050, 1950);
        QTest::qWait(120);
        QVERIFY(!_server.hasPendingDatagrams());
        arm();
        _controller->updateChannels(1050, 1950);
        expectCommand(-100, 100);
    }

    void unchangedInputRefreshesAndEachAxisStopsIndependently()
    {
        arm();
        for (int i = 0; i < 8; ++i) {
            _controller->updateChannels(1950, 1050);
            expectCommand(100, -100);
            QTest::qWait(50);
        }
        _controller->updateChannels(1500, 1050);
        expectCommand(0, -100);
        _controller->updateChannels(1500, 1500);
        for (int i = 0; i < 3; ++i) expectCommand(0, 0);
        _controller->updateChannels(1500, 1500);
        QTest::qWait(150);
        QVERIFY(!_server.hasPendingDatagrams());
    }

    void inputTimeoutStopsAndDisarms()
    {
        arm();
        _controller->updateChannels(1950, 1050);
        expectCommand(100, -100);
        // No further samples: the controller must stop, never replay motion.
        for (int i = 0; i < 3; ++i) expectCommand(0, 0);
        _controller->updateChannels(1950, 1050);
        QTest::qWait(120);
        QVERIFY(!_server.hasPendingDatagrams());
        arm();
        _controller->updateChannels(1950, 1500);
        expectCommand(100, 0);
    }

    void invalidChannelsStopBothAxes_data()
    {
        QTest::addColumn<int>("ch11");
        QTest::addColumn<int>("ch12");
        QTest::newRow("missing-yaw") << 0 << 1500;
        QTest::newRow("negative-pitch") << 1500 << -1;
        QTest::newRow("yaw-too-low") << 899 << 1500;
        QTest::newRow("pitch-too-high") << 1500 << 2101;
    }

    void invalidChannelsStopBothAxes()
    {
        QFETCH(int, ch11);
        QFETCH(int, ch12);
        arm();
        _controller->updateChannels(1950, 1950);
        expectCommand(100, 100);
        _controller->updateChannels(qint16(ch11), qint16(ch12));
        for (int i = 0; i < 3; ++i) expectCommand(0, 0);
        _controller->updateChannels(1950, 1950);
        QTest::qWait(120);
        QVERIFY(!_server.hasPendingDatagrams());
    }

    void freshMotionCancelsDelayedStop()
    {
        arm();
        _controller->updateChannels(1950, 1500);
        expectCommand(100, 0);
        _controller->updateChannels(1500, 1500);
        expectCommand(0, 0);
        _controller->updateChannels(1050, 1500);
        expectCommand(-100, 0);
        QTest::qWait(220);
        QVERIFY(!_server.hasPendingDatagrams());
    }

    void endpointChangeFlushesOldStops()
    {
        arm();
        _controller->updateChannels(1950, 1500);
        expectCommand(100, 0);
        _controller->cancel();
        expectCommand(0, 0);
        _controller->setAvailable(false);
        expectCommand(0, 0);
        expectCommand(0, 0);
        QUdpSocket nextServer;
        QVERIFY(nextServer.bind(QHostAddress(QHostAddress::LocalHost), 0));
        _sdk->setEndpoint(QStringLiteral("127.0.0.1"), nextServer.localPort());
        _controller->setAvailable(true);
        _controller->updateChannels(1950, 1500);
        QTest::qWait(220);
        QVERIFY(!_server.hasPendingDatagrams());
        QVERIFY(!nextServer.hasPendingDatagrams());
    }

    void cancelAndDestructionStopWithoutEventLoop()
    {
        arm();
        _controller->updateChannels(1950, 1500);
        expectCommand(100, 0);
        _controller->cancel();
        expectCommand(0, 0);
        delete _controller;
        _controller = nullptr;
        expectCommand(0, 0);
        expectCommand(0, 0);
    }

    void feedbackValidationAndRejection()
    {
        QSignalSpy feedback(_sdk, &Mt11Sdk::gimbalRotationFeedbackReceived);
        QSignalSpy received(_sdk, &Mt11Sdk::packetReceived);
        arm();
        _controller->updateChannels(1950, 1500);
        QTRY_VERIFY(_server.hasPendingDatagrams());
        const auto request = _server.receiveDatagram();
        const auto send = [&](QUdpSocket& sender, const QByteArray& reply) {
            sender.writeDatagram(reply, request.senderAddress(), request.senderPort());
        };
        QUdpSocket wrongPort;
        send(wrongPort, ack(QByteArray::fromHex("00")));
        send(_server, ack(QByteArray::fromHex("00"), 0));
        send(_server, ack(QByteArray::fromHex("02")));
        send(_server, ack(QByteArray::fromHex("0100")));
        QTest::qWait(30);
        QCOMPARE(feedback.count(), 0);
        QCOMPARE(received.count(), 0);
        send(_server, ack(QByteArray::fromHex("01")));
        QTRY_COMPARE(feedback.count(), 1);
        QCOMPARE(feedback.at(0).at(0).toBool(), true);
        QCOMPARE(received.count(), 1);
        _controller->updateChannels(1950, 1500);
        expectCommand(100, 0);
        send(_server, ack(QByteArray::fromHex("00")));
        QTRY_COMPARE(feedback.count(), 2);
        QCOMPARE(feedback.at(1).at(0).toBool(), false);
        for (int i = 0; i < 3; ++i) expectCommand(0, 0);
        _controller->updateChannels(1950, 1500);
        QTest::qWait(120);
        QVERIFY(!_server.hasPendingDatagrams());
    }
};

QTEST_GUILESS_MAIN(Mt11GimbalControllerTest)
#include "Mt11GimbalControllerTest.moc"
