/****************************************************************************
 *
 * 思翼云台缩放协议回归测试。
 *
 ****************************************************************************/

#include "SiyiProtocol.h"

#include <QtTest/QTest>

class SiyiProtocolTest : public QObject
{
    Q_OBJECT

private slots:
    void manualZoomPackets();
    void absoluteZoomPacket();
    void maximumZoomPayloads();
    void currentZoomPayloads();
    void invalidZoomPayloads();
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

QTEST_APPLESS_MAIN(SiyiProtocolTest)

#include "SiyiProtocolTest.moc"
