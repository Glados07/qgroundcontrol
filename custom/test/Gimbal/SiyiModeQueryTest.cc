#include "SiyiSdk.h"
#include "SiyiProtocol.h"
#include <QtTest/QTest>
#include <QtTest/QSignalSpy>
#include <QtNetwork/QUdpSocket>
#include <QtNetwork/QNetworkDatagram>

namespace {
QByteArray reply(quint16 sequence, quint8 mode, quint8 control = 2)
{
    QByteArray bytes = QByteArray::fromHex("556602070000000a00000000000100");
    bytes[2] = char(control);
    bytes[5] = char(sequence & 0xff);
    bytes[6] = char(sequence >> 8);
    bytes[12] = char(mode);
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
}

class SiyiModeQueryTest : public QObject {
    Q_OBJECT
private slots:
    void normalCameraPacketsAreUnchanged()
    {
        const auto ordinary = SiyiProtocol::decodePacket(SiyiProtocol::requestCameraSystemStatusPacket());
        QVERIFY(ordinary.valid);
        QCOMPARE(ordinary.sequence, quint16(0));
        QCOMPARE(ordinary.command, quint8(0x0a));
        QVERIFY(ordinary.payload.isEmpty());
    }

    void onlyCurrentQueryPortFromConfiguredEndpointIsAccepted()
    {
        QUdpSocket server, wrongPort;
        QVERIFY(server.bind(QHostAddress(QHostAddress::LocalHost), 0));
        QVERIFY(wrongPort.bind(QHostAddress(QHostAddress::LocalHost), 0));
        SiyiSdk sdk;
        sdk.setEndpoint("127.0.0.1", server.localPort());
        QSignalSpy spy(&sdk, &SiyiSdk::gimbalModeReceived);
        QVERIFY(sdk.requestCameraSystemStatus());
        QTRY_VERIFY(server.hasPendingDatagrams());
        const auto ordinary = server.receiveDatagram();
        QVERIFY(sdk.requestGimbalMode(42));
        QTRY_VERIFY(server.hasPendingDatagrams());
        const auto request = server.receiveDatagram();
        const auto sequence = SiyiProtocol::decodePacket(request.data()).sequence;
        QCOMPARE(sequence, quint16(0));
        QVERIFY(ordinary.senderPort() != request.senderPort());
        QCOMPARE(ordinary.data(), request.data());
        const auto send = [&](QUdpSocket &socket, QByteArray data) {
            socket.writeDatagram(data, request.senderAddress(), request.senderPort());
        };
        server.writeDatagram(reply(0, 1), ordinary.senderAddress(), ordinary.senderPort());
        send(server, reply(sequence, 1, 1)); // request, not ACK
        send(wrongPort, reply(sequence, 1));
        QByteArray corrupt = reply(sequence, 1);
        corrupt[12] = char(0);
        send(server, corrupt);
        QTest::qWait(80);
        QCOMPARE(spy.count(), 0);
        // Firmware may count its own frames rather than echo the request.
        send(server, reply(77, 0));
        QTRY_COMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toULongLong(), quint64(42));
        QCOMPARE(spy.at(0).at(1).toUInt(), uint(0));
        send(server, reply(sequence, 1)); // duplicate must not refresh mode
        QTest::qWait(50);
        QCOMPARE(spy.count(), 1);
    }

    void cancellationReconnectAndExpiryRejectOldReplies()
    {
        QUdpSocket server;
        QVERIFY(server.bind(QHostAddress(QHostAddress::LocalHost), 0));
        SiyiSdk sdk;
        sdk.setEndpoint("127.0.0.1", server.localPort());
        QSignalSpy spy(&sdk, &SiyiSdk::gimbalModeReceived);
        QVERIFY(sdk.requestGimbalMode(1));
        QTRY_VERIFY(server.hasPendingDatagrams());
        const auto old = server.receiveDatagram();
        const auto oldSeq = SiyiProtocol::decodePacket(old.data()).sequence;
        sdk.cancelGimbalModeRequest();
        server.writeDatagram(reply(oldSeq, 0), old.senderAddress(), old.senderPort());
        QTest::qWait(50);
        QCOMPARE(spy.count(), 0);
        QVERIFY(sdk.requestGimbalMode(2));
        QTRY_VERIFY(server.hasPendingDatagrams());
        const auto current = server.receiveDatagram();
        const auto seq = SiyiProtocol::decodePacket(current.data()).sequence;
        QVERIFY(current.senderPort() != old.senderPort());
        server.writeDatagram(reply(oldSeq, 0), old.senderAddress(), old.senderPort());
        QTest::qWait(50);
        QCOMPARE(spy.count(), 0);
        QTest::qWait(1550);
        server.writeDatagram(reply(seq, 0), current.senderAddress(), current.senderPort());
        QTest::qWait(50);
        QCOMPARE(spy.count(), 0);
        QVERIFY(sdk.requestGimbalMode(3));
        QTRY_VERIFY(server.hasPendingDatagrams());
        const auto endpointRequest = server.receiveDatagram();
        const auto endpointSeq = SiyiProtocol::decodePacket(endpointRequest.data()).sequence;
        sdk.setEndpoint("127.0.0.1", server.localPort() == 65535 ? 65534 : server.localPort() + 1);
        server.writeDatagram(reply(endpointSeq, 0), endpointRequest.senderAddress(), endpointRequest.senderPort());
        QTest::qWait(50);
        QCOMPARE(spy.count(), 0);
    }

    void zeroSequenceModeReplyDoesNotChangeCameraPolling()
    {
        QUdpSocket server;
        QVERIFY(server.bind(QHostAddress(QHostAddress::LocalHost), 0));
        SiyiSdk sdk;
        sdk.setEndpoint("127.0.0.1", server.localPort());
        QSignalSpy modeSpy(&sdk, &SiyiSdk::gimbalModeReceived);
        QSignalSpy cameraSpy(&sdk, &SiyiSdk::cameraSystemStatusReceived);
        QVERIFY(sdk.requestGimbalMode(9));
        QTRY_VERIFY(server.hasPendingDatagrams());
        const auto modeRequest = server.receiveDatagram();
        server.writeDatagram(reply(0, 1), modeRequest.senderAddress(), modeRequest.senderPort());
        QTRY_COMPARE(modeSpy.count(), 1);
        QCOMPARE(modeSpy.at(0).at(0).toULongLong(), quint64(9));
        QCOMPARE(modeSpy.at(0).at(1).toUInt(), uint(1));
        QCOMPARE(cameraSpy.count(), 0);
        QVERIFY(sdk.requestCameraSystemStatus());
        QTRY_VERIFY(server.hasPendingDatagrams());
        const auto cameraRequest = server.receiveDatagram();
        server.writeDatagram(reply(0, 0), cameraRequest.senderAddress(), cameraRequest.senderPort());
        QTRY_COMPARE(cameraSpy.count(), 1);
        QCOMPARE(modeSpy.count(), 1);
    }
};
QTEST_GUILESS_MAIN(SiyiModeQueryTest)
#include "SiyiModeQueryTest.moc"
