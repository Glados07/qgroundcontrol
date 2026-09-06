/****************************************************************************
 *
 * Real-provider frame conversion, message ordering and freshness tests.
 *
 ****************************************************************************/

#include <QtCore/QLoggingCategory>
#include <QtTest/QTest>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include "GimbalAzimuthProvider.h"
#include "TestDoubles.h"

namespace {

constexpr double kDegreesToRadians = 3.14159265358979323846 / 180.0;

std::array<float, 4> yawQuaternion(double yawDegrees) {
    const double halfYaw = yawDegrees * kDegreesToRadians * 0.5;
    return {static_cast<float>(std::cos(halfYaw)), 0.0F, 0.0F, static_cast<float>(std::sin(halfYaw))};
}

quint32 nextBootMs() {
    static quint32 bootMs = 10000;
    return ++bootMs;
}

mavlink_message_t headingMessage(double yawDegrees, quint8 system = 1, quint8 component = 1) {
    mavlink_attitude_t attitude{};
    attitude.time_boot_ms = nextBootMs();
    attitude.yaw = static_cast<float>(yawDegrees * kDegreesToRadians);
    mavlink_message_t message{};
    mavlink_msg_attitude_encode(system, component, &message, &attitude);
    return message;
}

mavlink_message_t quaternionHeadingMessage(double yawDegrees, double displayOffsetDegrees = 0.0) {
    const auto q = yawQuaternion(yawDegrees);
    const auto offset = yawQuaternion(displayOffsetDegrees);
    mavlink_attitude_quaternion_t attitude{};
    attitude.time_boot_ms = nextBootMs();
    attitude.q1 = q[0];
    attitude.q2 = q[1];
    attitude.q3 = q[2];
    attitude.q4 = q[3];
    for (std::size_t i = 0; i < offset.size(); ++i) {
        attitude.repr_offset_q[i] = offset[i];
    }
    mavlink_message_t message{};
    mavlink_msg_attitude_quaternion_encode(1, 1, &message, &attitude);
    return message;
}

mavlink_message_t gimbalMessage(double yawDegrees, quint16 flags = 28, quint8 system = 1, quint8 component = 154,
                                quint8 deviceId = 0) {
    const auto q = yawQuaternion(yawDegrees);
    mavlink_gimbal_device_attitude_status_t attitude{};
    attitude.time_boot_ms = nextBootMs();
    attitude.flags = flags;
    attitude.gimbal_device_id = deviceId;
    for (std::size_t i = 0; i < q.size(); ++i) {
        attitude.q[i] = q[i];
    }
    mavlink_message_t message{};
    mavlink_msg_gimbal_device_attitude_status_encode(system, component, &message, &attitude);
    return message;
}

bool azimuthEquals(const GimbalAzimuthProvider &provider, double expected) {
    return provider.valid() && std::abs(GimbalAzimuthPolicy::wrap180(provider.absoluteYaw() - expected)) < 0.0001;
}

}  // namespace

class GimbalAzimuthProviderTest : public QObject {
    Q_OBJECT
   private slots:
    void initTestCase() {
        QLoggingCategory::setFilterRules(
            QStringLiteral("qgc.custom.gimbal.azimuth.debug=false\n"
                           "qgc.custom.gimbal.azimuth.info=false"));
    }
    void cleanup() { MultiVehicleManager::instance()->setActiveVehicle(nullptr); }
    void followsAndLocksWithoutChangingConfiguredFrame();
    void usesUnroundedHeadingAndRefreshesOnHeadingArrival();
    void settingChangesRecalculateExistingSample();
    void explicitFramesOverrideLegacySetting();
    void isolatesVehicleComponentAndDeviceRoutes();
    void highLatencyUnits_data();
    void highLatencyUnits();
    void quaternionIgnoresDisplayOffset();
    void invalidTelemetryDoesNotRenewFreshness();
    void gimbalExpiryIsIndependentOfHeading();
    void reconnectRequiresNewHeadingAndGimbal();
    void ignoresReplayedGimbalSamplesButAcceptsZeroTimestamps();
};

void GimbalAzimuthProviderTest::followsAndLocksWithoutChangingConfiguredFrame() {
    Vehicle vehicle;
    MultiVehicleManager::instance()->setActiveVehicle(&vehicle);
    Fact legacyReference{1};
    GimbalAzimuthProvider provider(&legacyReference);

    provider.handleMavlinkMessage(&vehicle, headingMessage(45.0));
    provider.handleMavlinkMessage(&vehicle, gimbalMessage(-171.738, 12));
    QVERIFY(azimuthEquals(provider, -126.738));
    provider.handleMavlinkMessage(&vehicle, gimbalMessage(-171.387, 28));
    QVERIFY(azimuthEquals(provider, -126.387));

    // Dense rotation used to defeat movement-threshold frame inference.
    // For each paired sample, a locked camera remains at the same world yaw.
    for (int step = 0; step <= 360; ++step) {
        const double heading = 45.0 + step * 0.5;
        provider.handleMavlinkMessage(&vehicle, headingMessage(heading));
        provider.handleMavlinkMessage(&vehicle, gimbalMessage(-126.387 - heading, 28));
        QVERIFY(azimuthEquals(provider, -126.387));
    }
    // A yaw command in lock mode must still move the displayed azimuth.
    provider.handleMavlinkMessage(&vehicle, gimbalMessage(-111.387 - 225.0, 28));
    QVERIFY(azimuthEquals(provider, -111.387));

    for (int step = 0; step <= 90; ++step) {
        const double heading = 225.0 + step * 0.5;
        provider.handleMavlinkMessage(&vehicle, headingMessage(heading));
        provider.handleMavlinkMessage(&vehicle, gimbalMessage(20.0, 12));
        QVERIFY(azimuthEquals(provider, heading + 20.0));
    }
}

void GimbalAzimuthProviderTest::usesUnroundedHeadingAndRefreshesOnHeadingArrival() {
    Vehicle vehicle;
    MultiVehicleManager::instance()->setActiveVehicle(&vehicle);
    Fact legacyReference{1};
    GimbalAzimuthProvider provider(&legacyReference);
    QCOMPARE(vehicle.heading()->rawValue().toDouble(), 45.0);
    provider.handleMavlinkMessage(&vehicle, gimbalMessage(10.0));
    QVERIFY(!provider.valid());
    provider.handleMavlinkMessage(&vehicle, headingMessage(45.625));
    QVERIFY(azimuthEquals(provider, 55.625));
    provider.handleMavlinkMessage(&vehicle, headingMessage(46.125));
    QVERIFY(azimuthEquals(provider, 56.125));
}

void GimbalAzimuthProviderTest::settingChangesRecalculateExistingSample() {
    Vehicle vehicle;
    MultiVehicleManager::instance()->setActiveVehicle(&vehicle);
    Fact legacyReference{1};
    GimbalAzimuthProvider provider(&legacyReference);
    provider.handleMavlinkMessage(&vehicle, headingMessage(45.0));
    provider.handleMavlinkMessage(&vehicle, gimbalMessage(10.0, 28));
    QVERIFY(azimuthEquals(provider, 55.0));
    legacyReference.setRawValue(2);
    QVERIFY(azimuthEquals(provider, 10.0));
    legacyReference.setRawValue(0);
    QVERIFY(azimuthEquals(provider, 10.0));
    provider.handleMavlinkMessage(&vehicle, gimbalMessage(10.0, 12));
    QVERIFY(azimuthEquals(provider, 55.0));
    legacyReference.setRawValue(2);
    QVERIFY(azimuthEquals(provider, 10.0));
}

void GimbalAzimuthProviderTest::explicitFramesOverrideLegacySetting() {
    Vehicle vehicle;
    MultiVehicleManager::instance()->setActiveVehicle(&vehicle);
    Fact legacyReference{1};
    GimbalAzimuthProvider provider(&legacyReference);
    provider.handleMavlinkMessage(&vehicle, gimbalMessage(23.5, 28 | GIMBAL_DEVICE_FLAGS_YAW_IN_EARTH_FRAME));
    QVERIFY(azimuthEquals(provider, 23.5));
    provider.handleMavlinkMessage(&vehicle, headingMessage(70.0));
    QVERIFY(azimuthEquals(provider, 23.5));
    legacyReference.setRawValue(2);
    provider.handleMavlinkMessage(&vehicle, gimbalMessage(10.0, 28 | GIMBAL_DEVICE_FLAGS_YAW_IN_VEHICLE_FRAME));
    QVERIFY(azimuthEquals(provider, 80.0));
}

void GimbalAzimuthProviderTest::isolatesVehicleComponentAndDeviceRoutes() {
    Vehicle first(1);
    Vehicle second(2);
    Gimbal firstDevice(1, 154);
    Gimbal secondDevice(2, 154);
    first.gimbalController()->setActiveGimbal(&firstDevice);
    MultiVehicleManager::instance()->setActiveVehicle(&first);
    Fact legacyReference{1};
    GimbalAzimuthProvider provider(&legacyReference);
    provider.handleMavlinkMessage(&first, headingMessage(40.0));
    provider.handleMavlinkMessage(&first, gimbalMessage(10.0, 28, 1, 154, 1));
    QVERIFY(azimuthEquals(provider, 50.0));
    provider.handleMavlinkMessage(&first, headingMessage(100.0, 2, 1));
    provider.handleMavlinkMessage(&first, headingMessage(100.0, 1, 154));
    provider.handleMavlinkMessage(&first, gimbalMessage(90.0, 28, 2, 154, 1));
    provider.handleMavlinkMessage(&first, gimbalMessage(90.0, 28, 1, 155, 1));
    QVERIFY(azimuthEquals(provider, 50.0));
    provider.handleMavlinkMessage(&first, gimbalMessage(-10.0, 28, 1, 154, 2));
    QVERIFY(azimuthEquals(provider, 50.0));
    first.gimbalController()->setActiveGimbal(&secondDevice);
    QVERIFY(azimuthEquals(provider, 30.0));
    provider.handleMavlinkMessage(&second, headingMessage(100.0, 2));
    provider.handleMavlinkMessage(&second, gimbalMessage(5.0, 28, 2));
    QVERIFY(azimuthEquals(provider, 30.0));
    MultiVehicleManager::instance()->setActiveVehicle(&second);
    QVERIFY(azimuthEquals(provider, 105.0));
    MultiVehicleManager::instance()->setActiveVehicle(&first);
    QVERIFY(azimuthEquals(provider, 30.0));
    first.gimbalController()->setActiveGimbal(&firstDevice);
    QVERIFY(azimuthEquals(provider, 50.0));
}

void GimbalAzimuthProviderTest::highLatencyUnits_data() {
    QTest::addColumn<bool>("version2");
    QTest::addColumn<double>("expectedHeading");
    QTest::newRow("high-latency-centidegrees") << false << 45.62;
    QTest::newRow("high-latency-2-half-degrees") << true << 46.0;
}

void GimbalAzimuthProviderTest::highLatencyUnits() {
    QFETCH(bool, version2);
    QFETCH(double, expectedHeading);
    Vehicle vehicle;
    MultiVehicleManager::instance()->setActiveVehicle(&vehicle);
    Fact legacyReference{1};
    GimbalAzimuthProvider provider(&legacyReference);
    mavlink_message_t message{};
    if (version2) {
        mavlink_high_latency2_t status{};
        status.heading = 23;
        mavlink_msg_high_latency2_encode(1, 1, &message, &status);
    } else {
        mavlink_high_latency_t status{};
        status.heading = 4562;
        mavlink_msg_high_latency_encode(1, 1, &message, &status);
    }
    provider.handleMavlinkMessage(&vehicle, message);
    provider.handleMavlinkMessage(&vehicle, gimbalMessage(10.0));
    QVERIFY(azimuthEquals(provider, expectedHeading + 10.0));
}

void GimbalAzimuthProviderTest::quaternionIgnoresDisplayOffset() {
    Vehicle vehicle;
    MultiVehicleManager::instance()->setActiveVehicle(&vehicle);
    Fact legacyReference{1};
    GimbalAzimuthProvider provider(&legacyReference);
    provider.handleMavlinkMessage(&vehicle, headingMessage(135.0));
    provider.handleMavlinkMessage(&vehicle, quaternionHeadingMessage(45.625, 90.0));
    provider.handleMavlinkMessage(&vehicle, gimbalMessage(10.0));
    QVERIFY(azimuthEquals(provider, 55.625));
}

void GimbalAzimuthProviderTest::invalidTelemetryDoesNotRenewFreshness() {
    Vehicle vehicle;
    MultiVehicleManager::instance()->setActiveVehicle(&vehicle);
    Fact legacyReference{1};
    GimbalAzimuthProvider provider(&legacyReference);
    provider.handleMavlinkMessage(&vehicle, quaternionHeadingMessage(40.0));
    provider.handleMavlinkMessage(&vehicle, headingMessage(40.0));
    provider.handleMavlinkMessage(&vehicle, gimbalMessage(10.0));
    QVERIFY(azimuthEquals(provider, 50.0));
    QTest::qWait(1100);
    provider.handleMavlinkMessage(&vehicle, headingMessage(std::numeric_limits<double>::quiet_NaN()));
    provider.handleMavlinkMessage(&vehicle, quaternionHeadingMessage(std::numeric_limits<double>::quiet_NaN()));
    provider.handleMavlinkMessage(&vehicle, gimbalMessage(10.0));
    QVERIFY(azimuthEquals(provider, 50.0));
    // The gimbal is fresh; only the last valid heading expires.
    QTest::qWait(1200);
    QVERIFY(!provider.valid());
    provider.handleMavlinkMessage(&vehicle, headingMessage(41.0));
    QVERIFY(azimuthEquals(provider, 51.0));
    provider.handleMavlinkMessage(&vehicle, gimbalMessage(std::numeric_limits<double>::quiet_NaN()));
    QVERIFY(!provider.valid());
    provider.handleMavlinkMessage(&vehicle, headingMessage(42.0));
    QVERIFY(!provider.valid());
}

void GimbalAzimuthProviderTest::gimbalExpiryIsIndependentOfHeading() {
    Vehicle vehicle;
    MultiVehicleManager::instance()->setActiveVehicle(&vehicle);
    Fact legacyReference{1};
    GimbalAzimuthProvider provider(&legacyReference);
    provider.handleMavlinkMessage(&vehicle, headingMessage(40.0));
    provider.handleMavlinkMessage(&vehicle, gimbalMessage(10.0));
    QVERIFY(azimuthEquals(provider, 50.0));
    QTest::qWait(1100);
    provider.handleMavlinkMessage(&vehicle, headingMessage(41.0));
    QVERIFY(azimuthEquals(provider, 51.0));
    QTest::qWait(1200);
    QVERIFY(!provider.valid());
    provider.handleMavlinkMessage(&vehicle, headingMessage(42.0));
    QVERIFY(!provider.valid());
    provider.handleMavlinkMessage(&vehicle, gimbalMessage(10.0));
    QVERIFY(azimuthEquals(provider, 52.0));
}

void GimbalAzimuthProviderTest::reconnectRequiresNewHeadingAndGimbal() {
    Vehicle vehicle;
    MultiVehicleManager::instance()->setActiveVehicle(&vehicle);
    Fact legacyReference{1};
    GimbalAzimuthProvider provider(&legacyReference);
    provider.handleMavlinkMessage(&vehicle, headingMessage(40.0));
    provider.handleMavlinkMessage(&vehicle, gimbalMessage(10.0));
    QVERIFY(azimuthEquals(provider, 50.0));
    vehicle.vehicleLinkManager()->setCommunicationLost(true);
    QVERIFY(!provider.valid());
    vehicle.vehicleLinkManager()->setCommunicationLost(false);
    QVERIFY(!provider.valid());
    provider.handleMavlinkMessage(&vehicle, gimbalMessage(20.0));
    QVERIFY(!provider.valid());
    provider.handleMavlinkMessage(&vehicle, headingMessage(50.0));
    QVERIFY(azimuthEquals(provider, 70.0));
}

void GimbalAzimuthProviderTest::ignoresReplayedGimbalSamplesButAcceptsZeroTimestamps() {
    Vehicle vehicle;
    MultiVehicleManager::instance()->setActiveVehicle(&vehicle);
    Fact legacyReference{1};
    GimbalAzimuthProvider provider(&legacyReference);
    const auto older = gimbalMessage(10.0);
    const auto newer = gimbalMessage(20.0);
    provider.handleMavlinkMessage(&vehicle, headingMessage(40.0));
    provider.handleMavlinkMessage(&vehicle, newer);
    QVERIFY(azimuthEquals(provider, 60.0));
    provider.handleMavlinkMessage(&vehicle, older);
    provider.handleMavlinkMessage(&vehicle, newer);
    QVERIFY(azimuthEquals(provider, 60.0));
    provider.handleMavlinkMessage(&vehicle, headingMessage(50.0));
    QVERIFY(azimuthEquals(provider, 70.0));

    mavlink_gimbal_device_attitude_status_t status{};
    status.flags = 28;
    auto q = yawQuaternion(25.0);
    std::copy(q.begin(), q.end(), status.q);
    mavlink_message_t zeroTime{};
    mavlink_msg_gimbal_device_attitude_status_encode(1, 154, &zeroTime, &status);
    provider.handleMavlinkMessage(&vehicle, zeroTime);
    QVERIFY(azimuthEquals(provider, 75.0));
    q = yawQuaternion(26.0);
    std::copy(q.begin(), q.end(), status.q);
    mavlink_msg_gimbal_device_attitude_status_encode(1, 154, &zeroTime, &status);
    provider.handleMavlinkMessage(&vehicle, zeroTime);
    QVERIFY(azimuthEquals(provider, 76.0));
}

QTEST_GUILESS_MAIN(GimbalAzimuthProviderTest)

#include "GimbalAzimuthProviderTest.moc"
