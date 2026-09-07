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
#include "GimbalHeadingTelemetry.h"

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

GimbalAzimuthPolicy::Input legacyInput(GimbalAzimuthPolicy::LegacyYawReference reference, bool yawLock,
                                       double vehicleHeadingDegrees, double reportedYawDegrees) {
    GimbalAzimuthPolicy::Input input;
    input.legacyYawReference = reference;
    input.yawLock = yawLock;
    input.quaternion = quaternionFromEulerDegrees(0.0, 0.0, reportedYawDegrees);
    input.vehicleHeadingAvailable = true;
    input.vehicleHeadingDegrees = vehicleHeadingDegrees;
    return input;
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
    void yawLockWithoutDeltaUsesVehicleHeading();
    void yawFollowTracksBaseRotationWithFixedBodyYaw();
    void legacyReferenceModes_data();
    void legacyReferenceModes();
    void configuredVehicleKeepsLoggedLockTransitionContinuous();
    void configuredVehicleKeepsBearingDuringDenseBaseRotation();
    void configuredVehicleHandlesAlternatingHeadingPackets();
    void configuredVehicleKeepsFrameAfterHeadingCatchesUp();
    void configuredVehicleFollowTracksBaseRotation();
    void reversedLegacyReplaysRecordedLockRotation();
    void reversedLegacyReplaysSecondRecordedLockRotation();
    void reversedLegacyKeepsModeTransitionContinuous();
    void reversedLegacyFollowAndYawCommandsRemainLive();
    void reversedLegacyPreservesBearingAcrossFullBaseTurn();
    void reversedLegacyHandlesPitchRollAndQuaternionSign();
    void reversedLegacyDoesNotOverrideOtherFrames_data();
    void reversedLegacyDoesNotOverrideOtherFrames();
    void reversedLegacyStillRequiresValidInputs();
    void configuredEarthIgnoresHeadingAndLockMode();
    void configuredYawCommandsChangeLockedBearing();
    void legacyPitchChangesDoNotChangeAzimuth();
    void configuredVehiclePreservesFractionalYawAcrossNorth();
    void configuredVehicleWithoutHeadingIsInvalid();
    void configuredEarthNeedsNoHeading();
    void explicitFramesOverrideLegacyReference();
    void configuredReferenceStillRejectsConflictingFrames();
    void configuredReferenceStillRejectsInvalidQuaternion();
    void headingExpiryInvalidatesConfiguredVehicleResult();
    void invalidHeadingCannotRenewConfiguredVehicleResult();
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

void GimbalAzimuthPolicyTest::yawLockWithoutDeltaUsesVehicleHeading() {
    GimbalAzimuthPolicy::Input beforeRotation;
    beforeRotation.quaternion = quaternionFromEulerDegrees(0.0, -25.0, 57.0);
    beforeRotation.yawInVehicleFrame = true;
    beforeRotation.yawLock = true;
    beforeRotation.deltaYawSupported = true;
    beforeRotation.deltaYawAvailable = false;
    beforeRotation.deltaYawRadians = std::numeric_limits<double>::quiet_NaN();
    beforeRotation.vehicleHeadingAvailable = true;
    beforeRotation.vehicleHeadingDegrees = 15.0;

    GimbalAzimuthPolicy::Input afterRotation = beforeRotation;
    afterRotation.quaternion = quaternionFromEulerDegrees(0.0, -25.0, -53.0);
    afterRotation.vehicleHeadingDegrees = 125.0;

    const auto beforeResult = GimbalAzimuthPolicy::calculate(beforeRotation);
    const auto afterResult = GimbalAzimuthPolicy::calculate(afterRotation);

    QVERIFY(beforeResult.valid);
    QVERIFY(afterResult.valid);
    QVERIFY(anglesEqual(beforeResult.absoluteYawDegrees, 72.0));
    QVERIFY(anglesEqual(afterResult.absoluteYawDegrees, 72.0));
    QCOMPARE(beforeResult.source, GimbalAzimuthPolicy::Source::VehicleHeadingFallback);
    QCOMPARE(afterResult.source, GimbalAzimuthPolicy::Source::VehicleHeadingFallback);

    // A yaw command while locked still changes the body yaw and therefore the
    // calculated earth yaw instead of freezing the display at lock entry.
    afterRotation.quaternion = quaternionFromEulerDegrees(0.0, -25.0, -27.0);
    const auto afterYawCommand = GimbalAzimuthPolicy::calculate(afterRotation);
    QVERIFY(afterYawCommand.valid);
    QVERIFY(anglesEqual(afterYawCommand.absoluteYawDegrees, 98.0));
    QCOMPARE(afterYawCommand.source, GimbalAzimuthPolicy::Source::VehicleHeadingFallback);
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

void GimbalAzimuthPolicyTest::legacyReferenceModes_data() {
    QTest::addColumn<int>("reference");
    QTest::addColumn<bool>("yawLock");
    QTest::addColumn<double>("expectedYaw");
    QTest::addColumn<int>("expectedSource");

    using Reference = GimbalAzimuthPolicy::LegacyYawReference;
    using Source = GimbalAzimuthPolicy::Source;
    QTest::newRow("protocol-follow") << int(Reference::Protocol) << false << 30.0 << int(Source::LegacyVehicleHeading);
    QTest::newRow("protocol-lock") << int(Reference::Protocol) << true << 10.0 << int(Source::LegacyEarthFrame);
    QTest::newRow("vehicle-follow") << int(Reference::VehicleHeading) << false << 30.0
                                    << int(Source::ConfiguredLegacyVehicleHeading);
    QTest::newRow("vehicle-lock") << int(Reference::VehicleHeading) << true << 30.0
                                  << int(Source::ConfiguredLegacyVehicleHeading);
    QTest::newRow("earth-follow") << int(Reference::EarthNorth) << false << 10.0
                                  << int(Source::ConfiguredLegacyEarthFrame);
    QTest::newRow("earth-lock") << int(Reference::EarthNorth) << true << 10.0
                                << int(Source::ConfiguredLegacyEarthFrame);
}

void GimbalAzimuthPolicyTest::legacyReferenceModes() {
    QFETCH(int, reference);
    QFETCH(bool, yawLock);
    QFETCH(double, expectedYaw);
    QFETCH(int, expectedSource);

    auto input = legacyInput(static_cast<GimbalAzimuthPolicy::LegacyYawReference>(reference), yawLock, 20.0, 10.0);
    // Legacy frames cannot infer extension support from a decoded finite value.
    // Neither configured reference should silently start using this delta.
    input.deltaYawSupported = true;
    input.deltaYawAvailable = true;
    input.deltaYawRadians = 90.0 * kDegreesToRadians;

    const auto result = GimbalAzimuthPolicy::calculate(input);
    QVERIFY(result.valid);
    QVERIFY(anglesEqual(result.absoluteYawDegrees, expectedYaw));
    QCOMPARE(result.source, static_cast<GimbalAzimuthPolicy::Source>(expectedSource));
}

void GimbalAzimuthPolicyTest::configuredVehicleKeepsLoggedLockTransitionContinuous() {
    auto input = legacyInput(GimbalAzimuthPolicy::LegacyYawReference::VehicleHeading, false, 45.0, -171.738);
    const auto applyFlags = [&input](unsigned flags) {
        input.yawLock = (flags & 16U) != 0U;
        input.yawInVehicleFrame = (flags & 32U) != 0U;
        input.yawInEarthFrame = (flags & 64U) != 0U;
    };

    // Recorded no-frame flags and q values: the actual q motion was 0.351
    // degrees. With a configured body reference, a mode change cannot add a
    // 45-degree coordinate-system jump.
    applyFlags(12U);
    const auto follow = GimbalAzimuthPolicy::calculate(input);
    applyFlags(28U);
    input.quaternion = quaternionFromEulerDegrees(0.0, 0.0, -171.387);
    const auto locked = GimbalAzimuthPolicy::calculate(input);
    applyFlags(12U);
    const auto unlocked = GimbalAzimuthPolicy::calculate(input);

    QVERIFY(follow.valid);
    QVERIFY(locked.valid);
    QVERIFY(unlocked.valid);
    QVERIFY(anglesEqual(follow.absoluteYawDegrees, -126.738));
    QVERIFY(anglesEqual(locked.absoluteYawDegrees, -126.387));
    QVERIFY(anglesEqual(locked.absoluteYawDegrees - follow.absoluteYawDegrees, 0.351));
    QVERIFY(anglesEqual(unlocked.absoluteYawDegrees, locked.absoluteYawDegrees));
    QCOMPARE(follow.source, GimbalAzimuthPolicy::Source::ConfiguredLegacyVehicleHeading);
    QCOMPARE(locked.source, follow.source);
    QCOMPARE(unlocked.source, follow.source);
}

void GimbalAzimuthPolicyTest::reversedLegacyReplaysRecordedLockRotation() {
    // Actual WXYZ samples from 2026-09-07 at 12:00:26.642 and 12:00:42.343.
    // Both reported flags=28 (locked, no explicit frame) and roll near 180.
    // Rounded log angles are NOT substituted for the recorded quaternion.
    auto before = legacyInput(GimbalAzimuthPolicy::LegacyYawReference::VehicleHeading, true, 4.75517, 0.0);
    before.quaternion = {8.20792e-05, -0.300543, 0.953768, 1.9889e-05};
    auto after = before;
    after.vehicleHeadingDegrees = 312.904;
    after.quaternion = {6.03497e-05, 0.115557, 0.993301, 9.94521e-06};

    const auto oldBefore = GimbalAzimuthPolicy::calculate(before);
    const auto oldAfter = GimbalAzimuthPolicy::calculate(after);
    QVERIFY(oldBefore.valid);
    QVERIFY(oldAfter.valid);
    QVERIFY(anglesEqual(oldBefore.absoluteYawDegrees, -140.26438104076586));
    QVERIFY(anglesEqual(oldAfter.absoluteYawDegrees, 119.63249588800605));
    const double oldChange = GimbalAzimuthPolicy::wrap180(oldAfter.absoluteYawDegrees - oldBefore.absoluteYawDegrees);
    QVERIFY(anglesEqual(oldChange, -100.10312307122809));

    before.legacyYawReversed = true;
    after.legacyYawReversed = true;
    const auto correctedBefore = GimbalAzimuthPolicy::calculate(before);
    const auto correctedAfter = GimbalAzimuthPolicy::calculate(after);
    QVERIFY(correctedBefore.valid);
    QVERIFY(correctedAfter.valid);
    QVERIFY(anglesEqual(correctedBefore.absoluteYawDegrees, 149.77472104076586));
    QVERIFY(anglesEqual(correctedAfter.absoluteYawDegrees, 146.17550411199395));
    QCOMPARE(correctedBefore.source, GimbalAzimuthPolicy::Source::ConfiguredLegacyVehicleHeadingReversed);
    QCOMPARE(correctedAfter.source, correctedBefore.source);
    // The real input still contains drift and asynchronous sampling; the fix
    // removes the double addition, it does not falsely freeze the measurement.
    const double correctedChange =
        GimbalAzimuthPolicy::wrap180(correctedAfter.absoluteYawDegrees - correctedBefore.absoluteYawDegrees);
    QVERIFY(anglesEqual(correctedChange, -3.59921692877191));
}

void GimbalAzimuthPolicyTest::reversedLegacyKeepsModeTransitionContinuous() {
    auto input = legacyInput(GimbalAzimuthPolicy::LegacyYawReference::VehicleHeading, false, 15.0369, 0.0);
    input.quaternion = {0.162811, -0.275937, 0.946095, 0.0474903};
    input.legacyYawReversed = true;
    const auto follow = GimbalAzimuthPolicy::calculate(input);
    input.yawLock = true;
    const auto locked = GimbalAzimuthPolicy::calculate(input);
    input.yawLock = false;
    const auto unlocked = GimbalAzimuthPolicy::calculate(input);
    QVERIFY(follow.valid);
    QVERIFY(locked.valid);
    QVERIFY(unlocked.valid);
    QVERIFY(anglesEqual(follow.absoluteYawDegrees, 162.51741333969877));
    QVERIFY(anglesEqual(locked.absoluteYawDegrees, follow.absoluteYawDegrees));
    QVERIFY(anglesEqual(unlocked.absoluteYawDegrees, follow.absoluteYawDegrees));
    QCOMPARE(follow.source, GimbalAzimuthPolicy::Source::ConfiguredLegacyVehicleHeadingReversed);
    QCOMPARE(locked.source, follow.source);
    QCOMPARE(unlocked.source, follow.source);
}

void GimbalAzimuthPolicyTest::reversedLegacyReplaysSecondRecordedLockRotation() {
    // Second independent locked base rotation, 12:00:57.885 -> 12:01:04.135.
    // The raw heading changes +63.5991 degrees, crossing geographic north.
    auto before = legacyInput(GimbalAzimuthPolicy::LegacyYawReference::VehicleHeading, true, 311.902, 0.0);
    before.quaternion = {3.56248e-06, -0.0773886, -0.997001, -1.94334e-05};
    auto after = before;
    after.vehicleHeadingDegrees = 15.5011;
    after.quaternion = {0.000176855, -0.460539, 0.88764, -7.46735e-06};
    const auto oldBefore = GimbalAzimuthPolicy::calculate(before);
    const auto oldAfter = GimbalAzimuthPolicy::calculate(after);
    QVERIFY(oldBefore.valid);
    QVERIFY(oldAfter.valid);
    QVERIFY(anglesEqual(oldAfter.absoluteYawDegrees - oldBefore.absoluteYawDegrees, 127.31981703480322));
    before.legacyYawReversed = true;
    after.legacyYawReversed = true;
    const auto correctedBefore = GimbalAzimuthPolicy::calculate(before);
    const auto correctedAfter = GimbalAzimuthPolicy::calculate(after);
    QVERIFY(correctedBefore.valid);
    QVERIFY(correctedAfter.valid);
    QVERIFY(anglesEqual(correctedBefore.absoluteYawDegrees, 140.77895611773295));
    QVERIFY(anglesEqual(correctedAfter.absoluteYawDegrees, 140.65733908292975));
    QVERIFY(anglesEqual(correctedAfter.absoluteYawDegrees - correctedBefore.absoluteYawDegrees, -0.12161703480320));
}

void GimbalAzimuthPolicyTest::reversedLegacyFollowAndYawCommandsRemainLive() {
    auto input = legacyInput(GimbalAzimuthPolicy::LegacyYawReference::VehicleHeading, false, 30.0, 20.0);
    input.legacyYawReversed = true;
    const auto initial = GimbalAzimuthPolicy::calculate(input);
    QVERIFY(initial.valid);
    QVERIFY(anglesEqual(initial.absoluteYawDegrees, 10.0));
    input.vehicleHeadingDegrees += 90.0;
    const auto rotatedBase = GimbalAzimuthPolicy::calculate(input);
    QVERIFY(rotatedBase.valid);
    QVERIFY(anglesEqual(rotatedBase.absoluteYawDegrees, 100.0));

    // A joystick-induced feedback change must remain visible in either mode.
    for (const bool locked : {false, true}) {
        input.yawLock = locked;
        input.quaternion = quaternionFromEulerDegrees(180.0, 19.5, 45.0);
        const auto yawCommand = GimbalAzimuthPolicy::calculate(input);
        QVERIFY(yawCommand.valid);
        QVERIFY(anglesEqual(yawCommand.absoluteYawDegrees, 75.0));
        QCOMPARE(yawCommand.source, GimbalAzimuthPolicy::Source::ConfiguredLegacyVehicleHeadingReversed);
    }
}

void GimbalAzimuthPolicyTest::reversedLegacyPreservesBearingAcrossFullBaseTurn() {
    auto input = legacyInput(GimbalAzimuthPolicy::LegacyYawReference::VehicleHeading, true, 0.0, 0.0);
    input.legacyYawReversed = true;
    constexpr double worldYaw = 173.875;
    for (int step = 0; step <= 720; ++step) {
        input.vehicleHeadingDegrees = GimbalAzimuthPolicy::wrap180(-175.625 + step * 0.5);
        // Reversed feedback increases WITH base heading for a locked camera.
        input.quaternion = quaternionFromEulerDegrees(
            180.0, 19.53, GimbalAzimuthPolicy::wrap180(input.vehicleHeadingDegrees - worldYaw));
        const auto result = GimbalAzimuthPolicy::calculate(input);
        QVERIFY(result.valid);
        QVERIFY(anglesEqual(result.absoluteYawDegrees, worldYaw));
    }
}

void GimbalAzimuthPolicyTest::reversedLegacyHandlesPitchRollAndQuaternionSign() {
    auto input = legacyInput(GimbalAzimuthPolicy::LegacyYawReference::VehicleHeading, true, 0.125, 179.875);
    input.legacyYawReversed = true;
    for (const double roll : {0.0, 37.0, 180.0, -180.0}) {
        for (const double pitch : {-80.0, -19.53, 0.0, 19.53, 80.0}) {
            input.quaternion = quaternionFromEulerDegrees(roll, pitch, 179.875);
            const auto beforeSignChange = GimbalAzimuthPolicy::calculate(input);
            QVERIFY(beforeSignChange.valid);
            QVERIFY(anglesEqual(beforeSignChange.absoluteYawDegrees, -179.75));
            for (double &component : input.quaternion) {
                component *= -4.5;
            }
            const auto afterSignChange = GimbalAzimuthPolicy::calculate(input);
            QVERIFY(afterSignChange.valid);
            QVERIFY(anglesEqual(afterSignChange.absoluteYawDegrees, beforeSignChange.absoluteYawDegrees));
        }
    }
    input.quaternion = quaternionFromEulerDegrees(180.0, 19.53, -179.875);
    const auto acrossBoundary = GimbalAzimuthPolicy::calculate(input);
    QVERIFY(acrossBoundary.valid);
    QVERIFY(anglesEqual(acrossBoundary.absoluteYawDegrees, -180.0));
}

void GimbalAzimuthPolicyTest::reversedLegacyDoesNotOverrideOtherFrames_data() {
    QTest::addColumn<int>("reference");
    QTest::addColumn<bool>("locked");
    QTest::addColumn<int>("frame");
    QTest::addColumn<bool>("delta");
    QTest::addColumn<double>("expectedYaw");
    QTest::newRow("protocol-follow") << 0 << false << 0 << true << 90.0;
    QTest::newRow("protocol-lock") << 0 << true << 0 << true << 20.0;
    QTest::newRow("configured-earth-follow") << 2 << false << 0 << true << 20.0;
    QTest::newRow("configured-earth-lock") << 2 << true << 0 << true << 20.0;
    QTest::newRow("explicit-earth-follow") << 1 << false << 64 << true << 20.0;
    QTest::newRow("explicit-earth-lock") << 1 << true << 64 << true << 20.0;
    QTest::newRow("explicit-vehicle-fallback-follow") << 1 << false << 32 << false << 90.0;
    QTest::newRow("explicit-vehicle-fallback-lock") << 1 << true << 32 << false << 90.0;
    QTest::newRow("explicit-vehicle-delta-follow") << 1 << false << 32 << true << 150.0;
    QTest::newRow("explicit-vehicle-delta-lock") << 1 << true << 32 << true << 150.0;
}

void GimbalAzimuthPolicyTest::reversedLegacyDoesNotOverrideOtherFrames() {
    QFETCH(int, reference);
    QFETCH(bool, locked);
    QFETCH(int, frame);
    QFETCH(bool, delta);
    QFETCH(double, expectedYaw);
    auto input = legacyInput(static_cast<GimbalAzimuthPolicy::LegacyYawReference>(reference), locked, 70.0, 20.0);
    input.quaternion = quaternionFromEulerDegrees(180.0, 19.53, 20.0);
    input.yawInVehicleFrame = frame == 32;
    input.yawInEarthFrame = frame == 64;
    input.deltaYawSupported = delta;
    input.deltaYawAvailable = delta;
    input.deltaYawRadians = 130.0 * kDegreesToRadians;
    const auto standard = GimbalAzimuthPolicy::calculate(input);
    input.legacyYawReversed = true;
    const auto reversed = GimbalAzimuthPolicy::calculate(input);
    QVERIFY(standard.valid);
    QVERIFY(reversed.valid);
    QVERIFY(anglesEqual(reversed.absoluteYawDegrees, expectedYaw));
    QVERIFY(anglesEqual(reversed.absoluteYawDegrees, standard.absoluteYawDegrees));
    QCOMPARE(reversed.source, standard.source);
}

void GimbalAzimuthPolicyTest::reversedLegacyStillRequiresValidInputs() {
    auto input = legacyInput(GimbalAzimuthPolicy::LegacyYawReference::VehicleHeading, true, 70.0, 20.0);
    input.legacyYawReversed = true;
    input.vehicleHeadingAvailable = false;
    const auto missingHeading = GimbalAzimuthPolicy::calculate(input);
    QVERIFY(!missingHeading.valid);
    QCOMPARE(missingHeading.error, GimbalAzimuthPolicy::Error::MissingEarthReference);
    input.vehicleHeadingAvailable = true;
    input.vehicleHeadingDegrees = std::numeric_limits<double>::quiet_NaN();
    QVERIFY(!GimbalAzimuthPolicy::calculate(input).valid);
    input.vehicleHeadingDegrees = 70.0;
    input.quaternion = {0.0, 0.0, 0.0, 0.0};
    const auto invalidQuaternion = GimbalAzimuthPolicy::calculate(input);
    QVERIFY(!invalidQuaternion.valid);
    QCOMPARE(invalidQuaternion.error, GimbalAzimuthPolicy::Error::InvalidQuaternion);
    input.quaternion = quaternionFromEulerDegrees(180.0, 19.53, 20.0);
    input.yawInEarthFrame = true;
    input.yawInVehicleFrame = true;
    const auto conflictingFrames = GimbalAzimuthPolicy::calculate(input);
    QVERIFY(!conflictingFrames.valid);
    QCOMPARE(conflictingFrames.error, GimbalAzimuthPolicy::Error::ConflictingFrameFlags);
}

void GimbalAzimuthPolicyTest::configuredVehicleKeepsBearingDuringDenseBaseRotation() {
    auto input = legacyInput(GimbalAzimuthPolicy::LegacyYawReference::VehicleHeading, true, 0.0, 0.0);
    constexpr double worldYaw = 173.875;

    // Start already locked and rotate a full turn in sub-degree increments;
    // no startup observation or motion threshold is required.
    for (int index = 0; index <= 720; ++index) {
        input.vehicleHeadingDegrees = GimbalAzimuthPolicy::wrap180(-175.625 + index * 0.5);
        input.quaternion = quaternionFromEulerDegrees(
            0.0, -35.0, GimbalAzimuthPolicy::wrap180(worldYaw - input.vehicleHeadingDegrees));
        const auto result = GimbalAzimuthPolicy::calculate(input);
        QVERIFY(result.valid);
        QVERIFY(anglesEqual(result.absoluteYawDegrees, worldYaw));
        QCOMPARE(result.source, GimbalAzimuthPolicy::Source::ConfiguredLegacyVehicleHeading);
    }
}

void GimbalAzimuthPolicyTest::configuredVehicleHandlesAlternatingHeadingPackets() {
    auto input = legacyInput(GimbalAzimuthPolicy::LegacyYawReference::VehicleHeading, true, 20.0, 50.0);

    // q arrives twice as often as heading while the camera stays at 70 degrees.
    // A latest-sample transform has a 4-degree transient on odd packets.
    // It must recover on every paired packet and never switch to direct q.
    for (int index = 0; index <= 40; ++index) {
        input.vehicleHeadingDegrees = 20.0 + (index / 2) * 8.0;
        input.quaternion = quaternionFromEulerDegrees(0.0, 0.0, 50.0 - index * 4.0);
        const auto result = GimbalAzimuthPolicy::calculate(input);
        QVERIFY(result.valid);
        QVERIFY(anglesEqual(result.absoluteYawDegrees, index % 2 == 0 ? 70.0 : 66.0));
        QCOMPARE(result.source, GimbalAzimuthPolicy::Source::ConfiguredLegacyVehicleHeading);
    }
}

void GimbalAzimuthPolicyTest::configuredVehicleKeepsFrameAfterHeadingCatchesUp() {
    auto input = legacyInput(GimbalAzimuthPolicy::LegacyYawReference::VehicleHeading, true, 20.0, 50.0);
    const std::array<std::array<double, 3>, 5> samples{{
        {20.0, 50.0, 70.0},
        {40.0, 30.0, 70.0},
        {40.0, 20.0, 60.0},  // q updates before the matching heading.
        {50.0, 20.0, 70.0},  // Heading catches up: recover, do not select q=20.
        {51.0, 19.0, 70.0},
    }};

    for (const auto& sample : samples) {
        input.vehicleHeadingDegrees = sample[0];
        input.quaternion = quaternionFromEulerDegrees(0.0, 0.0, sample[1]);
        const auto result = GimbalAzimuthPolicy::calculate(input);
        QVERIFY(result.valid);
        QVERIFY(anglesEqual(result.absoluteYawDegrees, sample[2]));
        QCOMPARE(result.source, GimbalAzimuthPolicy::Source::ConfiguredLegacyVehicleHeading);
    }
}

void GimbalAzimuthPolicyTest::configuredVehicleFollowTracksBaseRotation() {
    auto input = legacyInput(GimbalAzimuthPolicy::LegacyYawReference::VehicleHeading, false, 30.0, -20.0);
    const auto before = GimbalAzimuthPolicy::calculate(input);
    input.vehicleHeadingDegrees = 120.0;
    const auto after = GimbalAzimuthPolicy::calculate(input);

    QVERIFY(before.valid);
    QVERIFY(after.valid);
    QVERIFY(anglesEqual(before.absoluteYawDegrees, 10.0));
    QVERIFY(anglesEqual(after.absoluteYawDegrees, 100.0));
    QCOMPARE(before.source, GimbalAzimuthPolicy::Source::ConfiguredLegacyVehicleHeading);
    QCOMPARE(after.source, before.source);
}

void GimbalAzimuthPolicyTest::configuredEarthIgnoresHeadingAndLockMode() {
    auto input = legacyInput(GimbalAzimuthPolicy::LegacyYawReference::EarthNorth, true, 0.0, 70.125);
    for (bool yawLock : {false, true}) {
        input.yawLock = yawLock;
        for (int index = 0; index <= 36; ++index) {
            input.vehicleHeadingDegrees = index * 10.0;
            const auto result = GimbalAzimuthPolicy::calculate(input);
            QVERIFY(result.valid);
            QVERIFY(anglesEqual(result.absoluteYawDegrees, 70.125));
            QCOMPARE(result.source, GimbalAzimuthPolicy::Source::ConfiguredLegacyEarthFrame);
        }
    }
}

void GimbalAzimuthPolicyTest::configuredYawCommandsChangeLockedBearing() {
    for (auto reference : {GimbalAzimuthPolicy::LegacyYawReference::VehicleHeading,
                           GimbalAzimuthPolicy::LegacyYawReference::EarthNorth}) {
        auto input = legacyInput(reference, true, 30.0, 10.0);
        const auto before = GimbalAzimuthPolicy::calculate(input);
        input.quaternion = quaternionFromEulerDegrees(0.0, 0.0, 25.0);
        const auto after = GimbalAzimuthPolicy::calculate(input);

        QVERIFY(before.valid);
        QVERIFY(after.valid);
        QVERIFY(anglesEqual(after.absoluteYawDegrees - before.absoluteYawDegrees, 15.0));
        QCOMPARE(after.source, before.source);
    }
}

void GimbalAzimuthPolicyTest::legacyPitchChangesDoNotChangeAzimuth() {
    for (auto reference :
         {GimbalAzimuthPolicy::LegacyYawReference::Protocol, GimbalAzimuthPolicy::LegacyYawReference::VehicleHeading,
          GimbalAzimuthPolicy::LegacyYawReference::EarthNorth}) {
        for (bool yawLock : {false, true}) {
            auto input = legacyInput(reference, yawLock, 30.25, 42.125);
            const auto baseline = GimbalAzimuthPolicy::calculate(input);
            QVERIFY(baseline.valid);
            // Avoid a vertical optical axis, where azimuth is undefined.
            for (double pitch : {-80.0, -45.0, 0.0, 30.0, 80.0}) {
                input.quaternion = quaternionFromEulerDegrees(12.0, pitch, 42.125);
                const auto result = GimbalAzimuthPolicy::calculate(input);
                QVERIFY(result.valid);
                QVERIFY(anglesEqual(result.absoluteYawDegrees, baseline.absoluteYawDegrees));
                QCOMPARE(result.source, baseline.source);
            }
        }
    }
}

void GimbalAzimuthPolicyTest::configuredVehiclePreservesFractionalYawAcrossNorth() {
    GimbalHeadingTelemetry headingTelemetry;
    QVERIFY(headingTelemetry.update(GimbalHeadingTelemetry::Source::Attitude, 359.75, 100, 100U));
    auto input = legacyInput(GimbalAzimuthPolicy::LegacyYawReference::VehicleHeading, true, 0.0, 0.5);
    auto heading = headingTelemetry.heading(100);
    input.vehicleHeadingAvailable = heading.valid;
    input.vehicleHeadingDegrees = heading.yawDegrees;
    const auto before = GimbalAzimuthPolicy::calculate(input);

    QVERIFY(headingTelemetry.update(GimbalHeadingTelemetry::Source::Attitude, 0.125, 200, 200U));
    heading = headingTelemetry.heading(200);
    input.vehicleHeadingAvailable = heading.valid;
    input.vehicleHeadingDegrees = heading.yawDegrees;
    const auto after = GimbalAzimuthPolicy::calculate(input);

    QVERIFY(before.valid);
    QVERIFY(after.valid);
    QVERIFY(anglesEqual(before.absoluteYawDegrees, 0.25));
    QVERIFY(anglesEqual(after.absoluteYawDegrees, 0.625));
    QVERIFY(anglesEqual(after.absoluteYawDegrees - before.absoluteYawDegrees, 0.375));
}

void GimbalAzimuthPolicyTest::configuredVehicleWithoutHeadingIsInvalid() {
    auto input = legacyInput(GimbalAzimuthPolicy::LegacyYawReference::VehicleHeading, true, 20.0, 50.0);
    input.deltaYawSupported = true;
    input.deltaYawAvailable = true;
    input.deltaYawRadians = 20.0 * kDegreesToRadians;

    for (bool yawLock : {false, true}) {
        input.yawLock = yawLock;
        input.vehicleHeadingAvailable = false;
        const auto missing = GimbalAzimuthPolicy::calculate(input);
        QVERIFY(!missing.valid);
        QCOMPARE(missing.error, GimbalAzimuthPolicy::Error::MissingEarthReference);

        input.vehicleHeadingAvailable = true;
        input.vehicleHeadingDegrees = std::numeric_limits<double>::quiet_NaN();
        const auto nonFinite = GimbalAzimuthPolicy::calculate(input);
        QVERIFY(!nonFinite.valid);
        QCOMPARE(nonFinite.error, GimbalAzimuthPolicy::Error::MissingEarthReference);
    }
}

void GimbalAzimuthPolicyTest::configuredEarthNeedsNoHeading() {
    auto input = legacyInput(GimbalAzimuthPolicy::LegacyYawReference::EarthNorth, true, 0.0, 42.125);
    input.vehicleHeadingAvailable = false;
    input.vehicleHeadingDegrees = std::numeric_limits<double>::quiet_NaN();
    for (bool yawLock : {false, true}) {
        input.yawLock = yawLock;
        const auto result = GimbalAzimuthPolicy::calculate(input);
        QVERIFY(result.valid);
        QVERIFY(anglesEqual(result.absoluteYawDegrees, 42.125));
        QCOMPARE(result.source, GimbalAzimuthPolicy::Source::ConfiguredLegacyEarthFrame);
    }
}

void GimbalAzimuthPolicyTest::explicitFramesOverrideLegacyReference() {
    for (auto reference :
         {GimbalAzimuthPolicy::LegacyYawReference::Protocol, GimbalAzimuthPolicy::LegacyYawReference::VehicleHeading,
          GimbalAzimuthPolicy::LegacyYawReference::EarthNorth}) {
        for (bool yawLock : {false, true}) {
            auto input = legacyInput(reference, yawLock, 20.0, 50.0);
            input.yawInEarthFrame = true;
            input.vehicleHeadingAvailable = false;
            const auto earth = GimbalAzimuthPolicy::calculate(input);
            QVERIFY(earth.valid);
            QVERIFY(anglesEqual(earth.absoluteYawDegrees, 50.0));
            QCOMPARE(earth.source, GimbalAzimuthPolicy::Source::ReportedEarthFrame);

            input.yawInEarthFrame = false;
            input.yawInVehicleFrame = true;
            input.deltaYawSupported = true;
            input.deltaYawAvailable = true;
            input.deltaYawRadians = 40.0 * kDegreesToRadians;
            const auto delta = GimbalAzimuthPolicy::calculate(input);
            QVERIFY(delta.valid);
            QVERIFY(anglesEqual(delta.absoluteYawDegrees, 90.0));
            QCOMPARE(delta.source, GimbalAzimuthPolicy::Source::DeltaYaw);

            input.deltaYawAvailable = false;
            input.vehicleHeadingAvailable = true;
            const auto vehicle = GimbalAzimuthPolicy::calculate(input);
            QVERIFY(vehicle.valid);
            QVERIFY(anglesEqual(vehicle.absoluteYawDegrees, 70.0));
            QCOMPARE(vehicle.source, GimbalAzimuthPolicy::Source::VehicleHeadingFallback);
        }
    }
}

void GimbalAzimuthPolicyTest::configuredReferenceStillRejectsConflictingFrames() {
    for (auto reference : {GimbalAzimuthPolicy::LegacyYawReference::VehicleHeading,
                           GimbalAzimuthPolicy::LegacyYawReference::EarthNorth}) {
        auto input = legacyInput(reference, true, 20.0, 50.0);
        input.yawInEarthFrame = true;
        input.yawInVehicleFrame = true;
        const auto result = GimbalAzimuthPolicy::calculate(input);
        QVERIFY(!result.valid);
        QCOMPARE(result.error, GimbalAzimuthPolicy::Error::ConflictingFrameFlags);
    }
}

void GimbalAzimuthPolicyTest::configuredReferenceStillRejectsInvalidQuaternion() {
    for (auto reference : {GimbalAzimuthPolicy::LegacyYawReference::VehicleHeading,
                           GimbalAzimuthPolicy::LegacyYawReference::EarthNorth}) {
        auto input = legacyInput(reference, true, 20.0, 50.0);
        input.quaternion = {0.0, 0.0, 0.0, 0.0};
        const auto result = GimbalAzimuthPolicy::calculate(input);
        QVERIFY(!result.valid);
        QCOMPARE(result.error, GimbalAzimuthPolicy::Error::InvalidQuaternion);
    }
}

void GimbalAzimuthPolicyTest::headingExpiryInvalidatesConfiguredVehicleResult() {
    GimbalHeadingTelemetry headingTelemetry;
    QVERIFY(headingTelemetry.update(GimbalHeadingTelemetry::Source::Quaternion, 20.0, 100, 100U));
    auto input = legacyInput(GimbalAzimuthPolicy::LegacyYawReference::VehicleHeading, true, 0.0, 50.0);
    auto heading = headingTelemetry.heading(2099);
    input.vehicleHeadingAvailable = heading.valid;
    input.vehicleHeadingDegrees = heading.yawDegrees;
    const auto recent = GimbalAzimuthPolicy::calculate(input);
    QVERIFY(recent.valid);
    QVERIFY(anglesEqual(recent.absoluteYawDegrees, 70.0));

    // New gimbal q does not renew an independently expired FC heading.
    input.quaternion = quaternionFromEulerDegrees(0.0, 0.0, 49.0);
    heading = headingTelemetry.heading(2101);
    input.vehicleHeadingAvailable = heading.valid;
    input.vehicleHeadingDegrees = heading.yawDegrees;
    const auto expired = GimbalAzimuthPolicy::calculate(input);
    QVERIFY(!expired.valid);
    QCOMPARE(expired.error, GimbalAzimuthPolicy::Error::MissingEarthReference);

    input.legacyYawReference = GimbalAzimuthPolicy::LegacyYawReference::EarthNorth;
    const auto earth = GimbalAzimuthPolicy::calculate(input);
    QVERIFY(earth.valid);
    QVERIFY(anglesEqual(earth.absoluteYawDegrees, 49.0));
}

void GimbalAzimuthPolicyTest::invalidHeadingCannotRenewConfiguredVehicleResult() {
    GimbalHeadingTelemetry headingTelemetry;
    QVERIFY(headingTelemetry.update(GimbalHeadingTelemetry::Source::Attitude, 20.0, 100, 100U));
    QVERIFY(!headingTelemetry.update(GimbalHeadingTelemetry::Source::Attitude, std::numeric_limits<double>::quiet_NaN(),
                                     2000, 2000U));

    auto input = legacyInput(GimbalAzimuthPolicy::LegacyYawReference::VehicleHeading, true, 0.0, 50.0);
    const auto heading = headingTelemetry.heading(2101);
    input.vehicleHeadingAvailable = heading.valid;
    input.vehicleHeadingDegrees = heading.yawDegrees;
    const auto result = GimbalAzimuthPolicy::calculate(input);
    QVERIFY(!result.valid);
    QCOMPARE(result.error, GimbalAzimuthPolicy::Error::MissingEarthReference);
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
