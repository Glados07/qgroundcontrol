/****************************************************************************
 *
 * 思翼云台缩放协议回归测试。
 *
 ****************************************************************************/

#include "A8MiniZoomPolicy.h"
#include "SiyiProtocol.h"

#include <QtTest/QTest>

class SiyiProtocolTest : public QObject
{
    Q_OBJECT

private slots:
    void manualZoomPackets();
    void absoluteZoomPacket();
    void strictPacketLength();
    void maximumZoomPayloads();
    void currentZoomPayloads();
    void invalidZoomPayloads();
    void pulledVideoResolutionLimits_data();
    void pulledVideoResolutionLimits();
    void strictStepTargets();
};

void SiyiProtocolTest::manualZoomPackets()
{
    QCOMPARE(SiyiProtocol::manualZoomPacket(1).toHex(), QByteArrayLiteral("5566010100000005018d64"));
    QCOMPARE(SiyiProtocol::manualZoomPacket(0).toHex(), QByteArrayLiteral("556601010000000500ac74"));
    QCOMPARE(SiyiProtocol::manualZoomPacket(-1).toHex(), QByteArrayLiteral("5566010100000005ff5c6a"));
    QVERIFY(SiyiProtocol::manualZoomPacket(2).isEmpty());
    QVERIFY(SiyiProtocol::manualZoomPacket(-2).isEmpty());
}

void SiyiProtocolTest::absoluteZoomPacket()
{
    QCOMPARE(SiyiProtocol::absoluteZoomPacket(1.0).toHex(), QByteArrayLiteral("556601020000000f010061be"));
    QCOMPARE(SiyiProtocol::absoluteZoomPacket(4.5).toHex(), QByteArrayLiteral("556601020000000f04053111"));
    QCOMPARE(SiyiProtocol::requestMaximumZoomPacket().toHex(), QByteArrayLiteral("5566010000000016b2a6"));
    QCOMPARE(SiyiProtocol::requestCurrentZoomPacket().toHex(), QByteArrayLiteral("55660100000000187c47"));
}

void SiyiProtocolTest::strictPacketLength()
{
    const QByteArray packet = SiyiProtocol::requestCurrentZoomPacket();
    QVERIFY(SiyiProtocol::decodePacket(packet).valid);

    QByteArray packetWithTrailingByte = packet;
    packetWithTrailingByte.append('\0');
    QVERIFY(!SiyiProtocol::decodePacket(packetWithTrailingByte).valid);

    QByteArray packetWithBadCrc = packet;
    packetWithBadCrc[packetWithBadCrc.size() - 1] =
        static_cast<char>(packetWithBadCrc.back() ^ 0x01);
    QVERIFY(!SiyiProtocol::decodePacket(packetWithBadCrc).valid);
}

void SiyiProtocolTest::maximumZoomPayloads()
{
    double zoomLevel = 0.0;
    bool usedLegacyEncoding = true;

    QVERIFY(!SiyiProtocol::parseMaximumZoomPayload(QByteArray::fromHex("0000"),
                                                    6.0,
                                                    &zoomLevel,
                                                    &usedLegacyEncoding));

    QVERIFY(SiyiProtocol::parseMaximumZoomPayload(QByteArray::fromHex("0100"),
                                                   6.0,
                                                   &zoomLevel,
                                                   &usedLegacyEncoding));
    QCOMPARE(zoomLevel, 1.0);
    QVERIFY(!usedLegacyEncoding);

    QVERIFY(SiyiProtocol::parseMaximumZoomPayload(QByteArray::fromHex("0305"),
                                                   6.0,
                                                   &zoomLevel,
                                                   &usedLegacyEncoding));
    QCOMPARE(zoomLevel, 3.5);
    QVERIFY(!usedLegacyEncoding);

    QVERIFY(SiyiProtocol::parseMaximumZoomPayload(QByteArray::fromHex("0505"),
                                                   6.0,
                                                   &zoomLevel,
                                                   &usedLegacyEncoding));
    QCOMPARE(zoomLevel, 5.5);
    QVERIFY(!usedLegacyEncoding);

    QVERIFY(SiyiProtocol::parseMaximumZoomPayload(QByteArray::fromHex("3700"),
                                                   6.0,
                                                   &zoomLevel,
                                                   &usedLegacyEncoding));
    QCOMPARE(zoomLevel, 5.5);
    QVERIFY(usedLegacyEncoding);

    QVERIFY(SiyiProtocol::parseMaximumZoomPayload(QByteArray::fromHex("0600"),
                                                   6.0,
                                                   &zoomLevel,
                                                   &usedLegacyEncoding));
    QCOMPARE(zoomLevel, 6.0);
    QVERIFY(!usedLegacyEncoding);
}

void SiyiProtocolTest::currentZoomPayloads()
{
    double zoomLevel = 0.0;
    bool usedLegacyEncoding = true;

    QVERIFY(SiyiProtocol::parseCurrentZoomPayload(QByteArray::fromHex("0100"),
                                                   1.0,
                                                   5.5,
                                                   &zoomLevel,
                                                   &usedLegacyEncoding));
    QCOMPARE(zoomLevel, 1.0);
    QVERIFY(!usedLegacyEncoding);

    QVERIFY(SiyiProtocol::parseCurrentZoomPayload(QByteArray::fromHex("0505"),
                                                   1.0,
                                                   5.5,
                                                   &zoomLevel,
                                                   &usedLegacyEncoding));
    QCOMPARE(zoomLevel, 5.5);
    QVERIFY(!usedLegacyEncoding);

    // 部分兼容固件可能复用 0x05 ACK 的小端十分之一格式。
    QVERIFY(SiyiProtocol::parseCurrentZoomPayload(QByteArray::fromHex("0a00"),
                                                   1.0,
                                                   5.5,
                                                   &zoomLevel,
                                                   &usedLegacyEncoding));
    QCOMPARE(zoomLevel, 1.0);
    QVERIFY(usedLegacyEncoding);

    QVERIFY(SiyiProtocol::parseCurrentZoomPayload(QByteArray::fromHex("3700"),
                                                   1.0,
                                                   5.5,
                                                   &zoomLevel,
                                                   &usedLegacyEncoding));
    QCOMPARE(zoomLevel, 5.5);
    QVERIFY(usedLegacyEncoding);

    QVERIFY(SiyiProtocol::parseCurrentZoomPayload(QByteArray::fromHex("3c00"),
                                                   1.0,
                                                   6.0,
                                                   &zoomLevel,
                                                   &usedLegacyEncoding));
    QCOMPARE(zoomLevel, 6.0);
    QVERIFY(usedLegacyEncoding);
}

void SiyiProtocolTest::invalidZoomPayloads()
{
    double zoomLevel = 3.2;
    bool usedLegacyEncoding = true;

    QVERIFY(!SiyiProtocol::parseCurrentZoomPayload(QByteArray::fromHex("ffff"),
                                                    1.0,
                                                    5.5,
                                                    &zoomLevel,
                                                    &usedLegacyEncoding));
    QCOMPARE(zoomLevel, 3.2);
    QVERIFY(!usedLegacyEncoding);

    QVERIFY(!SiyiProtocol::parseCurrentZoomPayload(QByteArray::fromHex("010a"),
                                                    1.0,
                                                    5.5,
                                                    &zoomLevel));
    QVERIFY(!SiyiProtocol::parseCurrentZoomPayload(QByteArray::fromHex("01"),
                                                    1.0,
                                                    5.5,
                                                    &zoomLevel));
    QVERIFY(!SiyiProtocol::parseCurrentZoomPayload(QByteArray::fromHex("010000"),
                                                    1.0,
                                                    5.5,
                                                    &zoomLevel));
    QVERIFY(!SiyiProtocol::parseManualZoomAckPayload(QByteArray::fromHex("0a0000"), &zoomLevel));
}

void SiyiProtocolTest::pulledVideoResolutionLimits_data()
{
    QTest::addColumn<int>("width");
    QTest::addColumn<int>("height");
    QTest::addColumn<double>("maximumZoom");

    QTest::newRow("uhd-4k") << 3840 << 2160 << 1.0;
    QTest::newRow("2k") << 2560 << 1440 << 3.5;
    QTest::newRow("1080p") << 1920 << 1080 << 5.5;
    QTest::newRow("720p") << 1280 << 720 << 6.0;
}

void SiyiProtocolTest::pulledVideoResolutionLimits()
{
    QFETCH(int, width);
    QFETCH(int, height);
    QFETCH(double, maximumZoom);

    double resolvedMaximumZoom = 0.0;
    QVERIFY(A8MiniZoomPolicy::maximumZoomForVideoResolution(
        static_cast<quint16>(width),
        static_cast<quint16>(height),
        &resolvedMaximumZoom));
    QCOMPARE(resolvedMaximumZoom, maximumZoom);

    QVERIFY(!A8MiniZoomPolicy::maximumZoomForVideoResolution(640,
                                                             480,
                                                             &resolvedMaximumZoom));
}

void SiyiProtocolTest::strictStepTargets()
{
    double targetZoom = 0.0;

    QVERIFY(A8MiniZoomPolicy::stepTarget(4.5, 1.0, 1.0, 5.5, 1, &targetZoom));
    QCOMPARE(targetZoom, 5.5);
    QVERIFY(A8MiniZoomPolicy::stepTarget(4.5, 1.0, 1.0, 5.5, -1, &targetZoom));
    QCOMPARE(targetZoom, 3.5);

    QVERIFY(!A8MiniZoomPolicy::stepTarget(5.0, 1.0, 1.0, 5.5, 1, &targetZoom));
    QVERIFY(!A8MiniZoomPolicy::stepTarget(1.5, 1.0, 1.0, 5.5, -1, &targetZoom));
    QVERIFY(!A8MiniZoomPolicy::stepTarget(1.0, 1.0, 1.0, 1.0, 1, &targetZoom));
}

QTEST_APPLESS_MAIN(SiyiProtocolTest)

#include "SiyiProtocolTest.moc"
