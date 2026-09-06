/****************************************************************************
 *
 * Flight-controller heading freshness and source-selection regression tests.
 *
 ****************************************************************************/

#include <QtTest/QTest>
#include <cmath>
#include <limits>

#include "GimbalHeadingTelemetry.h"

class GimbalHeadingTelemetryTest : public QObject {
    Q_OBJECT

   private slots:
    void startsWithoutMeasuredNorth();
    void preservesFractionalHeadingAndWrapsNorth();
    void sourcesExpireIndependently();
    void delayedCrossSourcePacketsDoNotReplaceNewerMeasurements();
    void equalMeasurementTimesPreferQuaternion();
    void invalidPacketsDoNotRenewHeading();
    void rejectsReceiveTimeRollback();
    void rejectsDuplicateBootTime();
    void ignoresSmallBootTimeRollback();
    void restartClearsOtherSources();
    void acceptsBootTimeWrap();
    void clearRemovesAllSources();
};

void GimbalHeadingTelemetryTest::startsWithoutMeasuredNorth() {
    GimbalHeadingTelemetry telemetry;
    QVERIFY(!telemetry.heading(0).valid);
    QVERIFY(!telemetry.update(GimbalHeadingTelemetry::Source::None, 10.0, 0));
    QVERIFY(telemetry.update(GimbalHeadingTelemetry::Source::Attitude, 0.0, 0));
    const auto heading = telemetry.heading(0);
    QVERIFY(heading.valid);
    QCOMPARE(heading.yawDegrees, 0.0);
}

void GimbalHeadingTelemetryTest::preservesFractionalHeadingAndWrapsNorth() {
    GimbalHeadingTelemetry telemetry;
    QVERIFY(telemetry.update(GimbalHeadingTelemetry::Source::Attitude, 45.625, 10));
    QCOMPARE(telemetry.heading(10).yawDegrees, 45.625);
    QVERIFY(telemetry.update(GimbalHeadingTelemetry::Source::Attitude, -0.125, 20));
    QCOMPARE(telemetry.heading(20).yawDegrees, 359.875);
    QVERIFY(telemetry.update(GimbalHeadingTelemetry::Source::Attitude, 360.125, 30));
    QCOMPARE(telemetry.heading(30).yawDegrees, 0.125);
}

void GimbalHeadingTelemetryTest::sourcesExpireIndependently() {
    GimbalHeadingTelemetry telemetry;
    QVERIFY(telemetry.update(GimbalHeadingTelemetry::Source::Quaternion, 10.25, 100, 100));
    QVERIFY(telemetry.update(GimbalHeadingTelemetry::Source::Attitude, 20.5, 1900, 1900));
    QVERIFY(telemetry.update(GimbalHeadingTelemetry::Source::HighLatency, 30.0, 2050));
    const auto newestHeading = telemetry.heading(2100);
    QCOMPARE(newestHeading.source, GimbalHeadingTelemetry::Source::Attitude);
    QCOMPARE(newestHeading.yawDegrees, 20.5);
    QCOMPARE(newestHeading.ageMs, std::int64_t{200});

    // Recent ATTITUDE packets cannot freshen a stale quaternion, unlike the
    // previous single timestamp paired with the UI's permanently preferred q.
    const auto attitudeHeading = telemetry.heading(2101);
    QCOMPARE(attitudeHeading.source, GimbalHeadingTelemetry::Source::Attitude);
    QCOMPARE(attitudeHeading.yawDegrees, 20.5);
    QCOMPARE(attitudeHeading.receivedAtMs, std::int64_t{1900});
    QCOMPARE(telemetry.heading(3901).source, GimbalHeadingTelemetry::Source::HighLatency);
    QVERIFY(!telemetry.heading(4051).valid);
}

void GimbalHeadingTelemetryTest::delayedCrossSourcePacketsDoNotReplaceNewerMeasurements() {
    GimbalHeadingTelemetry telemetry;
    QVERIFY(telemetry.update(GimbalHeadingTelemetry::Source::Attitude, 70.5, 100, 5000));
    QVERIFY(telemetry.update(GimbalHeadingTelemetry::Source::Quaternion, 40.25, 150, 4900));
    QCOMPARE(telemetry.heading(150).yawDegrees, 70.5);
    QVERIFY(telemetry.update(GimbalHeadingTelemetry::Source::Quaternion, 80.25, 200, 5100));
    QCOMPARE(telemetry.heading(200).yawDegrees, 80.25);
    QVERIFY(telemetry.update(GimbalHeadingTelemetry::Source::Attitude, 85.75, 210, 5110));
    QCOMPARE(telemetry.heading(210).yawDegrees, 85.75);
}

void GimbalHeadingTelemetryTest::equalMeasurementTimesPreferQuaternion() {
    GimbalHeadingTelemetry telemetry;
    QVERIFY(telemetry.update(GimbalHeadingTelemetry::Source::Quaternion, 70.25, 100, 5000));
    QVERIFY(telemetry.update(GimbalHeadingTelemetry::Source::Attitude, 70.5, 150, 5000));
    QCOMPARE(telemetry.heading(150).source, GimbalHeadingTelemetry::Source::Quaternion);
    QCOMPARE(telemetry.heading(150).ageMs, std::int64_t{50});
}

void GimbalHeadingTelemetryTest::invalidPacketsDoNotRenewHeading() {
    GimbalHeadingTelemetry telemetry;
    QVERIFY(telemetry.update(GimbalHeadingTelemetry::Source::Attitude, 35.125, 10, 5000));
    QVERIFY(
        !telemetry.update(GimbalHeadingTelemetry::Source::Attitude, std::numeric_limits<double>::quiet_NaN(), 1900, 0));
    QVERIFY(!telemetry.update(GimbalHeadingTelemetry::Source::Attitude, std::numeric_limits<double>::infinity(), 2000));
    QVERIFY(telemetry.heading(2010).valid);
    QVERIFY(!telemetry.heading(2011).valid);
}

void GimbalHeadingTelemetryTest::rejectsReceiveTimeRollback() {
    GimbalHeadingTelemetry telemetry;
    QVERIFY(!telemetry.update(GimbalHeadingTelemetry::Source::Attitude, 40.0, -1));
    QVERIFY(telemetry.update(GimbalHeadingTelemetry::Source::Attitude, 45.0, 100));
    QVERIFY(!telemetry.update(GimbalHeadingTelemetry::Source::Attitude, 50.0, 99));
    QVERIFY(!telemetry.heading(99).valid);
    QCOMPARE(telemetry.heading(100).yawDegrees, 45.0);
}

void GimbalHeadingTelemetryTest::rejectsDuplicateBootTime() {
    GimbalHeadingTelemetry telemetry;
    QVERIFY(telemetry.update(GimbalHeadingTelemetry::Source::Attitude, 40.0, 0, 5000));
    QVERIFY(!telemetry.update(GimbalHeadingTelemetry::Source::Attitude, 50.0, 1999, 5000));
    QVERIFY(!telemetry.heading(2001).valid);
}

void GimbalHeadingTelemetryTest::ignoresSmallBootTimeRollback() {
    GimbalHeadingTelemetry telemetry;
    QVERIFY(telemetry.update(GimbalHeadingTelemetry::Source::Attitude, 40.0, 0, 5000));
    QVERIFY(!telemetry.update(GimbalHeadingTelemetry::Source::Attitude, 50.0, 1500, 4999));
    QVERIFY(!telemetry.update(GimbalHeadingTelemetry::Source::Attitude, 60.0, 1999, 4000));
    QCOMPARE(telemetry.heading(2000).yawDegrees, 40.0);
    QVERIFY(!telemetry.heading(2001).valid);
}

void GimbalHeadingTelemetryTest::restartClearsOtherSources() {
    GimbalHeadingTelemetry telemetry;
    QVERIFY(telemetry.update(GimbalHeadingTelemetry::Source::Quaternion, 70.0, 10, 5010));
    QVERIFY(telemetry.update(GimbalHeadingTelemetry::Source::Attitude, 71.0, 11, 5011));
    QVERIFY(telemetry.update(GimbalHeadingTelemetry::Source::HighLatency, 72.0, 12));
    QVERIFY(telemetry.update(GimbalHeadingTelemetry::Source::Attitude, 1.25, 20, 0));
    const auto heading = telemetry.heading(20);
    QCOMPARE(heading.source, GimbalHeadingTelemetry::Source::Attitude);
    QCOMPARE(heading.yawDegrees, 1.25);
    QVERIFY(!telemetry.heading(2021).valid);
}

void GimbalHeadingTelemetryTest::acceptsBootTimeWrap() {
    GimbalHeadingTelemetry telemetry;
    QVERIFY(telemetry.update(GimbalHeadingTelemetry::Source::Quaternion, 90.0, 0, 4294967290U));
    QVERIFY(telemetry.update(GimbalHeadingTelemetry::Source::Attitude, 95.0, 5, 4294967295U));
    QVERIFY(telemetry.update(GimbalHeadingTelemetry::Source::Attitude, 96.25, 10, 4));
    QCOMPARE(telemetry.heading(10).source, GimbalHeadingTelemetry::Source::Attitude);
    QCOMPARE(telemetry.heading(2001).yawDegrees, 96.25);
}

void GimbalHeadingTelemetryTest::clearRemovesAllSources() {
    GimbalHeadingTelemetry telemetry;
    QVERIFY(telemetry.update(GimbalHeadingTelemetry::Source::Quaternion, 10.0, 10, 5000));
    QVERIFY(telemetry.update(GimbalHeadingTelemetry::Source::Attitude, 20.0, 20, 5010));
    QVERIFY(telemetry.update(GimbalHeadingTelemetry::Source::HighLatency, 30.0, 30));
    telemetry.clear();
    QVERIFY(!telemetry.heading(31).valid);
    QVERIFY(telemetry.update(GimbalHeadingTelemetry::Source::Quaternion, 15.5, 40, 0));
    QCOMPARE(telemetry.heading(40).yawDegrees, 15.5);
}

QTEST_GUILESS_MAIN(GimbalHeadingTelemetryTest)

#include "GimbalHeadingTelemetryTest.moc"
