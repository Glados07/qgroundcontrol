#include "A8RtspRecoveryPolicy.h"

#include <QtTest/QTest>

namespace {
using Policy = A8RtspRecoveryPolicy;
using Action = Policy::Action;
const QString uri = QStringLiteral("rtsp://192.168.144.25:8554/main.264");

quint64 ntp(quint32 seconds) { return quint64(seconds) << 32; }

Policy::Progress stalled(qint64 lastMs = 1000)
{
    return {100, 50, lastMs, lastMs, -1};
}

void ready(Policy &policy, quint64 generation = 1, qint64 now = 0)
{
    policy.begin(uri, generation, now);
    policy.displayed(generation, now);
}

QByteArray report()
{
    // V2, SR, 28 bytes, SSRC 0x31e082e6, NTP 2208988969.0, RTP 1000.
    return QByteArray::fromHex("80c8000631e082e683aa832900000000000003e80000000100000002");
}
}

class A8RtspRecoveryPolicyTest : public QObject
{
    Q_OBJECT
private slots:
    void endpointIsolation();
    void clockArithmetic();
    void senderReports();
    void malformedReports_data();
    void malformedReports();
    void waitsForRealProgress();
    void continuousFramesNeverRestart();
    void clockReportAloneDoesNotRestart();
    void observedLogStall();
    void jitterStallIsNotDecoderFailure();
    void singleDecisionPerGeneration();
    void budgetSurvivesReconnection();
    void uriChangeResetsBudget();
    void lifecycleGates();
    void observationsFromFutureIgnored();
};

void A8RtspRecoveryPolicyTest::endpointIsolation()
{
    const QString a8 = QStringLiteral("192.168.144.25");
    const QString mt11 = QStringLiteral("192.168.144.24");
    QVERIFY(Policy::matches(uri, a8, mt11));
    QVERIFY(Policy::matches(QStringLiteral("RTSP://192.168.144.25:8554/sub"), a8, mt11));
    QVERIFY(!Policy::matches(QStringLiteral("rtsp://192.168.144.24:8554/main.264"), a8, mt11));
    QVERIFY(!Policy::matches(uri, a8, a8));
    QVERIFY(!Policy::matches(uri, QString(), mt11));
    QVERIFY(!Policy::matches(QStringLiteral("udp://192.168.144.25:8554"), a8, mt11));
    QVERIFY(!Policy::matches(QStringLiteral("rtsp://192.168.144.25.evil/main.264"), a8, mt11));
    QVERIFY(Policy::matches(QStringLiteral("rtsps://[2001:db8::8]/video"),
                            QStringLiteral("[2001:DB8::8]"), QStringLiteral("2001:db8::11")));
}

void A8RtspRecoveryPolicyTest::clockArithmetic()
{
    QVERIFY(!Policy::clockJump(ntp(2208988801U), 1000, 0, ntp(2208988806U), 451000, 5000));
    QVERIFY(!Policy::clockJump(ntp(3998103200U), 0xfffff000U, 0,
                               ntp(3998103201U), quint32(0xfffff000U + 90000U), 1000));
    QVERIFY(Policy::clockJump(ntp(2208988969U), 5254745950ULL & 0xffffffffU, 0,
                              ntp(3998103284U), 7219159218ULL & 0xffffffffU, 2710));
    QVERIFY(Policy::clockJump(ntp(3998103200U), 1000, 0, ntp(3998103100U), 100000, 1000));
    QVERIFY(Policy::clockJump(ntp(3998103200U), 1000, 0, ntp(3998103201U), 9000000, 1000));
    QVERIFY(!Policy::clockJump(0, 1000, 0, ntp(3998103201U), 9000000, 1000));
    QVERIFY(!Policy::clockJump(ntp(3998103200U), 1000, 2000, ntp(3998103201U), 9000000, 1000));
    QVERIFY(!Policy::clockJump(ntp(3998103200U), 1000, 0, ntp(3998103300U), 9000000, 60000));
}

void A8RtspRecoveryPolicyTest::senderReports()
{
    auto bytes = report();
    Policy::SenderReport parsed;
    QVERIFY(Policy::senderReport(reinterpret_cast<const unsigned char *>(bytes.constData()), bytes.size(),
                                 0x31e082e6, parsed));
    QCOMPARE(parsed.rtp, quint32(1000));
    QCOMPARE(parsed.ntp, quint64(0x83aa8329) << 32);
    bytes += QByteArray::fromHex("80c9000131e082e6"); // Compound SR + RR.
    QVERIFY(Policy::senderReport(reinterpret_cast<const unsigned char *>(bytes.constData()), bytes.size(),
                                 0x31e082e6, parsed));
    QVERIFY(!Policy::senderReport(reinterpret_cast<const unsigned char *>(bytes.constData()), bytes.size(),
                                  0x31e082e7, parsed));
    bytes = report();
    bytes[0] = '\xa0';
    bytes[3] = char(7);
    bytes += QByteArray::fromHex("00000004");
    QVERIFY(Policy::senderReport(reinterpret_cast<const unsigned char *>(bytes.constData()), bytes.size(),
                                 0x31e082e6, parsed));
}

void A8RtspRecoveryPolicyTest::malformedReports_data()
{
    QTest::addColumn<QByteArray>("bytes");
    QTest::newRow("empty") << QByteArray();
    QTest::newRow("truncated-header") << QByteArray::fromHex("80c8");
    QTest::newRow("truncated-SR") << report().left(24);
    QTest::newRow("partial-tail") << (report() + QByteArray(1, '\0'));
    auto bytes = report();
    bytes[0] = '\x81';
    QTest::newRow("missing-report-block") << bytes;
    bytes = report(); bytes[0] = char(0x40);
    QTest::newRow("wrong-version") << bytes;
    bytes = report(); bytes[2] = '\xff'; bytes[3] = '\xff';
    QTest::newRow("oversized-length") << bytes;
    bytes = report(); bytes[0] = '\xa0'; bytes[27] = char(0);
    QTest::newRow("zero-padding") << bytes;
    bytes[27] = char(27);
    QTest::newRow("oversized-padding") << bytes;
    bytes = report(); bytes[0] = '\xa0'; bytes[3] = char(7);
    bytes += QByteArray::fromHex("0000000480c9000131e082e6");
    QTest::newRow("padding-not-last") << bytes;
    QTest::newRow("valid-SR-malformed-tail") << (report() + QByteArray::fromHex("00c90000"));
}

void A8RtspRecoveryPolicyTest::malformedReports()
{
    QFETCH(QByteArray, bytes);
    Policy::SenderReport parsed{123, 456};
    QVERIFY(!Policy::senderReport(reinterpret_cast<const unsigned char *>(bytes.constData()), bytes.size(),
                                  0x31e082e6, parsed));
    QCOMPARE(parsed.ntp, quint64(123));
    QCOMPARE(parsed.rtp, quint32(456));
}

void A8RtspRecoveryPolicyTest::waitsForRealProgress()
{
    Policy policy;
    policy.begin(uri, 1, 0);
    policy.displayed(2, 0); // Delayed first frame from a different generation.
    QCOMPARE(policy.evaluate(stalled(), 10000, true, false), Action::None);
    policy.displayed(1, 10000);
    QCOMPARE(policy.evaluate({}, 20000, true, false), Action::None);
    auto progress = stalled(); progress.rtpPackets = 0;
    QCOMPARE(policy.evaluate(progress, 20000, true, false), Action::None);
    progress = stalled(); progress.mediaBuffers = 0;
    QCOMPARE(policy.evaluate(progress, 20000, true, false), Action::None);
}

void A8RtspRecoveryPolicyTest::continuousFramesNeverRestart()
{
    Policy policy; ready(policy);
    for (int now = 1000; now <= 600000; now += 40) {
        auto progress = stalled(now);
        progress.lastClockJumpMs = now - 100;
        QCOMPARE(policy.evaluate(progress, now, true, false), Action::None);
    }
}

void A8RtspRecoveryPolicyTest::clockReportAloneDoesNotRestart()
{
    Policy policy; ready(policy);
    auto progress = stalled(1000);
    progress.lastClockJumpMs = 1000;
    QCOMPARE(policy.evaluate(progress, 2999, true, false), Action::None);
    progress.lastMediaMs = 3000;
    QCOMPARE(policy.evaluate(progress, 3000, true, false), Action::None);
    progress.lastMediaMs = 1000;
    progress.lastRtpMs = 3000;
    QCOMPARE(policy.evaluate(progress, 3000, true, false), Action::None);
}

void A8RtspRecoveryPolicyTest::observedLogStall()
{
    Policy policy; ready(policy);
    auto progress = stalled(24133);
    progress.lastClockJumpMs = 24156;
    QCOMPARE(policy.evaluate(progress, 26132, true, false), Action::None);
    QCOMPARE(policy.evaluate(progress, 26133, true, false), Action::ClockJumpStall);
}

void A8RtspRecoveryPolicyTest::jitterStallIsNotDecoderFailure()
{
    Policy policy; ready(policy);
    auto progress = stalled(1000);
    progress.lastRtpMs = 7000; // RTP arrives, but depay/parser produces no media.
    QCOMPARE(policy.evaluate(progress, 6999, true, false), Action::None);
    QCOMPARE(policy.evaluate(progress, 7000, true, false), Action::MediaStall);
    // Sink progress is deliberately not an input: a stalled display with
    // fresh compressed media cannot cause this policy to restart the source.
}

void A8RtspRecoveryPolicyTest::singleDecisionPerGeneration()
{
    Policy policy; ready(policy);
    QCOMPARE(policy.evaluate(stalled(), 7000, true, false), Action::MediaStall);
    QCOMPARE(policy.evaluate(stalled(), 10000, true, false), Action::None);
    policy.stop();
    QCOMPARE(policy.evaluate(stalled(), 20000, true, false), Action::None);
}

void A8RtspRecoveryPolicyTest::budgetSurvivesReconnection()
{
    Policy policy; ready(policy);
    QCOMPARE(policy.evaluate(stalled(), 7000, true, false), Action::MediaStall);
    policy.stop(); ready(policy, 2, 8000);
    QCOMPARE(policy.evaluate(stalled(9000), 15000, true, false), Action::MediaStall);
    policy.stop(); ready(policy, 3, 16000);
    QCOMPARE(policy.evaluate(stalled(17000), 23000, true, false), Action::RateLimited);
    QCOMPARE(policy.evaluate(stalled(17000), 24000, true, false), Action::None);
    QCOMPARE(policy.evaluate(stalled(17000), 67000, true, false), Action::MediaStall);
}

void A8RtspRecoveryPolicyTest::uriChangeResetsBudget()
{
    Policy policy; ready(policy);
    QCOMPARE(policy.evaluate(stalled(), 7000, true, false), Action::MediaStall);
    ready(policy, 2, 8000);
    QCOMPARE(policy.evaluate(stalled(9000), 15000, true, false), Action::MediaStall);
    policy.begin(uri + QStringLiteral("?new=1"), 3, 16000);
    policy.displayed(3, 16000);
    QCOMPARE(policy.evaluate(stalled(17000), 23000, true, false), Action::MediaStall);
}

void A8RtspRecoveryPolicyTest::lifecycleGates()
{
    Policy policy; ready(policy);
    QCOMPARE(policy.evaluate(stalled(), 10000, false, false), Action::None);
    QCOMPARE(policy.evaluate(stalled(), 15999, true, false), Action::None);
    QCOMPARE(policy.evaluate(stalled(), 16000, true, true), Action::None);
    QCOMPARE(policy.evaluate(stalled(), 21999, true, false), Action::None);
    QCOMPARE(policy.evaluate(stalled(), 22000, true, false), Action::MediaStall);
}

void A8RtspRecoveryPolicyTest::observationsFromFutureIgnored()
{
    Policy policy; ready(policy, 1, 1000);
    QCOMPARE(policy.evaluate(stalled(), 0, true, false), Action::None);
    QCOMPARE(policy.evaluate(stalled(10000), 9000, true, false), Action::None);
}

QTEST_APPLESS_MAIN(A8RtspRecoveryPolicyTest)
#include "A8RtspRecoveryPolicyTest.moc"
