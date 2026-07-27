/****************************************************************************
 *
 * 思翼云台缩放协议与固定步长策略回归测试。
 *
 ****************************************************************************/

#include "A8MiniZoomPolicy.h"
#include "SiyiProtocol.h"

#include <QtTest/QTest>

#include <limits>

class SiyiProtocolTest : public QObject
{
    Q_OBJECT

private slots:
    void manualZoomPackets();
    void absoluteZoomPackets();
    void strictPacketFrames();
    void manualZoomAckPayloads();
    void absoluteZoomAckPayloads();
    void maximumZoomPayloads();
    void currentZoomPayloads();
    void invalidZoomPayloads();
    void pulledVideoResolutionLimits_data();
    void pulledVideoResolutionLimits();
    void alignedMaximumZooms();
    void strictStepTargets();
    void alignmentTargets();
    void repeatedStepSequences();
    void targetTrackerRecovery();
};

void SiyiProtocolTest::manualZoomPackets()
{
    QCOMPARE(SiyiProtocol::manualZoomPacket(1).toHex(), QByteArrayLiteral("5566010100000005018d64"));
    QCOMPARE(SiyiProtocol::manualZoomPacket(0).toHex(), QByteArrayLiteral("556601010000000500ac74"));
    QCOMPARE(SiyiProtocol::manualZoomPacket(-1).toHex(), QByteArrayLiteral("5566010100000005ff5c6a"));
    QVERIFY(SiyiProtocol::manualZoomPacket(2).isEmpty());
    QVERIFY(SiyiProtocol::manualZoomPacket(-2).isEmpty());
}

void SiyiProtocolTest::absoluteZoomPackets()
{
    QCOMPARE(SiyiProtocol::absoluteZoomPacket(1.0).toHex(), QByteArrayLiteral("556601020000000f010061be"));
    QCOMPARE(SiyiProtocol::absoluteZoomPacket(1.8).toHex(), QByteArrayLiteral("556601020000000f0108693f"));
    QCOMPARE(SiyiProtocol::absoluteZoomPacket(2.0).toHex(), QByteArrayLiteral("556601020000000f020032eb"));
    QCOMPARE(SiyiProtocol::absoluteZoomPacket(4.5).toHex(), QByteArrayLiteral("556601020000000f04053111"));
    QCOMPARE(SiyiProtocol::absoluteZoomPacket(4.999999).toHex(), QByteArrayLiteral("556601020000000f0500a572"));
    QCOMPARE(SiyiProtocol::absoluteZoomPacket(5.5).toHex(), QByteArrayLiteral("556601020000000f05050022"));
    QCOMPARE(SiyiProtocol::absoluteZoomPacket(30.0).toHex(), QByteArrayLiteral("556601020000000f1e002cad"));
    QVERIFY(SiyiProtocol::absoluteZoomPacket(0.9).isEmpty());
    QVERIFY(SiyiProtocol::absoluteZoomPacket(30.1).isEmpty());
    QVERIFY(SiyiProtocol::absoluteZoomPacket(std::numeric_limits<double>::quiet_NaN()).isEmpty());
    QVERIFY(SiyiProtocol::absoluteZoomPacket(std::numeric_limits<double>::infinity()).isEmpty());

    QCOMPARE(SiyiProtocol::requestMaximumZoomPacket().toHex(), QByteArrayLiteral("5566010000000016b2a6"));
    QCOMPARE(SiyiProtocol::requestCurrentZoomPacket().toHex(), QByteArrayLiteral("55660100000000187c47"));
}

void SiyiProtocolTest::strictPacketFrames()
{
    const QByteArray request = SiyiProtocol::requestCurrentZoomPacket();
    const SiyiProtocol::DecodedPacket decodedRequest = SiyiProtocol::decodePacket(request);
    QVERIFY(decodedRequest.valid);
    QCOMPARE(decodedRequest.control, quint8(0x01));
    QCOMPARE(decodedRequest.sequence, quint16(0));
    QCOMPARE(decodedRequest.command, quint8(SiyiProtocol::CommandCurrentZoomValue));
    QVERIFY(decodedRequest.payload.isEmpty());
    QVERIFY(!SiyiProtocol::isAckPacket(decodedRequest));

    const QByteArray ack = QByteArray::fromHex("556602010000000f01c453");
    const SiyiProtocol::DecodedPacket decodedAck = SiyiProtocol::decodePacket(ack);
    QVERIFY(decodedAck.valid);
    QCOMPARE(decodedAck.control, quint8(0x02));
    QCOMPARE(decodedAck.command, quint8(SiyiProtocol::CommandAbsoluteZoom));
    QCOMPARE(decodedAck.payload, QByteArray::fromHex("01"));
    QVERIFY(SiyiProtocol::isAckPacket(decodedAck));

    SiyiProtocol::DecodedPacket invalidAckControl = decodedAck;
    invalidAckControl.control = 0x03;
    QVERIFY(!SiyiProtocol::isAckPacket(invalidAckControl));
    invalidAckControl.valid = false;
    invalidAckControl.control = 0x02;
    QVERIFY(!SiyiProtocol::isAckPacket(invalidAckControl));

    QByteArray packetWithTrailingByte = request;
    packetWithTrailingByte.append('\0');
    QVERIFY(!SiyiProtocol::decodePacket(packetWithTrailingByte).valid);

    QByteArray packetWithBadCrc = request;
    packetWithBadCrc[packetWithBadCrc.size() - 1] =
        static_cast<char>(packetWithBadCrc.back() ^ 0x01);
    QVERIFY(!SiyiProtocol::decodePacket(packetWithBadCrc).valid);

    QByteArray packetWithReservedControl = ack;
    packetWithReservedControl[2] = static_cast<char>(0x04);
    QVERIFY(!SiyiProtocol::decodePacket(packetWithReservedControl).valid);
    QVERIFY(!SiyiProtocol::decodePacket(QByteArray::fromHex("00660100000000187c47")).valid);
    QVERIFY(!SiyiProtocol::decodePacket(QByteArray::fromHex("5566010000000018")).valid);
}

void SiyiProtocolTest::manualZoomAckPayloads()
{
    double zoomLevel = 9.9;

    QVERIFY(SiyiProtocol::parseManualZoomAckPayload(QByteArray::fromHex("0a00"), &zoomLevel));
    QCOMPARE(zoomLevel, 1.0);
    QVERIFY(SiyiProtocol::parseManualZoomAckPayload(QByteArray::fromHex("1200"), &zoomLevel));
    QCOMPARE(zoomLevel, 1.8);
    QVERIFY(SiyiProtocol::parseManualZoomAckPayload(QByteArray::fromHex("1c00"), &zoomLevel));
    QCOMPARE(zoomLevel, 2.8);
    QVERIFY(SiyiProtocol::parseManualZoomAckPayload(QByteArray::fromHex("3700"), &zoomLevel));
    QCOMPARE(zoomLevel, 5.5);
    QVERIFY(SiyiProtocol::parseManualZoomAckPayload(QByteArray::fromHex("3c00"), &zoomLevel));
    QCOMPARE(zoomLevel, 6.0);
    QVERIFY(SiyiProtocol::parseManualZoomAckPayload(QByteArray::fromHex("2c01"), &zoomLevel));
    QCOMPARE(zoomLevel, 30.0);

    const QList<QByteArray> invalidPayloads = {
        QByteArray::fromHex("0900"),
        QByteArray::fromHex("2d01"),
        QByteArray::fromHex("ffff"),
        QByteArray::fromHex("0a"),
        QByteArray::fromHex("0a0000"),
    };
    for (const QByteArray& payload : invalidPayloads) {
        zoomLevel = 9.9;
        QVERIFY(!SiyiProtocol::parseManualZoomAckPayload(payload, &zoomLevel));
        QCOMPARE(zoomLevel, 9.9);
    }
    QVERIFY(!SiyiProtocol::parseManualZoomAckPayload(QByteArray::fromHex("0a00"), nullptr));
}

void SiyiProtocolTest::absoluteZoomAckPayloads()
{
    bool accepted = false;
    QVERIFY(SiyiProtocol::parseAbsoluteZoomAckPayload(QByteArray::fromHex("01"), &accepted));
    QVERIFY(accepted);
    QVERIFY(SiyiProtocol::parseAbsoluteZoomAckPayload(QByteArray::fromHex("00"), &accepted));
    QVERIFY(!accepted);

    accepted = true;
    QVERIFY(!SiyiProtocol::parseAbsoluteZoomAckPayload(QByteArray::fromHex("02"), &accepted));
    QVERIFY(accepted);
    QVERIFY(!SiyiProtocol::parseAbsoluteZoomAckPayload(QByteArray(), &accepted));
    QVERIFY(!SiyiProtocol::parseAbsoluteZoomAckPayload(QByteArray::fromHex("0001"), &accepted));
    QVERIFY(!SiyiProtocol::parseAbsoluteZoomAckPayload(QByteArray::fromHex("01"), nullptr));
}

void SiyiProtocolTest::maximumZoomPayloads()
{
    double zoomLevel = 9.9;
    bool usedLegacyTenthsEncoding = true;

    QVERIFY(SiyiProtocol::parseMaximumZoomPayload(
        QByteArray::fromHex("0100"), 6.0, &zoomLevel, &usedLegacyTenthsEncoding));
    QCOMPARE(zoomLevel, 1.0);
    QVERIFY(!usedLegacyTenthsEncoding);
    QVERIFY(SiyiProtocol::parseMaximumZoomPayload(QByteArray::fromHex("0305"), 6.0, &zoomLevel));
    QCOMPARE(zoomLevel, 3.5);
    QVERIFY(SiyiProtocol::parseMaximumZoomPayload(QByteArray::fromHex("0505"), 6.0, &zoomLevel));
    QCOMPARE(zoomLevel, 5.5);
    QVERIFY(SiyiProtocol::parseMaximumZoomPayload(QByteArray::fromHex("0600"), 6.0, &zoomLevel));
    QCOMPARE(zoomLevel, 6.0);
    QVERIFY(SiyiProtocol::parseMaximumZoomPayload(
        QByteArray::fromHex("0a00"), 6.0, &zoomLevel, &usedLegacyTenthsEncoding));
    QCOMPARE(zoomLevel, 1.0);
    QVERIFY(usedLegacyTenthsEncoding);
    QVERIFY(SiyiProtocol::parseMaximumZoomPayload(QByteArray::fromHex("0b00"), 6.0, &zoomLevel));
    QCOMPARE(zoomLevel, 1.1);
    QVERIFY(SiyiProtocol::parseMaximumZoomPayload(QByteArray::fromHex("2300"), 6.0, &zoomLevel));
    QCOMPARE(zoomLevel, 3.5);
    QVERIFY(SiyiProtocol::parseMaximumZoomPayload(QByteArray::fromHex("3700"), 6.0, &zoomLevel));
    QCOMPARE(zoomLevel, 5.5);
    QVERIFY(SiyiProtocol::parseMaximumZoomPayload(QByteArray::fromHex("3c00"), 6.0, &zoomLevel));
    QCOMPARE(zoomLevel, 6.0);

    zoomLevel = 9.9;
    const QList<QByteArray> invalidPayloads = {
        QByteArray::fromHex("0000"),
        QByteArray::fromHex("030a"),
        QByteArray::fromHex("0900"),
        QByteArray::fromHex("3d00"),
        QByteArray::fromHex("0001"),
        QByteArray::fromHex("ffff"),
        QByteArray::fromHex("01"),
        QByteArray::fromHex("010000"),
    };
    for (const QByteArray& payload : invalidPayloads) {
        usedLegacyTenthsEncoding = true;
        QVERIFY(!SiyiProtocol::parseMaximumZoomPayload(
            payload, 6.0, &zoomLevel, &usedLegacyTenthsEncoding));
        QCOMPARE(zoomLevel, 9.9);
        QVERIFY(usedLegacyTenthsEncoding);
    }
}

void SiyiProtocolTest::currentZoomPayloads()
{
    double zoomLevel = 0.0;
    bool usedLegacyTenthsEncoding = true;

    QVERIFY(SiyiProtocol::parseCurrentZoomPayload(
        QByteArray::fromHex("0100"), 1.0, 5.5, &zoomLevel, &usedLegacyTenthsEncoding));
    QCOMPARE(zoomLevel, 1.0);
    QVERIFY(!usedLegacyTenthsEncoding);
    QVERIFY(SiyiProtocol::parseCurrentZoomPayload(QByteArray::fromHex("0108"), 1.0, 5.5, &zoomLevel));
    QCOMPARE(zoomLevel, 1.8);
    QVERIFY(SiyiProtocol::parseCurrentZoomPayload(QByteArray::fromHex("0208"), 1.0, 5.5, &zoomLevel));
    QCOMPARE(zoomLevel, 2.8);
    QVERIFY(SiyiProtocol::parseCurrentZoomPayload(QByteArray::fromHex("0305"), 1.0, 5.5, &zoomLevel));
    QCOMPARE(zoomLevel, 3.5);
    QVERIFY(SiyiProtocol::parseCurrentZoomPayload(QByteArray::fromHex("0505"), 1.0, 5.5, &zoomLevel));
    QCOMPARE(zoomLevel, 5.5);
    QVERIFY(SiyiProtocol::parseCurrentZoomPayload(
        QByteArray::fromHex("0a00"), 1.0, 5.5, &zoomLevel, &usedLegacyTenthsEncoding));
    QCOMPARE(zoomLevel, 1.0);
    QVERIFY(usedLegacyTenthsEncoding);
    QVERIFY(SiyiProtocol::parseCurrentZoomPayload(QByteArray::fromHex("1000"), 1.0, 5.5, &zoomLevel));
    QCOMPARE(zoomLevel, 1.6);
    QVERIFY(SiyiProtocol::parseCurrentZoomPayload(QByteArray::fromHex("1200"), 1.0, 5.5, &zoomLevel));
    QCOMPARE(zoomLevel, 1.8);
    QVERIFY(SiyiProtocol::parseCurrentZoomPayload(QByteArray::fromHex("1400"), 1.0, 5.5, &zoomLevel));
    QCOMPARE(zoomLevel, 2.0);
    QVERIFY(SiyiProtocol::parseCurrentZoomPayload(QByteArray::fromHex("1c00"), 1.0, 5.5, &zoomLevel));
    QCOMPARE(zoomLevel, 2.8);
    QVERIFY(SiyiProtocol::parseCurrentZoomPayload(QByteArray::fromHex("2300"), 1.0, 5.5, &zoomLevel));
    QCOMPARE(zoomLevel, 3.5);
    QVERIFY(SiyiProtocol::parseCurrentZoomPayload(QByteArray::fromHex("3700"), 1.0, 5.5, &zoomLevel));
    QCOMPARE(zoomLevel, 5.5);
    QVERIFY(SiyiProtocol::parseCurrentZoomPayload(QByteArray::fromHex("3c00"), 1.0, 6.0, &zoomLevel));
    QCOMPARE(zoomLevel, 6.0);
}

void SiyiProtocolTest::invalidZoomPayloads()
{
    double zoomLevel = 3.2;
    const QList<QByteArray> invalidCurrentPayloads = {
        QByteArray::fromHex("0000"),
        QByteArray::fromHex("010a"),
        QByteArray::fromHex("0900"),
        QByteArray::fromHex("3800"),
        QByteArray::fromHex("3c00"),
        QByteArray::fromHex("0001"),
        QByteArray::fromHex("ffff"),
        QByteArray::fromHex("01"),
        QByteArray::fromHex("010000"),
    };
    for (const QByteArray& payload : invalidCurrentPayloads) {
        QVERIFY(!SiyiProtocol::parseCurrentZoomPayload(payload, 1.0, 5.5, &zoomLevel));
        QCOMPARE(zoomLevel, 3.2);
    }

    QVERIFY(!SiyiProtocol::parseCurrentZoomPayload(QByteArray::fromHex("0600"), 1.0, 5.5, &zoomLevel));
    QCOMPARE(zoomLevel, 3.2);
    QVERIFY(!SiyiProtocol::parseCurrentZoomPayload(QByteArray::fromHex("0100"), 2.0, 1.0, &zoomLevel));
    QCOMPARE(zoomLevel, 3.2);
    QVERIFY(!SiyiProtocol::parseCurrentZoomPayload(
        QByteArray::fromHex("0100"),
        1.0,
        std::numeric_limits<double>::quiet_NaN(),
        &zoomLevel));
    QCOMPARE(zoomLevel, 3.2);
    QVERIFY(!SiyiProtocol::parseCurrentZoomPayload(QByteArray::fromHex("0100"), 1.0, 5.5, nullptr));
}

void SiyiProtocolTest::pulledVideoResolutionLimits_data()
{
    QTest::addColumn<int>("width");
    QTest::addColumn<int>("height");
    QTest::addColumn<double>("maximumZoom");

    QTest::newRow("2k") << 2560 << 1440 << 3.5;
    QTest::newRow("1080p") << 1920 << 1080 << 5.5;
    QTest::newRow("1080p-coded-height") << 1920 << 1088 << 5.5;
    QTest::newRow("720p") << 1280 << 720 << 6.0;
    QTest::newRow("720p-coded-height") << 1280 << 736 << 6.0;
    QTest::newRow("4k-uhd-no-zoom") << 3840 << 2160 << 1.0;
    QTest::newRow("4k-dci-no-zoom") << 4096 << 2160 << 1.0;
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

    resolvedMaximumZoom = 9.9;
    QVERIFY(!A8MiniZoomPolicy::maximumZoomForVideoResolution(1366, 768, &resolvedMaximumZoom));
    QCOMPARE(resolvedMaximumZoom, 9.9);
    QVERIFY(!A8MiniZoomPolicy::maximumZoomForVideoResolution(640, 480, &resolvedMaximumZoom));
    QCOMPARE(resolvedMaximumZoom, 9.9);
    QVERIFY(!A8MiniZoomPolicy::maximumZoomForVideoResolution(1920, 1080, nullptr));
}

void SiyiProtocolTest::alignedMaximumZooms()
{
    double maximumZoom = 9.9;

    QVERIFY(A8MiniZoomPolicy::alignedMaximumZoom(5.5, 1.0, 1.0, &maximumZoom));
    QCOMPARE(maximumZoom, 5.5);
    QVERIFY(A8MiniZoomPolicy::alignedMaximumZoom(3.5, 1.0, 1.0, &maximumZoom));
    QCOMPARE(maximumZoom, 3.5);
    QVERIFY(A8MiniZoomPolicy::alignedMaximumZoom(6.0, 1.0, 1.0, &maximumZoom));
    QCOMPARE(maximumZoom, 6.0);
    QVERIFY(A8MiniZoomPolicy::alignedMaximumZoom(5.5, 0.5, 1.0, &maximumZoom));
    QCOMPARE(maximumZoom, 5.5);
    QVERIFY(A8MiniZoomPolicy::alignedMaximumZoom(3.5, 0.5, 1.0, &maximumZoom));
    QCOMPARE(maximumZoom, 3.5);
    QVERIFY(A8MiniZoomPolicy::alignedMaximumZoom(1.0, 1.0, 1.0, &maximumZoom));
    QCOMPARE(maximumZoom, 1.0);

    maximumZoom = 9.9;
    QVERIFY(!A8MiniZoomPolicy::alignedMaximumZoom(0.9, 1.0, 1.0, &maximumZoom));
    QCOMPARE(maximumZoom, 9.9);
    QVERIFY(!A8MiniZoomPolicy::alignedMaximumZoom(5.5, 0.0, 1.0, &maximumZoom));
    QCOMPARE(maximumZoom, 9.9);
    QVERIFY(!A8MiniZoomPolicy::alignedMaximumZoom(5.5, 1.0, 1.0, nullptr));
}

void SiyiProtocolTest::strictStepTargets()
{
    double targetZoom = 0.0;

    for (int current = 1; current < 5; ++current) {
        QVERIFY(A8MiniZoomPolicy::stepTarget(current, 1.0, 1.0, 5.5, 1, &targetZoom));
        QCOMPARE(targetZoom, current + 1.0);
    }
    QVERIFY(A8MiniZoomPolicy::stepTarget(5.0, 1.0, 1.0, 5.5, 1, &targetZoom));
    QCOMPARE(targetZoom, 5.5);
    QVERIFY(!A8MiniZoomPolicy::stepTarget(5.5, 1.0, 1.0, 5.5, 1, &targetZoom));
    QVERIFY(A8MiniZoomPolicy::stepTarget(5.5, 1.0, 1.0, 5.5, -1, &targetZoom));
    QCOMPARE(targetZoom, 5.0);
    for (int current = 5; current > 1; --current) {
        QVERIFY(A8MiniZoomPolicy::stepTarget(current, 1.0, 1.0, 5.5, -1, &targetZoom));
        QCOMPARE(targetZoom, current - 1.0);
    }

    QVERIFY(!A8MiniZoomPolicy::stepTarget(1.0, 1.0, 1.0, 5.5, -1, &targetZoom));

    QVERIFY(A8MiniZoomPolicy::stepTarget(3.0, 1.0, 1.0, 3.5, 1, &targetZoom));
    QCOMPARE(targetZoom, 3.5);
    QVERIFY(!A8MiniZoomPolicy::stepTarget(3.5, 1.0, 1.0, 3.5, 1, &targetZoom));
    QVERIFY(A8MiniZoomPolicy::stepTarget(3.5, 1.0, 1.0, 3.5, -1, &targetZoom));
    QCOMPARE(targetZoom, 3.0);

    QVERIFY(A8MiniZoomPolicy::stepTarget(1.8, 1.0, 1.0, 5.0, 1, &targetZoom));
    QCOMPARE(targetZoom, 2.0);
    QVERIFY(A8MiniZoomPolicy::stepTarget(1.8, 1.0, 1.0, 5.0, -1, &targetZoom));
    QCOMPARE(targetZoom, 1.0);
    QVERIFY(A8MiniZoomPolicy::stepTarget(1.6, 1.0, 1.0, 5.5, 1, &targetZoom));
    QCOMPARE(targetZoom, 2.0);
    QVERIFY(A8MiniZoomPolicy::stepTarget(1.6, 1.0, 1.0, 5.5, -1, &targetZoom));
    QCOMPARE(targetZoom, 1.0);
    QVERIFY(A8MiniZoomPolicy::stepTarget(2.8, 1.0, 1.0, 5.0, 1, &targetZoom));
    QCOMPARE(targetZoom, 3.0);
    QVERIFY(A8MiniZoomPolicy::stepTarget(2.8, 1.0, 1.0, 5.0, -1, &targetZoom));
    QCOMPARE(targetZoom, 2.0);
    QVERIFY(A8MiniZoomPolicy::stepTarget(4.5, 1.0, 1.0, 5.0, 1, &targetZoom));
    QCOMPARE(targetZoom, 5.0);
    QVERIFY(A8MiniZoomPolicy::stepTarget(4.5, 1.0, 1.0, 5.0, -1, &targetZoom));
    QCOMPARE(targetZoom, 4.0);
    QVERIFY(A8MiniZoomPolicy::stepTarget(5.5, 1.0, 1.0, 5.0, -1, &targetZoom));
    QCOMPARE(targetZoom, 5.0);
    QVERIFY(!A8MiniZoomPolicy::stepTarget(5.5, 1.0, 1.0, 5.0, 1, &targetZoom));
    QVERIFY(!A8MiniZoomPolicy::stepTarget(1.0, 1.0, 1.0, 1.0, 1, &targetZoom));
    QVERIFY(!A8MiniZoomPolicy::stepTarget(1.0, 0.0, 1.0, 5.5, 1, &targetZoom));
    QVERIFY(!A8MiniZoomPolicy::stepTarget(1.0, 1.0, 1.0, 5.5, 0, &targetZoom));

    QVERIFY(A8MiniZoomPolicy::stepTarget(5.0, 0.5, 1.0, 5.5, 1, &targetZoom));
    QCOMPARE(targetZoom, 5.5);
    QVERIFY(A8MiniZoomPolicy::stepTarget(5.5, 0.5, 1.0, 5.5, -1, &targetZoom));
    QCOMPARE(targetZoom, 5.0);

    // The exact resolution ceiling is a legal terminal stop. Off-grid camera
    // feedback close to that ceiling must move to a legal stop in the requested
    // direction instead of becoming a new step anchor.
    QVERIFY(A8MiniZoomPolicy::stepTarget(5.2, 1.0, 1.0, 5.5, 1, &targetZoom));
    QCOMPARE(targetZoom, 5.5);
    QVERIFY(A8MiniZoomPolicy::stepTarget(5.2, 1.0, 1.0, 5.5, -1, &targetZoom));
    QCOMPARE(targetZoom, 5.0);
    QVERIFY(A8MiniZoomPolicy::stepTarget(5.3, 1.0, 1.0, 5.5, 1, &targetZoom));
    QCOMPARE(targetZoom, 5.5);
    QVERIFY(A8MiniZoomPolicy::stepTarget(5.3, 1.0, 1.0, 5.5, -1, &targetZoom));
    QCOMPARE(targetZoom, 5.0);
    QVERIFY(A8MiniZoomPolicy::stepTarget(3.3, 1.0, 1.0, 3.5, 1, &targetZoom));
    QCOMPARE(targetZoom, 3.5);
    QVERIFY(A8MiniZoomPolicy::stepTarget(3.3, 1.0, 1.0, 3.5, -1, &targetZoom));
    QCOMPARE(targetZoom, 3.0);
    QVERIFY(A8MiniZoomPolicy::stepTarget(5.6, 1.0, 1.0, 5.5, -1, &targetZoom));
    QCOMPARE(targetZoom, 5.5);
    QVERIFY(!A8MiniZoomPolicy::stepTarget(5.6, 1.0, 1.0, 5.5, 1, &targetZoom));
    QVERIFY(A8MiniZoomPolicy::stepTarget(4.0, 1.0, 1.0, 3.5, -1, &targetZoom));
    QCOMPARE(targetZoom, 3.5);
    QVERIFY(!A8MiniZoomPolicy::stepTarget(4.0, 1.0, 1.0, 3.5, 1, &targetZoom));
}

void SiyiProtocolTest::alignmentTargets()
{
    double targetZoom = 0.0;

    QVERIFY(A8MiniZoomPolicy::alignmentTarget(1.8, 1.0, 1.0, 5.5, 0, &targetZoom));
    QCOMPARE(targetZoom, 2.0);
    QVERIFY(A8MiniZoomPolicy::alignmentTarget(2.8, 1.0, 1.0, 5.5, 0, &targetZoom));
    QCOMPARE(targetZoom, 3.0);
    QVERIFY(A8MiniZoomPolicy::alignmentTarget(5.2, 1.0, 1.0, 5.5, 0, &targetZoom));
    QCOMPARE(targetZoom, 5.0);
    QVERIFY(A8MiniZoomPolicy::alignmentTarget(5.3, 1.0, 1.0, 5.5, 0, &targetZoom));
    QCOMPARE(targetZoom, 5.5);
    QVERIFY(A8MiniZoomPolicy::alignmentTarget(3.3, 1.0, 1.0, 3.5, 0, &targetZoom));
    QCOMPARE(targetZoom, 3.5);
    QVERIFY(A8MiniZoomPolicy::alignmentTarget(5.5, 1.0, 1.0, 5.5, 0, &targetZoom));
    QCOMPARE(targetZoom, 5.5);
    QVERIFY(A8MiniZoomPolicy::alignmentTarget(3.5, 1.0, 1.0, 3.5, 0, &targetZoom));
    QCOMPARE(targetZoom, 3.5);
    QVERIFY(A8MiniZoomPolicy::alignmentTarget(1.5, 1.0, 1.0, 5.5, -1, &targetZoom));
    QCOMPARE(targetZoom, 1.0);
    QVERIFY(A8MiniZoomPolicy::alignmentTarget(1.5, 1.0, 1.0, 5.5, 1, &targetZoom));
    QCOMPARE(targetZoom, 2.0);
    QVERIFY(A8MiniZoomPolicy::alignmentTarget(5.5, 0.5, 1.0, 5.5, 0, &targetZoom));
    QCOMPARE(targetZoom, 5.5);

    QVERIFY(A8MiniZoomPolicy::isAlignedZoom(5.0, 1.0, 1.0, 5.5));
    QVERIFY(!A8MiniZoomPolicy::isAlignedZoom(1.8, 1.0, 1.0, 5.5));
    QVERIFY(A8MiniZoomPolicy::isAlignedZoom(5.5, 1.0, 1.0, 5.5));
    QVERIFY(A8MiniZoomPolicy::isAlignedZoom(3.5, 1.0, 1.0, 3.5));
    QVERIFY(A8MiniZoomPolicy::isAlignedZoom(5.5, 0.5, 1.0, 5.5));
}

void SiyiProtocolTest::repeatedStepSequences()
{
    double currentZoom = 1.0;
    double targetZoom = 0.0;
    const QList<double> expected1080pZoomIn = {2.0, 3.0, 4.0, 5.0, 5.5};
    for (const double expected : expected1080pZoomIn) {
        QVERIFY(A8MiniZoomPolicy::stepTarget(currentZoom, 1.0, 1.0, 5.5, 1, &targetZoom));
        QCOMPARE(targetZoom, expected);
        currentZoom = targetZoom;
    }
    QVERIFY(!A8MiniZoomPolicy::stepTarget(currentZoom, 1.0, 1.0, 5.5, 1, &targetZoom));

    const QList<double> expected1080pZoomOut = {5.0, 4.0, 3.0, 2.0, 1.0};
    for (const double expected : expected1080pZoomOut) {
        QVERIFY(A8MiniZoomPolicy::stepTarget(currentZoom, 1.0, 1.0, 5.5, -1, &targetZoom));
        QCOMPARE(targetZoom, expected);
        currentZoom = targetZoom;
    }
    QVERIFY(!A8MiniZoomPolicy::stepTarget(currentZoom, 1.0, 1.0, 5.5, -1, &targetZoom));

    currentZoom = 1.0;
    const QList<double> expected2kZoomIn = {2.0, 3.0, 3.5};
    for (const double expected : expected2kZoomIn) {
        QVERIFY(A8MiniZoomPolicy::stepTarget(currentZoom, 1.0, 1.0, 3.5, 1, &targetZoom));
        QCOMPARE(targetZoom, expected);
        currentZoom = targetZoom;
    }
    QVERIFY(!A8MiniZoomPolicy::stepTarget(currentZoom, 1.0, 1.0, 3.5, 1, &targetZoom));

    const QList<double> expected2kZoomOut = {3.0, 2.0, 1.0};
    for (const double expected : expected2kZoomOut) {
        QVERIFY(A8MiniZoomPolicy::stepTarget(currentZoom, 1.0, 1.0, 3.5, -1, &targetZoom));
        QCOMPARE(targetZoom, expected);
        currentZoom = targetZoom;
    }
    QVERIFY(!A8MiniZoomPolicy::stepTarget(currentZoom, 1.0, 1.0, 3.5, -1, &targetZoom));

    currentZoom = 1.0;
    for (int step = 1; step <= 9; ++step) {
        QVERIFY(A8MiniZoomPolicy::stepTarget(currentZoom, 0.5, 1.0, 5.5, 1, &targetZoom));
        QCOMPARE(targetZoom, 1.0 + step * 0.5);
        currentZoom = targetZoom;
    }
    QCOMPARE(currentZoom, 5.5);
    QVERIFY(!A8MiniZoomPolicy::stepTarget(currentZoom, 0.5, 1.0, 5.5, 1, &targetZoom));

    for (int step = 1; step <= 9; ++step) {
        QVERIFY(A8MiniZoomPolicy::stepTarget(currentZoom, 0.5, 1.0, 5.5, -1, &targetZoom));
        QCOMPARE(targetZoom, 5.5 - step * 0.5);
        currentZoom = targetZoom;
    }
    QCOMPARE(currentZoom, 1.0);
    QVERIFY(!A8MiniZoomPolicy::stepTarget(currentZoom, 0.5, 1.0, 5.5, -1, &targetZoom));

    currentZoom = 1.0;
    const QList<double> expectedPointSevenZoomIn =
        {1.7, 2.4, 3.1, 3.8, 4.5, 5.2, 5.5};
    for (const double expected : expectedPointSevenZoomIn) {
        QVERIFY(A8MiniZoomPolicy::stepTarget(
            currentZoom, 0.7, 1.0, 5.5, 1, &targetZoom));
        QCOMPARE(targetZoom, expected);
        QVERIFY(A8MiniZoomPolicy::isAlignedZoom(
            targetZoom, 0.7, 1.0, 5.5));
        currentZoom = targetZoom;
    }
    QVERIFY(!A8MiniZoomPolicy::stepTarget(
        currentZoom, 0.7, 1.0, 5.5, 1, &targetZoom));

    const QList<double> expectedPointSevenZoomOut =
        {5.2, 4.5, 3.8, 3.1, 2.4, 1.7, 1.0};
    for (const double expected : expectedPointSevenZoomOut) {
        QVERIFY(A8MiniZoomPolicy::stepTarget(
            currentZoom, 0.7, 1.0, 5.5, -1, &targetZoom));
        QCOMPARE(targetZoom, expected);
        QVERIFY(A8MiniZoomPolicy::isAlignedZoom(
            targetZoom, 0.7, 1.0, 5.5));
        currentZoom = targetZoom;
    }
    QVERIFY(!A8MiniZoomPolicy::stepTarget(
        currentZoom, 0.7, 1.0, 5.5, -1, &targetZoom));

    // A newer tap/hold target is planned from the previous legal request,
    // not from an off-grid 0x18 movement sample. This is the rapid-input
    // sequence used when pending 2.0x still reports raw 1.6x.
    const double movementSample = 1.6;
    double pendingRequestedZoom = 2.0;
    QVERIFY(A8MiniZoomPolicy::stepTarget(
        pendingRequestedZoom, 1.0, 1.0, 5.5, 1, &targetZoom));
    QCOMPARE(targetZoom, 3.0);
    pendingRequestedZoom = targetZoom;
    QVERIFY(A8MiniZoomPolicy::stepTarget(
        pendingRequestedZoom, 1.0, 1.0, 5.5, 1, &targetZoom));
    QCOMPARE(targetZoom, 4.0);

    QVERIFY(A8MiniZoomPolicy::stepTarget(
        movementSample, 1.0, 1.0, 5.5, 1, &targetZoom));
    QCOMPARE(targetZoom, 2.0);
}

void SiyiProtocolTest::targetTrackerRecovery()
{
    using Observation = A8MiniZoomPolicy::TargetObservation;

    A8MiniZoomPolicy::TargetTracker tracker;
    tracker.reset(2.0);
    QCOMPARE(tracker.observe(2.0), Observation::TargetReached);

    tracker.reset(2.0);
    QCOMPARE(tracker.observe(1.2), Observation::Waiting);
    QCOMPARE(tracker.observe(1.5), Observation::Waiting);
    QCOMPARE(tracker.observe(2.0), Observation::TargetReached);

    tracker.reset(2.0);
    QCOMPARE(tracker.observe(1.0), Observation::Waiting);
    QCOMPARE(tracker.observe(1.0), Observation::Waiting);
    QCOMPARE(tracker.observe(1.0), Observation::Waiting);
    QCOMPARE(tracker.stableDifferentZoom(), 1.0);
    QCOMPARE(tracker.observe(1.6), Observation::Waiting);
    QCOMPARE(tracker.observe(1.6), Observation::Waiting);
    QCOMPARE(tracker.observe(1.6), Observation::Waiting);
    QCOMPARE(tracker.observe(1.6), Observation::Waiting);
    QCOMPARE(tracker.observe(1.6), Observation::StableDifferent);
    QCOMPARE(tracker.observe(2.0), Observation::TargetReached);

    tracker.reset(2.0);
    QCOMPARE(tracker.observe(1.8), Observation::Waiting);
    QCOMPARE(tracker.observe(1.8), Observation::Waiting);
    QCOMPARE(tracker.observe(1.9), Observation::Waiting);
    QCOMPARE(tracker.observe(1.9), Observation::Waiting);
    QCOMPARE(tracker.observe(1.9), Observation::Waiting);
    QCOMPARE(tracker.observe(1.9), Observation::Waiting);
    QCOMPARE(tracker.observe(1.9), Observation::StableDifferent);

    tracker.clear();
    QCOMPARE(tracker.observe(2.0), Observation::Waiting);
}

QTEST_APPLESS_MAIN(SiyiProtocolTest)

#include "SiyiProtocolTest.moc"
