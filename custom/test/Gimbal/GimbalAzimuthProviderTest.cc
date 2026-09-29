/****************************************************************************
 *
 * Real-provider frame conversion, message ordering and freshness tests.
 *
 ****************************************************************************/

#include <QtCore/QLoggingCategory>
#include <QtCore/QPointer>
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

std::array<float, 4> quaternionFromEulerDegrees(double rollDegrees, double pitchDegrees, double yawDegrees) {
    const double cr = std::cos(rollDegrees * kDegreesToRadians * 0.5);
    const double sr = std::sin(rollDegrees * kDegreesToRadians * 0.5);
    const double cp = std::cos(pitchDegrees * kDegreesToRadians * 0.5);
    const double sp = std::sin(pitchDegrees * kDegreesToRadians * 0.5);
    const double cy = std::cos(yawDegrees * kDegreesToRadians * 0.5);
    const double sy = std::sin(yawDegrees * kDegreesToRadians * 0.5);
    return {static_cast<float>(cr * cp * cy + sr * sp * sy),
            static_cast<float>(sr * cp * cy - cr * sp * sy),
            static_cast<float>(cr * sp * cy + sr * cp * sy),
            static_cast<float>(cr * cp * sy - sr * sp * cy)};
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

mavlink_message_t quaternionGimbalMessage(const std::array<float, 4> &q, quint16 flags = 28) {
    mavlink_gimbal_device_attitude_status_t attitude{};
    attitude.time_boot_ms = nextBootMs();
    attitude.flags = flags;
    std::copy(q.begin(), q.end(), attitude.q);
    mavlink_message_t message{};
    mavlink_msg_gimbal_device_attitude_status_encode(1, 154, &message, &attitude);
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
    void followsAndLocksWithFixedLegacyFrame();
    void usesUnroundedHeadingAndRefreshesOnHeadingArrival();
    void constructorHonorsQObjectParent();
    void invertedInstallationTracksYawCommands_data();
    void invertedInstallationTracksYawCommands();
    void invertedInstallationBaseRotation_data();
    void invertedInstallationBaseRotation();
    void invertedInstallationTiltKeepsAzimuth_data();
    void invertedInstallationTiltKeepsAzimuth();
    void fixedFeedbackConventionNeedsHeadingInBothModes();
    void installationConventionDoesNotOverrideExplicitFrames();
    void installationConventionPreservesFreshnessGuards();
    void explicitFramesOverrideFixedLegacyConvention();
    void isolatesVehicleComponentAndDeviceRoutes();
    void highLatencyUnits_data();
    void highLatencyUnits();
    void quaternionIgnoresDisplayOffset();
    void invalidTelemetryDoesNotRenewFreshness();
    void gimbalExpiryIsIndependentOfHeading();
    void reconnectRequiresNewHeadingAndGimbal();
    void ignoresReplayedGimbalSamplesButAcceptsZeroTimestamps();
};

void GimbalAzimuthProviderTest::followsAndLocksWithFixedLegacyFrame() {
    Vehicle vehicle;
    MultiVehicleManager::instance()->setActiveVehicle(&vehicle);
    GimbalAzimuthProvider provider;

    provider.handleMavlinkMessage(&vehicle, headingMessage(45.0));
    provider.handleMavlinkMessage(&vehicle, gimbalMessage(-171.738, 12));
    QVERIFY(azimuthEquals(provider, -126.738));
    provider.handleMavlinkMessage(&vehicle, gimbalMessage(-171.387, 28));
    QVERIFY(azimuthEquals(provider, -126.387));
    provider.handleMavlinkMessage(&vehicle, gimbalMessage(-171.387, 12));
    QVERIFY(azimuthEquals(provider, -126.387));
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
    GimbalAzimuthProvider provider;
    QCOMPARE(vehicle.heading()->rawValue().toDouble(), 45.0);
    provider.handleMavlinkMessage(&vehicle, gimbalMessage(10.0));
    QVERIFY(!provider.valid());
    provider.handleMavlinkMessage(&vehicle, headingMessage(45.625));
    QVERIFY(azimuthEquals(provider, 55.625));
    provider.handleMavlinkMessage(&vehicle, headingMessage(46.125));
    QVERIFY(azimuthEquals(provider, 56.125));
}

void GimbalAzimuthProviderTest::constructorHonorsQObjectParent() {
    QPointer<GimbalAzimuthProvider> provider;
    {
        QObject owner;
        provider = new GimbalAzimuthProvider(&owner);
        QCOMPARE(provider->parent(), &owner);
        QVERIFY(owner.children().contains(provider.data()));
    }
    QVERIFY(provider.isNull());
}

void GimbalAzimuthProviderTest::invertedInstallationTracksYawCommands_data() {
    QTest::addColumn<int>("flags");
    QTest::addColumn<double>("heading");
    QTest::addColumn<double>("initialYaw");
    QTest::addColumn<double>("commandedYaw");
    QTest::addColumn<double>("expectedAzimuth");

    for (int flags : {12, 28}) {
        const QByteArray mode = flags == 12 ? "follow-" : "lock-";
        QTest::newRow((mode + "north-right-east").constData()) << flags << 0.0 << 0.0 << 30.0 << 30.0;
        QTest::newRow((mode + "north-left-west").constData()) << flags << 0.0 << 0.0 << -30.0 << 330.0;
        QTest::newRow((mode + "nonzero-heading-right").constData()) << flags << 70.0 << 20.0 << 35.0 << 105.0;
        QTest::newRow((mode + "nonzero-heading-left").constData()) << flags << 70.0 << 20.0 << 5.0 << 75.0;
        QTest::newRow((mode + "east-right").constData()) << flags << 90.0 << 0.0 << 30.0 << 120.0;
        QTest::newRow((mode + "east-left").constData()) << flags << 90.0 << 0.0 << -30.0 << 60.0;
        QTest::newRow((mode + "south-right").constData()) << flags << 180.0 << 0.0 << 30.0 << 210.0;
        QTest::newRow((mode + "south-left").constData()) << flags << 180.0 << 0.0 << -30.0 << 150.0;
        QTest::newRow((mode + "west-right").constData()) << flags << 270.0 << 0.0 << 30.0 << 300.0;
        QTest::newRow((mode + "west-left").constData()) << flags << 270.0 << 0.0 << -30.0 << 240.0;
        QTest::newRow((mode + "north-wrap-right").constData()) << flags << 359.75 << 0.0 << 0.5 << 0.25;
        QTest::newRow((mode + "north-wrap-left").constData()) << flags << 0.25 << 0.0 << -0.5 << 359.75;
    }
}

void GimbalAzimuthProviderTest::invertedInstallationTracksYawCommands() {
    QFETCH(int, flags);
    QFETCH(double, heading);
    QFETCH(double, initialYaw);
    QFETCH(double, commandedYaw);
    QFETCH(double, expectedAzimuth);
    Vehicle vehicle;
    MultiVehicleManager::instance()->setActiveVehicle(&vehicle);
    GimbalAzimuthProvider provider;
    // Synthetic inputs express the installed A8 Mini's direction contract;
    // these are not a replay of an upside-down hardware capture. The earlier
    // tabletop capture remains covered by the generic reversed-policy tests.
    provider.handleMavlinkMessage(&vehicle, headingMessage(heading));
    provider.handleMavlinkMessage(&vehicle, gimbalMessage(initialYaw, flags));
    QVERIFY(azimuthEquals(provider, heading + initialYaw));
    const double before = provider.absoluteYaw();
    provider.handleMavlinkMessage(&vehicle, gimbalMessage(commandedYaw, flags));
    QVERIFY(azimuthEquals(provider, expectedAzimuth));
    const double actualChange = GimbalAzimuthPolicy::wrap180(provider.absoluteYaw() - before);
    const double expectedChange = commandedYaw - initialYaw;
    QVERIFY(std::abs(actualChange - expectedChange) < 0.0001);
    QCOMPARE(provider.referenceSource(), QStringLiteral("ConfiguredLegacyVehicleHeading"));
}

void GimbalAzimuthProviderTest::invertedInstallationBaseRotation_data() {
    QTest::addColumn<int>("flags");
    QTest::addColumn<double>("feedbackAfterRotation");
    QTest::addColumn<double>("expectedAfterRotation");
    QTest::newRow("locked-camera-base-plus-90") << 28 << -70.0 << 90.0;
    QTest::newRow("following-camera-base-plus-90") << 12 << 20.0 << 180.0;
}

void GimbalAzimuthProviderTest::invertedInstallationBaseRotation() {
    QFETCH(int, flags);
    QFETCH(double, feedbackAfterRotation);
    QFETCH(double, expectedAfterRotation);
    Vehicle vehicle;
    MultiVehicleManager::instance()->setActiveVehicle(&vehicle);
    GimbalAzimuthProvider provider;
    provider.handleMavlinkMessage(&vehicle, headingMessage(70.0));
    provider.handleMavlinkMessage(&vehicle, gimbalMessage(20.0, flags));
    QVERIFY(azimuthEquals(provider, 90.0));
    // For a world-locked camera, relative feedback falls by 90 degrees;
    // in follow mode relative feedback stays fixed and world yaw rises by 90.
    provider.handleMavlinkMessage(&vehicle, headingMessage(160.0));
    provider.handleMavlinkMessage(&vehicle, gimbalMessage(feedbackAfterRotation, flags));
    QVERIFY(azimuthEquals(provider, expectedAfterRotation));
    QCOMPARE(provider.referenceSource(), QStringLiteral("ConfiguredLegacyVehicleHeading"));
}

void GimbalAzimuthProviderTest::invertedInstallationTiltKeepsAzimuth_data() {
    QTest::addColumn<int>("flags");
    QTest::addColumn<double>("roll");
    QTest::addColumn<double>("pitch");
    for (int flags : {12, 28}) {
        const QByteArray mode = flags == 12 ? "follow-" : "lock-";
        QTest::newRow((mode + "pitch-down").constData()) << flags << 0.0 << -60.0;
        QTest::newRow((mode + "pitch-up").constData()) << flags << 0.0 << 60.0;
        QTest::newRow((mode + "roll-and-pitch").constData()) << flags << 15.0 << -30.0;
        QTest::newRow((mode + "half-turn-roll-pitch-down").constData()) << flags << 180.0 << -60.0;
        QTest::newRow((mode + "half-turn-roll-pitch-up").constData()) << flags << 180.0 << 60.0;
        QTest::newRow((mode + "negative-half-turn-roll-pitch-up").constData()) << flags << -180.0 << 60.0;
    }
}

void GimbalAzimuthProviderTest::invertedInstallationTiltKeepsAzimuth() {
    QFETCH(int, flags);
    QFETCH(double, roll);
    QFETCH(double, pitch);
    Vehicle vehicle;
    MultiVehicleManager::instance()->setActiveVehicle(&vehicle);
    GimbalAzimuthProvider provider;
    provider.handleMavlinkMessage(&vehicle, headingMessage(30.25));
    provider.handleMavlinkMessage(&vehicle, gimbalMessage(42.125, flags));
    QVERIFY(azimuthEquals(provider, 72.375));
    auto q = quaternionFromEulerDegrees(roll, pitch, 42.125);
    provider.handleMavlinkMessage(&vehicle, quaternionGimbalMessage(q, flags));
    QVERIFY(azimuthEquals(provider, 72.375));
    // q and -q describe the same orientation, including a half-turn roll.
    for (float &component : q) {
        component = -component;
    }
    provider.handleMavlinkMessage(&vehicle, quaternionGimbalMessage(q, flags));
    QVERIFY(azimuthEquals(provider, 72.375));
    QCOMPARE(provider.referenceSource(), QStringLiteral("ConfiguredLegacyVehicleHeading"));
}

void GimbalAzimuthProviderTest::fixedFeedbackConventionNeedsHeadingInBothModes() {
    Vehicle vehicle;
    MultiVehicleManager::instance()->setActiveVehicle(&vehicle);
    GimbalAzimuthProvider provider;
    provider.handleMavlinkMessage(&vehicle, gimbalMessage(20.0, 28));
    QVERIFY(!provider.valid());
    provider.handleMavlinkMessage(&vehicle, gimbalMessage(20.0, 12));
    QVERIFY(!provider.valid());
    provider.handleMavlinkMessage(&vehicle, headingMessage(70.0));
    QVERIFY(azimuthEquals(provider, 90.0));
    QCOMPARE(provider.referenceSource(), QStringLiteral("ConfiguredLegacyVehicleHeading"));
    provider.handleMavlinkMessage(&vehicle, gimbalMessage(20.0, 28));
    QVERIFY(azimuthEquals(provider, 90.0));
    QCOMPARE(provider.referenceSource(), QStringLiteral("ConfiguredLegacyVehicleHeading"));

    // The product has no user-configurable frame or direction: lock/follow use
    // the same fixed heading-plus-feedback contract for legacy messages.
    provider.handleMavlinkMessage(&vehicle, headingMessage(160.0));
    provider.handleMavlinkMessage(&vehicle, gimbalMessage(-70.0, 28));
    QVERIFY(azimuthEquals(provider, 90.0));
    provider.handleMavlinkMessage(&vehicle, headingMessage(250.0));
    provider.handleMavlinkMessage(&vehicle, gimbalMessage(-70.0, 12));
    QVERIFY(azimuthEquals(provider, 180.0));
}

void GimbalAzimuthProviderTest::installationConventionDoesNotOverrideExplicitFrames() {
    Vehicle vehicle;
    MultiVehicleManager::instance()->setActiveVehicle(&vehicle);
    GimbalAzimuthProvider provider;
    provider.handleMavlinkMessage(&vehicle, headingMessage(70.0));
    provider.handleMavlinkMessage(&vehicle, gimbalMessage(20.0, 28 | GIMBAL_DEVICE_FLAGS_YAW_IN_EARTH_FRAME));
    QVERIFY(azimuthEquals(provider, 20.0));
    provider.handleMavlinkMessage(&vehicle, gimbalMessage(20.0, 28 | GIMBAL_DEVICE_FLAGS_YAW_IN_VEHICLE_FRAME));
    QVERIFY(azimuthEquals(provider, 90.0));

    mavlink_gimbal_device_attitude_status_t attitude{};
    attitude.flags = 28 | GIMBAL_DEVICE_FLAGS_YAW_IN_VEHICLE_FRAME;
    attitude.time_boot_ms = nextBootMs();
    attitude.delta_yaw = static_cast<float>(130.0 * kDegreesToRadians);
    attitude.delta_yaw_velocity = 0.1F;  // Nonzero tail makes the extension explicit on the wire.
    const auto q = yawQuaternion(20.0);
    std::copy(q.begin(), q.end(), attitude.q);
    mavlink_message_t message{};
    mavlink_msg_gimbal_device_attitude_status_encode(1, 154, &message, &attitude);
    provider.handleMavlinkMessage(&vehicle, message);
    QVERIFY(azimuthEquals(provider, 150.0));
    QVERIFY(provider.usingDeltaYaw());
}

void GimbalAzimuthProviderTest::installationConventionPreservesFreshnessGuards() {
    Vehicle vehicle;
    MultiVehicleManager::instance()->setActiveVehicle(&vehicle);
    GimbalAzimuthProvider provider;
    provider.handleMavlinkMessage(&vehicle, gimbalMessage(20.0));
    QVERIFY(!provider.valid());
    provider.handleMavlinkMessage(&vehicle, headingMessage(70.0));
    QVERIFY(azimuthEquals(provider, 90.0));
    QTest::qWait(1100);
    provider.handleMavlinkMessage(&vehicle, gimbalMessage(20.0));
    QTest::qWait(1200);
    QVERIFY(!provider.valid());  // Gimbal is fresh, heading has expired.
    provider.handleMavlinkMessage(&vehicle, headingMessage(72.0));
    QVERIFY(azimuthEquals(provider, 92.0));
    vehicle.vehicleLinkManager()->setCommunicationLost(true);
    QVERIFY(!provider.valid());
    vehicle.vehicleLinkManager()->setCommunicationLost(false);
    QVERIFY(!provider.valid());
    provider.handleMavlinkMessage(&vehicle, headingMessage(74.0));
    QVERIFY(!provider.valid());  // Reconnection also requires fresh gimbal data.
    provider.handleMavlinkMessage(&vehicle, gimbalMessage(20.0));
    QVERIFY(azimuthEquals(provider, 94.0));
}

void GimbalAzimuthProviderTest::explicitFramesOverrideFixedLegacyConvention() {
    Vehicle vehicle;
    MultiVehicleManager::instance()->setActiveVehicle(&vehicle);
    GimbalAzimuthProvider provider;
    provider.handleMavlinkMessage(&vehicle, gimbalMessage(23.5, 28 | GIMBAL_DEVICE_FLAGS_YAW_IN_EARTH_FRAME));
    QVERIFY(azimuthEquals(provider, 23.5));
    provider.handleMavlinkMessage(&vehicle, headingMessage(70.0));
    QVERIFY(azimuthEquals(provider, 23.5));
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
    GimbalAzimuthProvider provider;
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
    GimbalAzimuthProvider provider;
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
    GimbalAzimuthProvider provider;
    provider.handleMavlinkMessage(&vehicle, headingMessage(135.0));
    provider.handleMavlinkMessage(&vehicle, quaternionHeadingMessage(45.625, 90.0));
    provider.handleMavlinkMessage(&vehicle, gimbalMessage(10.0));
    QVERIFY(azimuthEquals(provider, 55.625));
}

void GimbalAzimuthProviderTest::invalidTelemetryDoesNotRenewFreshness() {
    Vehicle vehicle;
    MultiVehicleManager::instance()->setActiveVehicle(&vehicle);
    GimbalAzimuthProvider provider;
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
    GimbalAzimuthProvider provider;
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
    GimbalAzimuthProvider provider;
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
    GimbalAzimuthProvider provider;
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
