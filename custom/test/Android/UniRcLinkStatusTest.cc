#include "UniRcLinkStatus.h"

#include <QtCore/QElapsedTimer>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>

namespace {
UniRcProtocol::LinkInfo linkInfo(int strength, int quality)
{
    UniRcProtocol::LinkInfo info;
    info.channel = 1;
    info.signalStrength = static_cast<qint16>(strength);
    info.signalQuality = static_cast<qint8>(quality);
    return info;
}
}

class UniRcLinkStatusTest : public QObject
{
    Q_OBJECT

private slots:
    void validZeroDiffersFromUnavailable();
    void strengthLevels_data();
    void strengthLevels();
    void qualityLevels_data();
    void qualityLevels();
    void invalidateAndRecover();
    void expiresAfterThreeSeconds();
    void repeatedSampleRefreshesExpiry();
    void invalidSamplesKeepPreviousStatus();
};

void UniRcLinkStatusTest::validZeroDiffersFromUnavailable()
{
    UniRcLinkStatus status;
    QSignalSpy changed(&status, &UniRcLinkStatus::statusChanged);
    QVERIFY(!status.available());
    QCOMPARE(status.strengthPercent(), 0);
    QCOMPARE(status.strengthLevel(), 0);
    status.update(linkInfo(0, -15));
    QVERIFY(status.available());
    QCOMPARE(status.strengthPercent(), 0);
    QCOMPARE(status.strengthLevel(), 0);
    QCOMPARE(status.signalQuality(), -15);
    QCOMPARE(status.qualityLevel(), 0);
    QCOMPARE(changed.count(), 1);
}

void UniRcLinkStatusTest::strengthLevels_data()
{
    QTest::addColumn<int>("percent");
    QTest::addColumn<int>("level");
    QTest::newRow("zero") << 0 << 0;
    QTest::newRow("below-one-bar") << 19 << 0;
    QTest::newRow("one-bar") << 20 << 1;
    QTest::newRow("below-two-bars") << 39 << 1;
    QTest::newRow("two-bars") << 40 << 2;
    QTest::newRow("below-three-bars") << 59 << 2;
    QTest::newRow("three-bars") << 60 << 3;
    QTest::newRow("below-four-bars") << 79 << 3;
    QTest::newRow("four-bars") << 80 << 4;
    QTest::newRow("below-five-bars") << 99 << 4;
    QTest::newRow("five-bars") << 100 << 5;
}

void UniRcLinkStatusTest::strengthLevels()
{
    QFETCH(int, percent);
    QFETCH(int, level);
    UniRcLinkStatus status;
    status.update(linkInfo(percent, 10));
    QVERIFY(status.available());
    QCOMPARE(status.strengthPercent(), percent);
    QCOMPARE(status.strengthLevel(), level);
}

void UniRcLinkStatusTest::qualityLevels_data()
{
    QTest::addColumn<int>("quality");
    QTest::addColumn<int>("level");
    QTest::newRow("minimum") << -15 << 0;
    QTest::newRow("negative") << -1 << 0;
    QTest::newRow("zero") << 0 << 0;
    QTest::newRow("below-medium") << 4 << 0;
    QTest::newRow("medium-boundary") << 5 << 1;
    QTest::newRow("below-strong") << 9 << 1;
    QTest::newRow("strong-boundary") << 10 << 2;
    QTest::newRow("maximum") << 30 << 2;
}

void UniRcLinkStatusTest::qualityLevels()
{
    QFETCH(int, quality);
    QFETCH(int, level);
    UniRcLinkStatus status;
    status.update(linkInfo(80, quality));
    QCOMPARE(status.signalQuality(), quality);
    QCOMPARE(status.qualityLevel(), level);
}

void UniRcLinkStatusTest::invalidateAndRecover()
{
    UniRcLinkStatus status;
    QSignalSpy changed(&status, &UniRcLinkStatus::statusChanged);
    status.invalidate();
    QCOMPARE(changed.count(), 0);
    status.update(linkInfo(100, 30));
    status.invalidate();
    QVERIFY(!status.available());
    QCOMPARE(status.strengthPercent(), 0);
    QCOMPARE(status.strengthLevel(), 0);
    QCOMPARE(status.signalQuality(), 0);
    QCOMPARE(status.qualityLevel(), 0);
    QCOMPARE(changed.count(), 2);
    status.invalidate();
    QCOMPARE(changed.count(), 2);
    status.update(linkInfo(40, 5));
    QVERIFY(status.available());
    QCOMPARE(status.strengthLevel(), 2);
    QCOMPARE(status.qualityLevel(), 1);
    QCOMPARE(changed.count(), 3);
}

void UniRcLinkStatusTest::expiresAfterThreeSeconds()
{
    UniRcLinkStatus status;
    QSignalSpy changed(&status, &UniRcLinkStatus::statusChanged);
    QElapsedTimer elapsed;
    elapsed.start();
    status.update(linkInfo(60, 10));
    QTest::qWait(2800);
    QVERIFY(status.available());
    QTRY_VERIFY_WITH_TIMEOUT(!status.available(), 1000);
    QVERIFY(elapsed.elapsed() >= 3000);
    QCOMPARE(status.strengthPercent(), 0);
    QCOMPARE(changed.count(), 2);
}

void UniRcLinkStatusTest::repeatedSampleRefreshesExpiry()
{
    UniRcLinkStatus status;
    QSignalSpy changed(&status, &UniRcLinkStatus::statusChanged);
    const auto repeated = linkInfo(80, 10);
    status.update(repeated);
    QTest::qWait(2000);
    status.update(repeated);
    QCOMPARE(changed.count(), 1);
    QTest::qWait(1200);
    QVERIFY(status.available()); // More than 3 s since the first sample.
    QTRY_VERIFY_WITH_TIMEOUT(!status.available(), 2500);
    QCOMPARE(changed.count(), 2);
}

void UniRcLinkStatusTest::invalidSamplesKeepPreviousStatus()
{
    UniRcLinkStatus status;
    QSignalSpy changed(&status, &UniRcLinkStatus::statusChanged);
    status.update(linkInfo(-1, 10));
    QVERIFY(!status.available());
    status.update(linkInfo(60, 5));
    status.update(linkInfo(101, 10));
    status.update(linkInfo(80, -16));
    status.update(linkInfo(80, 31));
    QVERIFY(status.available());
    QCOMPARE(status.strengthPercent(), 60);
    QCOMPARE(status.signalQuality(), 5);
    QCOMPARE(changed.count(), 1);
}

QTEST_GUILESS_MAIN(UniRcLinkStatusTest)

#include "UniRcLinkStatusTest.moc"
