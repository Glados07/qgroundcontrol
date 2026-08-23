/****************************************************************************
 *
 * UniPod MT11 SDK V0.1.0 protocol regression tests.
 *
 ****************************************************************************/

#include "Mt11Protocol.h"
#include "Mt11ZoomPolicy.h"
#include "ZoomStepPolicy.h"

#include <QtTest/QTest>

#include <limits>

class Mt11ProtocolTest : public QObject
{
    Q_OBJECT

private slots:
    void documentedCommandFrames();
    void thermalFrames();
    void strictFrameDecoding();
    void multiFrameDatagramIsAtomic();
    void zoomPayloads();
    void zoomStepPolicy();
    void mt11ZoomPolicy();
    void cameraAndFunctionPayloads();
    void videoModePayloads();
};

void Mt11ProtocolTest::documentedCommandFrames()
{
    // Exact examples from the PDF, pages 37-40.
    QCOMPARE(Mt11Protocol::manualZoomPacket(1).toHex(),
             QByteArrayLiteral("5566010100000005018d64"));
    QCOMPARE(Mt11Protocol::manualZoomPacket(0).toHex(),
             QByteArrayLiteral("556601010000000500ac74"));
    QCOMPARE(Mt11Protocol::manualZoomPacket(-1).toHex(),
             QByteArrayLiteral("5566010100000005ff5c6a"));
    QVERIFY(Mt11Protocol::manualZoomPacket(2).isEmpty());

    QCOMPARE(Mt11Protocol::takePhotoPacket().toHex(),
             QByteArrayLiteral("556601010000000c0034ce"));
    QCOMPARE(Mt11Protocol::toggleVideoRecordingPacket().toHex(),
             QByteArrayLiteral("556601010000000c0276ee"));
    QCOMPARE(Mt11Protocol::requestCameraSystemStatusPacket().toHex(),
             QByteArrayLiteral("556601000000000a0f75"));
    QCOMPARE(Mt11Protocol::absoluteZoomPacket(1.0).toHex(),
             QByteArrayLiteral("556601020000000f010061be"));
    QCOMPARE(Mt11Protocol::absoluteZoomPacket(7.5).toHex(),
             QByteArrayLiteral("556601020000000f07056244"));
    QCOMPARE(Mt11Protocol::absoluteZoomPacket(30.0).toHex(),
             QByteArrayLiteral("556601020000000f1e002cad"));
    QVERIFY(Mt11Protocol::absoluteZoomPacket(0.9).isEmpty());
    QVERIFY(Mt11Protocol::absoluteZoomPacket(30.1).isEmpty());
    QVERIFY(Mt11Protocol::absoluteZoomPacket(
                std::numeric_limits<double>::quiet_NaN()).isEmpty());
    QCOMPARE(Mt11Protocol::requestMaximumZoomPacket().toHex(),
             QByteArrayLiteral("5566010000000016b2a6"));
    QCOMPARE(Mt11Protocol::requestCurrentZoomPacket().toHex(),
             QByteArrayLiteral("55660100000000187c47"));
}

void Mt11ProtocolTest::thermalFrames()
{
    QCOMPARE(Mt11Protocol::requestVideoModePacket().toHex(),
             QByteArrayLiteral("556601000000001074c6"));
    // Main zoom + sub thermal and the inverse are the two documented MT11
    // combinations used for the RGB/thermal switch.
    QCOMPARE(Mt11Protocol::setThermalModePacket(false).toHex(),
             QByteArrayLiteral("5566010200000011000270f5"));
    QCOMPARE(Mt11Protocol::setThermalModePacket(true).toHex(),
             QByteArrayLiteral("5566010200000011020050b3"));
}

void Mt11ProtocolTest::strictFrameDecoding()
{
    const QByteArray request = Mt11Protocol::requestCurrentZoomPacket();
    const auto decodedRequest = Mt11Protocol::decodePacket(request);
    QVERIFY(decodedRequest.valid);
    QCOMPARE(decodedRequest.control, quint8(0x01));
    QCOMPARE(decodedRequest.sequence, quint16(0));
    QCOMPARE(decodedRequest.command,
             quint8(Mt11Protocol::CommandCurrentZoomValue));
    QVERIFY(decodedRequest.payload.isEmpty());
    QVERIFY(!Mt11Protocol::isAckPacket(decodedRequest));

    // Valid non-zero sequence proves little-endian decode; production sends
    // zero, but replies are allowed to choose any 16-bit sequence.
    const QByteArray nonZeroSequenceAck =
        QByteArray::fromHex("55660202003412180505a15f");
    const auto ack = Mt11Protocol::decodePacket(nonZeroSequenceAck);
    QVERIFY(ack.valid);
    QCOMPARE(ack.control, quint8(0x02));
    QCOMPARE(ack.sequence, quint16(0x1234));
    QCOMPARE(ack.payload, QByteArray::fromHex("0505"));
    QVERIFY(Mt11Protocol::isAckPacket(ack));

    QByteArray trailing = request;
    trailing.append('\0');
    QVERIFY(!Mt11Protocol::decodePacket(trailing).valid);
    QByteArray badCrc = request;
    badCrc[badCrc.size() - 1] =
        static_cast<char>(static_cast<quint8>(badCrc.back()) ^ 1);
    QVERIFY(!Mt11Protocol::decodePacket(badCrc).valid);
    QByteArray reservedControl = request;
    reservedControl[2] = static_cast<char>(0x04);
    QVERIFY(!Mt11Protocol::decodePacket(reservedControl).valid);
}

void Mt11ProtocolTest::multiFrameDatagramIsAtomic()
{
    const QByteArray datagram = QByteArray::fromHex(
        "55660202003412160505a044"
        "556602080035120a0000000101020000727a"
        "556602020036121802081853");

    QList<Mt11Protocol::DecodedPacket> packets;
    QVERIFY(Mt11Protocol::decodeDatagram(datagram, &packets));
    QCOMPARE(packets.size(), 3);
    QCOMPARE(packets.at(0).sequence, quint16(0x1234));
    QCOMPARE(packets.at(0).command,
             quint8(Mt11Protocol::CommandMaximumZoomValue));
    QCOMPARE(packets.at(1).sequence, quint16(0x1235));
    QCOMPARE(packets.at(1).command,
             quint8(Mt11Protocol::CommandCameraSystemInfo));
    QCOMPARE(packets.at(2).sequence, quint16(0x1236));
    QCOMPARE(packets.at(2).command,
             quint8(Mt11Protocol::CommandCurrentZoomValue));

    Mt11Protocol::DecodedPacket sentinel;
    sentinel.valid = true;
    sentinel.sequence = 0xbeef;
    QList<Mt11Protocol::DecodedPacket> unchanged{sentinel};
    QByteArray badMiddle = datagram;
    badMiddle[29] = static_cast<char>(
        static_cast<quint8>(badMiddle.at(29)) ^ 1);
    QVERIFY(!Mt11Protocol::decodeDatagram(badMiddle, &unchanged));
    QCOMPARE(unchanged.size(), 1);
    QCOMPARE(unchanged.constFirst().sequence, quint16(0xbeef));

    QByteArray truncated = datagram;
    truncated.chop(1);
    QVERIFY(!Mt11Protocol::decodeDatagram(truncated, &unchanged));
    QCOMPARE(unchanged.size(), 1);
    QVERIFY(!Mt11Protocol::decodeDatagram({}, &unchanged));
    QVERIFY(!Mt11Protocol::decodeDatagram(datagram, nullptr));
}

void Mt11ProtocolTest::zoomPayloads()
{
    double zoom = 99.0;
    QVERIFY(Mt11Protocol::parseManualZoomAckPayload(
        QByteArray::fromHex("4b00"), &zoom));
    QCOMPARE(zoom, 7.5);
    // 0x05 ACK is little-endian uint16/10: 0x0673 / 10 = 165.1x.
    QVERIFY(Mt11Protocol::parseManualZoomAckPayload(
        QByteArray::fromHex("7306"), &zoom));
    QCOMPARE(zoom, 165.1);
    QVERIFY(Mt11Protocol::parseManualZoomAckPayload(
        QByteArray::fromHex("ff09"), &zoom));
    QCOMPARE(zoom, 255.9);
    QVERIFY(!Mt11Protocol::parseManualZoomAckPayload(
        QByteArray::fromHex("0900"), &zoom));
    QVERIFY(!Mt11Protocol::parseManualZoomAckPayload(
        QByteArray::fromHex("000a"), &zoom));
    QVERIFY(!Mt11Protocol::parseManualZoomAckPayload(
        QByteArray::fromHex("7306"), nullptr));

    bool accepted = false;
    QVERIFY(Mt11Protocol::parseAbsoluteZoomAckPayload(
        QByteArray::fromHex("01"), &accepted));
    QVERIFY(accepted);
    QVERIFY(Mt11Protocol::parseAbsoluteZoomAckPayload(
        QByteArray::fromHex("00"), &accepted));
    QVERIFY(!accepted);
    QVERIFY(!Mt11Protocol::parseAbsoluteZoomAckPayload(
        QByteArray::fromHex("02"), &accepted));

    QVERIFY(Mt11Protocol::parseZoomValuePayload(
        QByteArray::fromHex("1e00"), 1.0, 30.0, &zoom));
    QCOMPARE(zoom, 30.0);
    QVERIFY(Mt11Protocol::parseZoomValuePayload(
        QByteArray::fromHex("0705"), 1.0, 30.0, &zoom));
    QCOMPARE(zoom, 7.5);
    // 0x16/0x18 use integer + one decimal digit: 0xa5 + 0.1 = 165.1x.
    QVERIFY(Mt11Protocol::parseZoomValuePayload(
        QByteArray::fromHex("a501"), 1.0, 255.9, &zoom));
    QCOMPARE(zoom, 165.1);
    QVERIFY(Mt11Protocol::parseZoomValuePayload(
        QByteArray::fromHex("ff09"), 1.0, 255.9, &zoom));
    QCOMPARE(zoom, 255.9);
    QVERIFY(!Mt11Protocol::parseZoomValuePayload(
        QByteArray::fromHex("a501"), 1.0, 165.0, &zoom));
    QVERIFY(!Mt11Protocol::parseZoomValuePayload(
        QByteArray::fromHex("070a"), 1.0, 30.0, &zoom));
    QVERIFY(!Mt11Protocol::parseZoomValuePayload(
        QByteArray::fromHex("0009"), 1.0, 30.0, &zoom));
}

void Mt11ProtocolTest::zoomStepPolicy()
{
    constexpr double minimumZoom = 1.0;
    constexpr double absoluteMaximumZoom = 30.0;
    constexpr double zoomStep = 1.0;

    double currentZoom = minimumZoom;
    double targetZoom = 0.0;
    for (int expected = 2; expected <= 30; ++expected) {
        QVERIFY(ZoomStepPolicy::stepTarget(currentZoom,
                                           zoomStep,
                                           minimumZoom,
                                           absoluteMaximumZoom,
                                           1,
                                           &targetZoom));
        QCOMPARE(targetZoom, static_cast<double>(expected));
        QVERIFY(ZoomStepPolicy::isAlignedZoom(targetZoom,
                                              zoomStep,
                                              minimumZoom,
                                              absoluteMaximumZoom));
        currentZoom = targetZoom;
    }
    QVERIFY(!ZoomStepPolicy::stepTarget(currentZoom,
                                        zoomStep,
                                        minimumZoom,
                                        absoluteMaximumZoom,
                                        1,
                                        &targetZoom));

    for (int expected = 29; expected >= 1; --expected) {
        QVERIFY(ZoomStepPolicy::stepTarget(currentZoom,
                                           zoomStep,
                                           minimumZoom,
                                           absoluteMaximumZoom,
                                           -1,
                                           &targetZoom));
        QCOMPARE(targetZoom, static_cast<double>(expected));
        QVERIFY(ZoomStepPolicy::isAlignedZoom(targetZoom,
                                              zoomStep,
                                              minimumZoom,
                                              absoluteMaximumZoom));
        currentZoom = targetZoom;
    }
    QVERIFY(!ZoomStepPolicy::stepTarget(currentZoom,
                                        zoomStep,
                                        minimumZoom,
                                        absoluteMaximumZoom,
                                        -1,
                                        &targetZoom));

    // Measured feedback can be off-grid after native continuous zoom. A tap
    // must select the next minimum-anchored stop in its requested direction,
    // never turn that measured value into a new grid origin.
    QVERIFY(ZoomStepPolicy::stepTarget(
        1.6, zoomStep, minimumZoom, absoluteMaximumZoom, 1, &targetZoom));
    QCOMPARE(targetZoom, 2.0);
    QVERIFY(ZoomStepPolicy::stepTarget(
        1.6, zoomStep, minimumZoom, absoluteMaximumZoom, -1, &targetZoom));
    QCOMPARE(targetZoom, 1.0);
    QVERIFY(ZoomStepPolicy::stepTarget(
        5.6, zoomStep, minimumZoom, absoluteMaximumZoom, 1, &targetZoom));
    QCOMPARE(targetZoom, 6.0);
    QVERIFY(ZoomStepPolicy::stepTarget(
        5.6, zoomStep, minimumZoom, absoluteMaximumZoom, -1, &targetZoom));
    QCOMPARE(targetZoom, 5.0);
    QVERIFY(ZoomStepPolicy::stepTarget(
        29.5, zoomStep, minimumZoom, absoluteMaximumZoom, 1, &targetZoom));
    QCOMPARE(targetZoom, 30.0);
    QVERIFY(ZoomStepPolicy::stepTarget(
        29.5, zoomStep, minimumZoom, absoluteMaximumZoom, -1, &targetZoom));
    QCOMPARE(targetZoom, 29.0);

    // A non-integral device ceiling remains one exact legal terminal stop.
    constexpr double nonIntegralMaximumZoom = 8.5;
    QVERIFY(ZoomStepPolicy::isAlignedZoom(nonIntegralMaximumZoom,
                                          zoomStep,
                                          minimumZoom,
                                          nonIntegralMaximumZoom));
    QVERIFY(ZoomStepPolicy::stepTarget(8.0,
                                       zoomStep,
                                       minimumZoom,
                                       nonIntegralMaximumZoom,
                                       1,
                                       &targetZoom));
    QCOMPARE(targetZoom, nonIntegralMaximumZoom);
    QVERIFY(ZoomStepPolicy::stepTarget(nonIntegralMaximumZoom,
                                       zoomStep,
                                       minimumZoom,
                                       nonIntegralMaximumZoom,
                                       -1,
                                       &targetZoom));
    QCOMPARE(targetZoom, 8.0);

    // The generic grid helper deliberately preserves its recovery behavior
    // above the supplied ceiling. MT11 must apply its hold-only >30x gate
    // before calling it, otherwise a downward tap at 100x would target 30x.
    QVERIFY(!ZoomStepPolicy::stepTarget(30.0,
                                        zoomStep,
                                        minimumZoom,
                                        absoluteMaximumZoom,
                                        1,
                                        &targetZoom));
    QVERIFY(ZoomStepPolicy::stepTarget(30.0,
                                       zoomStep,
                                       minimumZoom,
                                       absoluteMaximumZoom,
                                       -1,
                                       &targetZoom));
    QCOMPARE(targetZoom, 29.0);
    QVERIFY(!ZoomStepPolicy::stepTarget(100.0,
                                        zoomStep,
                                        minimumZoom,
                                        absoluteMaximumZoom,
                                        1,
                                        &targetZoom));
    QVERIFY(ZoomStepPolicy::stepTarget(100.0,
                                       zoomStep,
                                       minimumZoom,
                                       absoluteMaximumZoom,
                                       -1,
                                       &targetZoom));
    QCOMPARE(targetZoom, 30.0);
    QVERIFY(!ZoomStepPolicy::isAlignedZoom(100.0,
                                           zoomStep,
                                           minimumZoom,
                                           absoluteMaximumZoom));

    // Public absolute-set entry points can use this predicate to reject
    // in-range values which are nevertheless outside the configured grid.
    QVERIFY(ZoomStepPolicy::isAlignedZoom(2.0,
                                          zoomStep,
                                          minimumZoom,
                                          absoluteMaximumZoom));
    QVERIFY(!ZoomStepPolicy::isAlignedZoom(2.5,
                                           zoomStep,
                                           minimumZoom,
                                           absoluteMaximumZoom));
    QVERIFY(ZoomStepPolicy::isAlignedZoom(2.5,
                                          0.5,
                                          minimumZoom,
                                          absoluteMaximumZoom));
    QVERIFY(!ZoomStepPolicy::isAlignedZoom(30.1,
                                           zoomStep,
                                           minimumZoom,
                                           absoluteMaximumZoom));

    QVERIFY(ZoomStepPolicy::alignmentTarget(5.5,
                                            zoomStep,
                                            minimumZoom,
                                            absoluteMaximumZoom,
                                            1,
                                            &targetZoom));
    QCOMPARE(targetZoom, 6.0);
    QVERIFY(ZoomStepPolicy::alignmentTarget(5.5,
                                            zoomStep,
                                            minimumZoom,
                                            absoluteMaximumZoom,
                                            -1,
                                            &targetZoom));
    QCOMPARE(targetZoom, 5.0);
}

void Mt11ProtocolTest::mt11ZoomPolicy()
{
    constexpr double zoomStep = 1.0;
    constexpr double deviceMaximumZoom = 165.1;
    double targetZoom = 0.0;

    // Raw measured feedback owns the protocol gate; the already-aligned
    // display target owns grid planning for an eligible tap.
    QVERIFY(Mt11ZoomPolicy::tapTarget(5.6,
                                      6.0,
                                      zoomStep,
                                      deviceMaximumZoom,
                                      1,
                                      &targetZoom));
    QCOMPARE(targetZoom, 7.0);
    QVERIFY(Mt11ZoomPolicy::tapTarget(5.6,
                                      6.0,
                                      zoomStep,
                                      deviceMaximumZoom,
                                      -1,
                                      &targetZoom));
    QCOMPARE(targetZoom, 5.0);
    QVERIFY(!Mt11ZoomPolicy::tapTarget(5.6,
                                       5.6,
                                       zoomStep,
                                       deviceMaximumZoom,
                                       1,
                                       &targetZoom));

    // The exact 30x absolute-command boundary is an additional terminal stop
    // when the configured grid cannot divide the 1x-30x range evenly. A 2x
    // tap grid must therefore be symmetric: 1, 3, ..., 29, 30 and back.
    double stepTwoZoom = 1.0;
    for (int expected = 3; expected <= 29; expected += 2) {
        QVERIFY(Mt11ZoomPolicy::tapTarget(stepTwoZoom,
                                          stepTwoZoom,
                                          2.0,
                                          deviceMaximumZoom,
                                          1,
                                          &targetZoom));
        QCOMPARE(targetZoom, static_cast<double>(expected));
        stepTwoZoom = targetZoom;
    }
    QVERIFY(Mt11ZoomPolicy::tapTarget(stepTwoZoom,
                                      stepTwoZoom,
                                      2.0,
                                      deviceMaximumZoom,
                                      1,
                                      &targetZoom));
    QCOMPARE(targetZoom, 30.0);
    stepTwoZoom = targetZoom;
    QVERIFY(!Mt11ZoomPolicy::tapTarget(stepTwoZoom,
                                       stepTwoZoom,
                                       2.0,
                                       deviceMaximumZoom,
                                       1,
                                       &targetZoom));
    QVERIFY(Mt11ZoomPolicy::tapTarget(stepTwoZoom,
                                      stepTwoZoom,
                                      2.0,
                                      deviceMaximumZoom,
                                      -1,
                                      &targetZoom));
    QCOMPARE(targetZoom, 29.0);
    stepTwoZoom = targetZoom;
    for (int expected = 27; expected >= 1; expected -= 2) {
        QVERIFY(Mt11ZoomPolicy::tapTarget(stepTwoZoom,
                                          stepTwoZoom,
                                          2.0,
                                          deviceMaximumZoom,
                                          -1,
                                          &targetZoom));
        QCOMPARE(targetZoom, static_cast<double>(expected));
        stepTwoZoom = targetZoom;
    }
    QVERIFY(!Mt11ZoomPolicy::tapTarget(stepTwoZoom,
                                       stepTwoZoom,
                                       2.0,
                                       deviceMaximumZoom,
                                       -1,
                                       &targetZoom));

    // Exactly 30.0x still permits a downward absolute tap. At 30.1x and
    // above, measured feedback blocks both directions even if displayZoom is
    // a superficially valid 30.0x target.
    QVERIFY(!Mt11ZoomPolicy::tapTarget(30.0,
                                       30.0,
                                       zoomStep,
                                       deviceMaximumZoom,
                                       1,
                                       &targetZoom));
    QVERIFY(Mt11ZoomPolicy::tapTarget(30.0,
                                      30.0,
                                      zoomStep,
                                      deviceMaximumZoom,
                                      -1,
                                      &targetZoom));
    QCOMPARE(targetZoom, 29.0);
    for (const double measuredZoom : {30.1, 100.0}) {
        QVERIFY(!Mt11ZoomPolicy::tapTarget(measuredZoom,
                                           30.0,
                                           zoomStep,
                                           deviceMaximumZoom,
                                           1,
                                           &targetZoom));
        QVERIFY(!Mt11ZoomPolicy::tapTarget(measuredZoom,
                                           30.0,
                                           zoomStep,
                                           deviceMaximumZoom,
                                           -1,
                                           &targetZoom));
    }

    QVERIFY(Mt11ZoomPolicy::alignedDisplayTarget(5.6,
                                                  zoomStep,
                                                  deviceMaximumZoom,
                                                  0,
                                                  &targetZoom));
    QCOMPARE(targetZoom, 6.0);
    QVERIFY(ZoomStepPolicy::isAlignedZoom(
        targetZoom, zoomStep, 1.0, deviceMaximumZoom));
    QVERIFY(Mt11ZoomPolicy::alignedDisplayTarget(5.5,
                                                  zoomStep,
                                                  deviceMaximumZoom,
                                                  -1,
                                                  &targetZoom));
    QCOMPARE(targetZoom, 5.0);
    QVERIFY(Mt11ZoomPolicy::alignedDisplayTarget(5.5,
                                                  zoomStep,
                                                  deviceMaximumZoom,
                                                  1,
                                                  &targetZoom));
    QCOMPARE(targetZoom, 6.0);

    // The device-reported 165.1x physical ceiling is deliberately not a
    // display endpoint. With a 1.0x step the last published stop is 165.0x.
    QVERIFY(Mt11ZoomPolicy::isDisplayTarget(
        165.0, zoomStep, deviceMaximumZoom));
    QVERIFY(!Mt11ZoomPolicy::isDisplayTarget(
        165.1, zoomStep, deviceMaximumZoom));
    QVERIFY(Mt11ZoomPolicy::alignedDisplayTarget(165.1,
                                                  zoomStep,
                                                  deviceMaximumZoom,
                                                  0,
                                                  &targetZoom));
    QCOMPARE(targetZoom, 165.0);

    // The 30x protocol boundary is the sole extra endpoint in an otherwise
    // minimum-anchored MT11 grid. The off-grid 165.1x physical endpoint is
    // still excluded when the configured step does not land on it.
    QVERIFY(Mt11ZoomPolicy::isDisplayTarget(29.0, 2.0, 30.0));
    QVERIFY(Mt11ZoomPolicy::isDisplayTarget(30.0, 2.0, 30.0));
    QVERIFY(Mt11ZoomPolicy::alignedDisplayTarget(
        30.0, 2.0, 30.0, 1, &targetZoom));
    QCOMPARE(targetZoom, 30.0);
    QVERIFY(Mt11ZoomPolicy::isDisplayTarget(
        30.0, 2.0, deviceMaximumZoom));
    QVERIFY(Mt11ZoomPolicy::alignedDisplayTarget(
        30.0, 2.0, deviceMaximumZoom, 0, &targetZoom));
    QCOMPARE(targetZoom, 30.0);
    QVERIFY(!Mt11ZoomPolicy::isDisplayTarget(
        165.1, 2.0, deviceMaximumZoom));
    QVERIFY(Mt11ZoomPolicy::isDisplayTarget(
        165.1, 0.1, deviceMaximumZoom));
    QVERIFY(Mt11ZoomPolicy::alignedDisplayTarget(
        165.1, 0.1, deviceMaximumZoom, 0, &targetZoom));
    QCOMPARE(targetZoom, 165.1);

}

void Mt11ProtocolTest::cameraAndFunctionPayloads()
{
    Mt11Protocol::CameraSystemStatus status;
    QVERIFY(Mt11Protocol::parseCameraSystemStatusPayload(
        QByteArray::fromHex("0001000101020001"), &status));
    QCOMPARE(status.hdrStatus, quint8(1));
    QCOMPARE(status.recordingStatus, quint8(1));
    QCOMPARE(status.gimbalMotionMode, quint8(1));
    QCOMPARE(status.gimbalMountingDirection, quint8(2));
    QCOMPARE(status.videoOutputStatus, quint8(0));
    QCOMPARE(status.zoomLinkage, quint8(1));
    QVERIFY(!Mt11Protocol::parseCameraSystemStatusPayload(
        QByteArray::fromHex("00010001010200"), &status));

    for (quint8 type = 0; type <= 6; ++type) {
        quint8 parsedType = 0xff;
        QVERIFY(Mt11Protocol::parseFunctionFeedbackPayload(
            QByteArray(1, static_cast<char>(type)), &parsedType));
        QCOMPARE(parsedType, type);
    }
    quint8 parsedType = 0;
    QVERIFY(!Mt11Protocol::parseFunctionFeedbackPayload(
        QByteArray::fromHex("07"), &parsedType));
}

void Mt11ProtocolTest::videoModePayloads()
{
    Mt11Protocol::VideoMode mode;
    QVERIFY(Mt11Protocol::parseVideoModePayload(
        QByteArray::fromHex("0002"), &mode));
    QCOMPARE(mode.mainStream, quint8(Mt11Protocol::VideoSourceZoom));
    QCOMPARE(mode.subStream, quint8(Mt11Protocol::VideoSourceThermal));
    QVERIFY(!mode.thermalOnMainStream());

    QVERIFY(Mt11Protocol::parseVideoModePayload(
        QByteArray::fromHex("0200"), &mode));
    QVERIFY(mode.thermalOnMainStream());
    QVERIFY(Mt11Protocol::parseVideoModePayload(
        QByteArray::fromHex("0302"), &mode));
    QVERIFY(!mode.thermalOnMainStream());
    QVERIFY(!Mt11Protocol::parseVideoModePayload(
        QByteArray::fromHex("0600"), &mode));
    QVERIFY(!Mt11Protocol::parseVideoModePayload(
        QByteArray::fromHex("0003"), &mode));
}

QTEST_APPLESS_MAIN(Mt11ProtocolTest)

#include "Mt11ProtocolTest.moc"
