/****************************************************************************
 *
 * Gimbal azimuth frame conversion policy regression tests.
 *
 ****************************************************************************/

#include <QtTest/QTest>
#include <array>
#include <cmath>
#include <limits>

#include "GimbalAzimuthPolicy.h"

namespace {

constexpr double kDegreesToRadians = 3.141592653589793238462643383279502884 / 180.0;

std::array<double, 4> quaternionFromEulerDegrees(double rollDegrees, double pitchDegrees, double yawDegrees) {
    const double roll = rollDegrees * kDegreesToRadians;
    const double pitch = pitchDegrees * kDegreesToRadians;
    const double yaw = yawDegrees * kDegreesToRadians;

    const double cr = std::cos(roll * 0.5);
    const double sr = std::sin(roll * 0.5);
    const double cp = std::cos(pitch * 0.5);
    const double sp = std::sin(pitch * 0.5);
    const double cy = std::cos(yaw * 0.5);
    const double sy = std::sin(yaw * 0.5);

    return {
        (cr * cp * cy) + (sr * sp * sy),
        (sr * cp * cy) - (cr * sp * sy),
        (cr * sp * cy) + (sr * cp * sy),
        (cr * cp * sy) - (sr * sp * cy),
    };
}

bool anglesEqual(double actual, double expected, double tolerance = 1e-9) {
    return std::abs(GimbalAzimuthPolicy::wrap180(actual - expected)) <= tolerance;
}

}  // namespace

class GimbalAzimuthPolicyTest : public QObject {
    Q_OBJECT

   private slots:
    void explicitVehicleFrameUsesDeltaYaw();
    void explicitVehicleFrameUsesZeroDeltaYaw();
    void explicitVehicleFrameRequiresUsableDeltaYaw_data();
    void explicitVehicleFrameRequiresUsableDeltaYaw();
    void explicitEarthFrameUsesReportedYaw();
    void explicitEarthFrameIgnoresDeltaForAzimuth();
    void explicitFrameOverridesYawLock();
    void yawLockKeepsAbsoluteYawStableAcrossBaseRotation();
    void yawFollowTracksBaseRotationWithFixedBodyYaw();
    void legacyFollowIgnoresDeltaYaw();
    void legacyLockIgnoresDeltaYaw();
    void conflictingFrameFlagsAreRejected();
    void invalidQuaternionIsRejected();
    void nonUnitQuaternionIsNormalized();
    void vehicleFrameNeedsEarthReference();
    void earthFrameNeedsNoExternalReference();
    void wrap180_data();
    void wrap180();
    void wrap180PreservesNonFiniteValues();
};

void GimbalAzimuthPolicyTest::explicitVehicleFrameUsesDeltaYaw() {
    GimbalAzimuthPolicy::Input input;
    input.quaternion = quaternionFromEulerDegrees(25.0, -35.0, -70.0);
    input.yawInVehicleFrame = true;
    input.yawLock = true;
    input.deltaYawSupported = true;
    input.deltaYawAvailable = true;
    input.deltaYawRadians = 120.0 * kDegreesToRadians;
    input.vehicleHeadingAvailable = true;
    input.vehicleHeadingDegrees = -45.0;

    const auto result = GimbalAzimuthPolicy::calculate(input);

    QVERIFY(result.valid);
    QVERIFY(anglesEqual(result.absoluteYawDegrees, 50.0));
    QCOMPARE(result.source, GimbalAzimuthPolicy::Source::DeltaYaw);
    QCOMPARE(result.error, GimbalAzimuthPolicy::Error::None);
}

void GimbalAzimuthPolicyTest::explicitVehicleFrameUsesZeroDeltaYaw() {
    GimbalAzimuthPolicy::Input input;
    input.quaternion = quaternionFromEulerDegrees(0.0, -20.0, 35.0);
    input.yawInVehicleFrame = true;
    input.yawLock = true;
    input.deltaYawSupported = true;
    input.deltaYawAvailable = true;
    input.deltaYawRadians = 0.0;
    input.vehicleHeadingAvailable = true;
    input.vehicleHeadingDegrees = 120.0;

    const auto result = GimbalAzimuthPolicy::calculate(input);

    QVERIFY(result.valid);
    QVERIFY(anglesEqual(result.absoluteYawDegrees, 35.0));
    QCOMPARE(result.source, GimbalAzimuthPolicy::Source::DeltaYaw);
    QCOMPARE(result.error, GimbalAzimuthPolicy::Error::None);
}

void GimbalAzimuthPolicyTest::explicitVehicleFrameRequiresUsableDeltaYaw_data() {
    QTest::addColumn<bool>("deltaSupported");
    QTest::addColumn<bool>("deltaAvailable");
    QTest::addColumn<double>("deltaYawRadians");

    QTest::newRow("unsupported") << false << true << (90.0 * kDegreesToRadians);
    QTest::newRow("not-available") << true << false << (90.0 * kDegreesToRadians);
    QTest::newRow("non-finite") << true << true << std::numeric_limits<double>::quiet_NaN();
}

void GimbalAzimuthPolicyTest::explicitVehicleFrameRequiresUsableDeltaYaw() {
    QFETCH(bool, deltaSupported);
    QFETCH(bool, deltaAvailable);
    QFETCH(double, deltaYawRadians);

    GimbalAzimuthPolicy::Input input;
    input.quaternion = quaternionFromEulerDegrees(0.0, 0.0, 10.0);
    input.yawInVehicleFrame = true;
    input.deltaYawSupported = deltaSupported;
    input.deltaYawAvailable = deltaAvailable;
    input.deltaYawRadians = deltaYawRadians;
    input.vehicleHeadingAvailable = true;
    input.vehicleHeadingDegrees = 20.0;

    const auto result = GimbalAzimuthPolicy::calculate(input);

    QVERIFY(result.valid);
    QVERIFY(anglesEqual(result.absoluteYawDegrees, 30.0));
    QCOMPARE(result.source, GimbalAzimuthPolicy::Source::VehicleHeadingFallback);
}

void GimbalAzimuthPolicyTest::explicitEarthFrameUsesReportedYaw() {
    GimbalAzimuthPolicy::Input input;
    input.quaternion = quaternionFromEulerDegrees(-15.0, 30.0, 75.0);
    input.yawInEarthFrame = true;
    input.deltaYawSupported = true;
    input.deltaYawAvailable = true;
    input.deltaYawRadians = -140.0 * kDegreesToRadians;
    input.vehicleHeadingAvailable = true;
    input.vehicleHeadingDegrees = -50.0;

    const auto result = GimbalAzimuthPolicy::calculate(input);

    QVERIFY(result.valid);
    QVERIFY(anglesEqual(result.absoluteYawDegrees, 75.0));
    QCOMPARE(result.source, GimbalAzimuthPolicy::Source::ReportedEarthFrame);
}

void GimbalAzimuthPolicyTest::explicitEarthFrameIgnoresDeltaForAzimuth() {
    GimbalAzimuthPolicy::Input input;
    input.quaternion = quaternionFromEulerDegrees(20.0, -25.0, 80.0);
    input.yawInEarthFrame = true;
    input.deltaYawSupported = true;
    input.deltaYawAvailable = true;
    input.deltaYawRadians = 30.0 * kDegreesToRadians;
    input.vehicleHeadingAvailable = true;
    input.vehicleHeadingDegrees = -100.0;

    const auto firstResult = GimbalAzimuthPolicy::calculate(input);
    input.deltaYawRadians = -150.0 * kDegreesToRadians;
    input.vehicleHeadingDegrees = 45.0;
    const auto secondResult = GimbalAzimuthPolicy::calculate(input);

    QVERIFY(firstResult.valid);
    QVERIFY(secondResult.valid);
    QVERIFY(anglesEqual(firstResult.absoluteYawDegrees, 80.0));
    QVERIFY(anglesEqual(secondResult.absoluteYawDegrees, 80.0));
}

void GimbalAzimuthPolicyTest::explicitFrameOverridesYawLock() {
    GimbalAzimuthPolicy::Input vehicleInput;
    vehicleInput.quaternion = quaternionFromEulerDegrees(0.0, 0.0, -15.0);
    vehicleInput.yawInVehicleFrame = true;
    vehicleInput.yawLock = true;
    vehicleInput.deltaYawSupported = true;
    vehicleInput.deltaYawAvailable = true;
    vehicleInput.deltaYawRadians = 40.0 * kDegreesToRadians;

    const auto vehicleResult = GimbalAzimuthPolicy::calculate(vehicleInput);
    QVERIFY(vehicleResult.valid);
    QVERIFY(anglesEqual(vehicleResult.absoluteYawDegrees, 25.0));
    QCOMPARE(vehicleResult.source, GimbalAzimuthPolicy::Source::DeltaYaw);

    GimbalAzimuthPolicy::Input earthInput;
    earthInput.quaternion = quaternionFromEulerDegrees(0.0, 0.0, 65.0);
    earthInput.yawInEarthFrame = true;
    earthInput.yawLock = false;

    const auto earthResult = GimbalAzimuthPolicy::calculate(earthInput);
    QVERIFY(earthResult.valid);
    QVERIFY(anglesEqual(earthResult.absoluteYawDegrees, 65.0));
    QCOMPARE(earthResult.source, GimbalAzimuthPolicy::Source::ReportedEarthFrame);
}

void GimbalAzimuthPolicyTest::yawLockKeepsAbsoluteYawStableAcrossBaseRotation() {
    GimbalAzimuthPolicy::Input beforeRotation;
    beforeRotation.quaternion = quaternionFromEulerDegrees(0.0, -30.0, 30.0);
    beforeRotation.yawInVehicleFrame = true;
    beforeRotation.yawLock = true;
    beforeRotation.deltaYawSupported = true;
    beforeRotation.deltaYawAvailable = true;
    beforeRotation.deltaYawRadians = 70.0 * kDegreesToRadians;

    GimbalAzimuthPolicy::Input afterRotation = beforeRotation;
    // The base turns +90 degrees while the locked camera keeps its earth
    // bearing: body-relative yaw therefore turns -90 degrees.
    afterRotation.quaternion = quaternionFromEulerDegrees(0.0, -30.0, -60.0);
    afterRotation.deltaYawRadians = 160.0 * kDegreesToRadians;

    const auto beforeResult = GimbalAzimuthPolicy::calculate(beforeRotation);
    const auto afterResult = GimbalAzimuthPolicy::calculate(afterRotation);

    QVERIFY(beforeResult.valid);
    QVERIFY(afterResult.valid);
    QVERIFY(anglesEqual(beforeResult.absoluteYawDegrees, 100.0));
    QVERIFY(anglesEqual(afterResult.absoluteYawDegrees, 100.0));
    QVERIFY(anglesEqual(afterResult.absoluteYawDegrees, beforeResult.absoluteYawDegrees));
    QCOMPARE(beforeResult.source, GimbalAzimuthPolicy::Source::DeltaYaw);
    QCOMPARE(afterResult.source, GimbalAzimuthPolicy::Source::DeltaYaw);
}

void GimbalAzimuthPolicyTest::yawFollowTracksBaseRotationWithFixedBodyYaw() {
    GimbalAzimuthPolicy::Input beforeRotation;
    beforeRotation.quaternion = quaternionFromEulerDegrees(0.0, -30.0, -20.0);
    beforeRotation.yawInVehicleFrame = true;
    beforeRotation.yawLock = false;
    beforeRotation.deltaYawSupported = true;
    beforeRotation.deltaYawAvailable = true;
    beforeRotation.deltaYawRadians = 30.0 * kDegreesToRadians;
    beforeRotation.vehicleHeadingAvailable = true;
    beforeRotation.vehicleHeadingDegrees = 30.0;

    GimbalAzimuthPolicy::Input afterRotation = beforeRotation;
    // Follow keeps body-relative yaw fixed while the vehicle/base earth
    // offset, represented by delta_yaw, turns +90 degrees.
    afterRotation.deltaYawRadians = 120.0 * kDegreesToRadians;
    afterRotation.vehicleHeadingDegrees = 120.0;

    const auto beforeResult = GimbalAzimuthPolicy::calculate(beforeRotation);
    const auto afterResult = GimbalAzimuthPolicy::calculate(afterRotation);

    QVERIFY(beforeResult.valid);
    QVERIFY(afterResult.valid);
    QVERIFY(anglesEqual(beforeResult.absoluteYawDegrees, 10.0));
    QVERIFY(anglesEqual(afterResult.absoluteYawDegrees, 100.0));
    QVERIFY(anglesEqual(afterResult.absoluteYawDegrees - beforeResult.absoluteYawDegrees, 90.0));
    QCOMPARE(beforeResult.source, GimbalAzimuthPolicy::Source::DeltaYaw);
    QCOMPARE(afterResult.source, GimbalAzimuthPolicy::Source::DeltaYaw);
}

void GimbalAzimuthPolicyTest::legacyFollowIgnoresDeltaYaw() {
    GimbalAzimuthPolicy::Input input;
    input.quaternion = quaternionFromEulerDegrees(0.0, 0.0, 10.0);
    input.yawLock = false;
    input.deltaYawSupported = true;
    input.deltaYawAvailable = true;
    input.deltaYawRadians = 90.0 * kDegreesToRadians;
    input.vehicleHeadingAvailable = true;
    input.vehicleHeadingDegrees = 20.0;

    const auto result = GimbalAzimuthPolicy::calculate(input);

    QVERIFY(result.valid);
    QVERIFY(anglesEqual(result.absoluteYawDegrees, 30.0));
    QCOMPARE(result.source, GimbalAzimuthPolicy::Source::LegacyVehicleHeading);
}

void GimbalAzimuthPolicyTest::legacyLockIgnoresDeltaYaw() {
    GimbalAzimuthPolicy::Input input;
    input.quaternion = quaternionFromEulerDegrees(0.0, 0.0, 50.0);
    input.yawLock = true;
    input.deltaYawSupported = true;
    input.deltaYawAvailable = true;
    input.deltaYawRadians = 90.0 * kDegreesToRadians;
    input.vehicleHeadingAvailable = true;
    input.vehicleHeadingDegrees = 20.0;

    const auto result = GimbalAzimuthPolicy::calculate(input);

    QVERIFY(result.valid);
    QVERIFY(anglesEqual(result.absoluteYawDegrees, 50.0));
    QCOMPARE(result.source, GimbalAzimuthPolicy::Source::LegacyEarthFrame);
}

void GimbalAzimuthPolicyTest::conflictingFrameFlagsAreRejected() {
    GimbalAzimuthPolicy::Input input;
    input.yawInVehicleFrame = true;
    input.yawInEarthFrame = true;

    const auto result = GimbalAzimuthPolicy::calculate(input);

    QVERIFY(!result.valid);
    QCOMPARE(result.source, GimbalAzimuthPolicy::Source::Invalid);
    QCOMPARE(result.error, GimbalAzimuthPolicy::Error::ConflictingFrameFlags);
}

void GimbalAzimuthPolicyTest::invalidQuaternionIsRejected() {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double infinity = std::numeric_limits<double>::infinity();
    const std::array<std::array<double, 4>, 4> invalidQuaternions{
        std::array<double, 4>{0.0, 0.0, 0.0, 0.0},
        std::array<double, 4>{1e-14, 0.0, 0.0, 0.0},
        std::array<double, 4>{1.0, nan, 0.0, 0.0},
        std::array<double, 4>{1.0, 0.0, infinity, 0.0},
    };

    for (const auto& quaternion : invalidQuaternions) {
        GimbalAzimuthPolicy::Input input;
        input.quaternion = quaternion;
        input.yawInEarthFrame = true;

        QVERIFY(!GimbalAzimuthPolicy::isValidQuaternion(quaternion));

        const auto result = GimbalAzimuthPolicy::calculate(input);
        QVERIFY(!result.valid);
        QCOMPARE(result.error, GimbalAzimuthPolicy::Error::InvalidQuaternion);
    }
}

void GimbalAzimuthPolicyTest::nonUnitQuaternionIsNormalized() {
    GimbalAzimuthPolicy::Input input;
    input.quaternion = quaternionFromEulerDegrees(0.0, 0.0, -115.0);
    for (double& component : input.quaternion) {
        component *= 4.5;
    }
    input.yawInEarthFrame = true;

    QVERIFY(GimbalAzimuthPolicy::isValidQuaternion(input.quaternion));
    const auto result = GimbalAzimuthPolicy::calculate(input);
    QVERIFY(result.valid);
    QVERIFY(anglesEqual(result.absoluteYawDegrees, -115.0));
}

void GimbalAzimuthPolicyTest::vehicleFrameNeedsEarthReference() {
    GimbalAzimuthPolicy::Input input;
    input.quaternion = quaternionFromEulerDegrees(0.0, 0.0, -35.0);
    input.yawInVehicleFrame = true;

    const auto result = GimbalAzimuthPolicy::calculate(input);

    QVERIFY(!result.valid);
    QCOMPARE(result.source, GimbalAzimuthPolicy::Source::Invalid);
    QCOMPARE(result.error, GimbalAzimuthPolicy::Error::MissingEarthReference);
}

void GimbalAzimuthPolicyTest::earthFrameNeedsNoExternalReference() {
    GimbalAzimuthPolicy::Input input;
    input.quaternion = quaternionFromEulerDegrees(0.0, 0.0, 125.0);
    input.yawInEarthFrame = true;

    const auto result = GimbalAzimuthPolicy::calculate(input);

    QVERIFY(result.valid);
    QVERIFY(anglesEqual(result.absoluteYawDegrees, 125.0));
    QCOMPARE(result.source, GimbalAzimuthPolicy::Source::ReportedEarthFrame);
}

void GimbalAzimuthPolicyTest::wrap180_data() {
    QTest::addColumn<double>("input");
    QTest::addColumn<double>("expected");

    QTest::newRow("zero") << 0.0 << 0.0;
    QTest::newRow("positive-boundary") << 180.0 << -180.0;
    QTest::newRow("negative-boundary") << -180.0 << -180.0;
    QTest::newRow("positive-turn") << 540.0 << -180.0;
    QTest::newRow("negative-turn") << -540.0 << -180.0;
    QTest::newRow("large-positive") << 1441.25 << 1.25;
    QTest::newRow("large-negative") << -1441.25 << -1.25;
}

void GimbalAzimuthPolicyTest::wrap180() {
    QFETCH(double, input);
    QFETCH(double, expected);

    QCOMPARE(GimbalAzimuthPolicy::wrap180(input), expected);
}

void GimbalAzimuthPolicyTest::wrap180PreservesNonFiniteValues() {
    const double nan = GimbalAzimuthPolicy::wrap180(std::numeric_limits<double>::quiet_NaN());
    const double positiveInfinity = GimbalAzimuthPolicy::wrap180(std::numeric_limits<double>::infinity());
    const double negativeInfinity = GimbalAzimuthPolicy::wrap180(-std::numeric_limits<double>::infinity());

    QVERIFY(std::isnan(nan));
    QVERIFY(std::isinf(positiveInfinity));
    QVERIFY(positiveInfinity > 0.0);
    QVERIFY(std::isinf(negativeInfinity));
    QVERIFY(negativeInfinity < 0.0);
}

QTEST_APPLESS_MAIN(GimbalAzimuthPolicyTest)

#include "GimbalAzimuthPolicyTest.moc"
