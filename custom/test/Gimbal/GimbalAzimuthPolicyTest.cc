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
#include "GimbalYawLockResolver.h"

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

GimbalAzimuthPolicy::Result azimuthResult(double yawDegrees, GimbalAzimuthPolicy::Source source) {
    GimbalAzimuthPolicy::Result result;
    result.valid = true;
    result.absoluteYawDegrees = GimbalAzimuthPolicy::wrap180(yawDegrees);
    result.source = source;
    return result;
}

GimbalYawLockResolver::Input resolverInput(
    bool eligible, double standardYawDegrees, double reportedYawDegrees,
    GimbalAzimuthPolicy::Source standardSource = GimbalAzimuthPolicy::Source::VehicleHeadingFallback) {
    GimbalYawLockResolver::Input input;
    input.mode = eligible ? GimbalYawLockResolver::CompatibilityMode::ExplicitVehicleFrame
                          : GimbalYawLockResolver::CompatibilityMode::None;
    input.standardResult = azimuthResult(standardYawDegrees, standardSource);
    input.reportedYawResult = azimuthResult(reportedYawDegrees, GimbalAzimuthPolicy::Source::ReportedEarthFrame);
    return input;
}

GimbalYawLockResolver::Input resolverInputWithHeading(double standardYawDegrees, double reportedYawDegrees,
                                                      double vehicleHeadingYawDegrees) {
    GimbalYawLockResolver::Input input =
        resolverInput(true, standardYawDegrees, reportedYawDegrees, GimbalAzimuthPolicy::Source::DeltaYaw);
    input.vehicleHeadingResult =
        azimuthResult(vehicleHeadingYawDegrees, GimbalAzimuthPolicy::Source::VehicleHeadingFallback);
    return input;
}

GimbalYawLockResolver::Input legacyResolverInput(
    bool yawLock, double vehicleHeadingDegrees, double reportedYawDegrees, bool transitionReferenceAvailable = false,
    double transitionReferenceYawDegrees = 0.0,
    double transitionReportedYawReferenceDegrees = std::numeric_limits<double>::quiet_NaN(),
    bool vehicleHeadingAvailable = true) {
    GimbalAzimuthPolicy::Input policyInput;
    policyInput.quaternion = quaternionFromEulerDegrees(0.0, 0.0, reportedYawDegrees);
    policyInput.yawLock = yawLock;
    policyInput.vehicleHeadingAvailable = vehicleHeadingAvailable;
    policyInput.vehicleHeadingDegrees = vehicleHeadingDegrees;

    GimbalYawLockResolver::Input input;
    input.mode = GimbalYawLockResolver::modeForStatus(policyInput.yawLock, policyInput.yawInVehicleFrame,
                                                      policyInput.yawInEarthFrame);
    input.standardResult = GimbalAzimuthPolicy::calculate(policyInput);
    GimbalAzimuthPolicy::Input reportedYawInput = policyInput;
    reportedYawInput.yawInVehicleFrame = false;
    reportedYawInput.yawInEarthFrame = true;
    input.reportedYawResult = GimbalAzimuthPolicy::calculate(reportedYawInput);
    if (input.mode == GimbalYawLockResolver::CompatibilityMode::LegacyNoFrame && vehicleHeadingAvailable) {
        policyInput.yawInVehicleFrame = true;
        policyInput.deltaYawSupported = false;
        policyInput.deltaYawAvailable = false;
        input.vehicleHeadingResult = GimbalAzimuthPolicy::calculate(policyInput);
    }
    input.transitionReferenceAvailable = transitionReferenceAvailable;
    input.transitionReferenceYawDegrees = transitionReferenceYawDegrees;
    input.transitionReportedYawReferenceAvailable =
        transitionReferenceAvailable && std::isfinite(transitionReportedYawReferenceDegrees);
    input.transitionReportedYawReferenceDegrees = transitionReportedYawReferenceDegrees;
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
    void resolverClassifiesLoggedLegacyFlags();
    void resolverKeepsLoggedLegacyTransitionContinuous();
    void resolverKeepsCompliantLegacyEarthFrameAtTransition();
    void resolverDoesNotHideLegacyYawCommandAtTransition();
    void resolverCorrectsDelayedLegacyFrameChange();
    void resolverLearnsLegacyVehicleHeadingWhenStartingLocked();
    void resolverLeavesAmbiguousLegacyTransitionOnProtocolResult();
    void resolverRequiresMotionAfterFollowForReportedYaw();
    void resolverRequiresMotionAfterFollowForStandard();
    void resolverLearnsReportedYawWhenStartingLocked();
    void resolverToleratesLockedYawDrift();
    void resolverLearnsCompliantVehicleFrameWhenStartingLocked();
    void resolverReanchorsDuringCameraMotion();
    void resolverHandlesLockFlagQuaternionSkew();
    void resolverHandlesCoincidentYawAtLock();
    void resolverReevaluatesChangedProtocolSource();
    void resolverReevaluatesReportedYawSelection();
    void resolverReevaluatesStandardSelection();
    void resolverRetainsSelectionAcrossInvalidSample();
    void resolverUsesVehicleHeadingWhenDeltaFreezes();
    void resolverUsesVehicleHeadingWhenSupportedDeltaStaysZero();
    void resolverKeepsDeltaWhenHeadingAlsoStable();
    void resolverKeepsReportedYawWithHeadingCandidate();
    void resolverReevaluatesVehicleHeadingSelection();
    void resolverDropsVehicleHeadingWhenReferenceExpires();
    void resolverUsesWrappedVehicleHeadingEvidence();
    void resolverUsesWrappedMotionEvidence();
    void resolverResetDoesNotReuseCompatibilitySelection();
    void resolverDetectsSenderRestartAtZeroBootTime();
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

void GimbalAzimuthPolicyTest::resolverClassifiesLoggedLegacyFlags() {
    constexpr unsigned kYawLockFlag = 16U;
    constexpr unsigned kYawInVehicleFrameFlag = 32U;
    constexpr unsigned kYawInEarthFrameFlag = 64U;
    const auto modeForFlags = [=](unsigned flags) {
        return GimbalYawLockResolver::modeForStatus(
            (flags & kYawLockFlag) != 0U, (flags & kYawInVehicleFrameFlag) != 0U, (flags & kYawInEarthFrameFlag) != 0U);
    };

    // The captured bridge changed only YAW_LOCK: Follow flags=12 and Lock
    // flags=28. Neither packet declared a yaw frame, so Lock must enter the
    // legacy ambiguity resolver instead of bypassing it.
    QCOMPARE(modeForFlags(12U), GimbalYawLockResolver::CompatibilityMode::None);
    QCOMPARE(modeForFlags(28U), GimbalYawLockResolver::CompatibilityMode::LegacyNoFrame);
    QCOMPARE(modeForFlags(kYawLockFlag | kYawInVehicleFrameFlag),
             GimbalYawLockResolver::CompatibilityMode::ExplicitVehicleFrame);
    QCOMPARE(modeForFlags(kYawLockFlag | kYawInEarthFrameFlag), GimbalYawLockResolver::CompatibilityMode::None);
    QCOMPARE(modeForFlags(kYawLockFlag | kYawInVehicleFrameFlag | kYawInEarthFrameFlag),
             GimbalYawLockResolver::CompatibilityMode::None);
}

void GimbalAzimuthPolicyTest::resolverKeepsLoggedLegacyTransitionContinuous() {
    GimbalYawLockResolver::State state;

    const auto follow = GimbalYawLockResolver::update(state, legacyResolverInput(false, 45.0, -171.738));
    QVERIFY(anglesEqual(follow.azimuth.absoluteYawDegrees, -126.738, 1e-6));
    QCOMPARE(follow.azimuth.source, GimbalAzimuthPolicy::Source::LegacyVehicleHeading);

    // Real log values: the first Lock q changed by just 0.351 degrees, while
    // the old protocol-only path dropped the 45-degree vehicle heading and
    // produced a 44.649-degree display jump.
    const auto lockInput = legacyResolverInput(true, 45.0, -171.387, true, follow.azimuth.absoluteYawDegrees, -171.738);
    const auto lock = GimbalYawLockResolver::update(state, lockInput);
    QCOMPARE(lock.selection, GimbalYawLockResolver::Selection::VehicleHeading);
    QCOMPARE(lock.azimuth.source, GimbalAzimuthPolicy::Source::YawLockVehicleHeadingCompatibility);
    QVERIFY(anglesEqual(lock.azimuth.absoluteYawDegrees, -126.387, 1e-6));
    QVERIFY(anglesEqual(lock.azimuth.absoluteYawDegrees - follow.azimuth.absoluteYawDegrees, 0.351, 1e-6));

    // With the camera still earth-fixed, a 45-degree base turn is cancelled by
    // body q. The learned heading+q result therefore remains unchanged.
    const auto baseTurn = GimbalYawLockResolver::update(state, legacyResolverInput(true, 90.0, 143.613));
    QCOMPARE(baseTurn.selection, GimbalYawLockResolver::Selection::VehicleHeading);
    QVERIFY(anglesEqual(baseTurn.azimuth.absoluteYawDegrees, -126.387, 1e-6));

    // Follow is still the native legacy heading+q calculation. Leaving Lock
    // clears compatibility state without introducing another reference jump.
    const auto unlock = GimbalYawLockResolver::update(state, legacyResolverInput(false, 90.0, 143.613));
    QCOMPARE(unlock.selection, GimbalYawLockResolver::Selection::Undecided);
    QCOMPARE(unlock.azimuth.source, GimbalAzimuthPolicy::Source::LegacyVehicleHeading);
    QVERIFY(anglesEqual(unlock.azimuth.absoluteYawDegrees, -126.387, 1e-6));
}

void GimbalAzimuthPolicyTest::resolverKeepsCompliantLegacyEarthFrameAtTransition() {
    GimbalYawLockResolver::State state;

    const auto follow = GimbalYawLockResolver::update(state, legacyResolverInput(false, 45.0, -171.738));
    const auto lock = GimbalYawLockResolver::update(
        state, legacyResolverInput(true, 45.0, -126.387, true, follow.azimuth.absoluteYawDegrees, -171.738));

    // A compliant legacy sender changes q to earth yaw at Lock entry. Direct q
    // is continuous, so the protocol interpretation wins instead of applying
    // heading a second time.
    QCOMPARE(lock.selection, GimbalYawLockResolver::Selection::Standard);
    QCOMPARE(lock.azimuth.source, GimbalAzimuthPolicy::Source::LegacyEarthFrame);
    QVERIFY(anglesEqual(lock.azimuth.absoluteYawDegrees, -126.387, 1e-6));

    const auto baseTurn = GimbalYawLockResolver::update(state, legacyResolverInput(true, 90.0, -126.2));
    QCOMPARE(baseTurn.selection, GimbalYawLockResolver::Selection::Standard);
    QCOMPARE(baseTurn.azimuth.source, GimbalAzimuthPolicy::Source::LegacyEarthFrame);
    QVERIFY(anglesEqual(baseTurn.azimuth.absoluteYawDegrees, -126.2, 1e-6));
}

void GimbalAzimuthPolicyTest::resolverDoesNotHideLegacyYawCommandAtTransition() {
    GimbalYawLockResolver::State state;

    const auto follow = GimbalYawLockResolver::update(state, legacyResolverInput(false, 45.0, -171.738));
    const auto lock = GimbalYawLockResolver::update(
        state, legacyResolverInput(true, 45.0, -165.738, true, follow.azimuth.absoluteYawDegrees, -171.738));

    // A compliant sender can enter earth-frame Lock while a yaw command is
    // changing the actual world bearing. heading+q happens to remain within
    // the broad continuity window here, but raw q moved 6 degrees. Do not hide
    // the command by selecting the vendor body-frame interpretation.
    QCOMPARE(lock.selection, GimbalYawLockResolver::Selection::Undecided);
    QCOMPARE(lock.azimuth.source, GimbalAzimuthPolicy::Source::LegacyEarthFrame);
    QVERIFY(anglesEqual(lock.azimuth.absoluteYawDegrees, -165.738, 1e-6));
}

void GimbalAzimuthPolicyTest::resolverCorrectsDelayedLegacyFrameChange() {
    GimbalYawLockResolver::State state;

    const auto follow = GimbalYawLockResolver::update(state, legacyResolverInput(false, 45.0, -171.738));
    const auto earlyLock = GimbalYawLockResolver::update(
        state, legacyResolverInput(true, 45.0, -171.387, true, follow.azimuth.absoluteYawDegrees, -171.738));
    QCOMPARE(earlyLock.selection, GimbalYawLockResolver::Selection::VehicleHeading);

    // Some senders update YAW_LOCK before changing q from body to earth. The
    // provider keeps the transition reference for one second, allowing the
    // resolver to switch as soon as the new q representation is unambiguous.
    const auto settledLock = GimbalYawLockResolver::update(
        state, legacyResolverInput(true, 45.0, -126.387, true, follow.azimuth.absoluteYawDegrees, -171.738));
    QCOMPARE(settledLock.selection, GimbalYawLockResolver::Selection::Standard);
    QCOMPARE(settledLock.azimuth.source, GimbalAzimuthPolicy::Source::LegacyEarthFrame);
    QVERIFY(anglesEqual(settledLock.azimuth.absoluteYawDegrees, -126.387, 1e-6));
}

void GimbalAzimuthPolicyTest::resolverLearnsLegacyVehicleHeadingWhenStartingLocked() {
    GimbalYawLockResolver::State state;

    // With no preceding Follow sample there is no information-theoretic way
    // to classify the first packet. Start with the MAVLink legacy result.
    const auto first = GimbalYawLockResolver::update(state, legacyResolverInput(true, 45.0, -171.387));
    QCOMPARE(first.selection, GimbalYawLockResolver::Selection::Undecided);
    QCOMPARE(first.azimuth.source, GimbalAzimuthPolicy::Source::LegacyEarthFrame);
    QVERIFY(anglesEqual(first.azimuth.absoluteYawDegrees, -171.387, 1e-6));

    // Base motion supplies the missing evidence: direct q moves by 45 degrees,
    // while heading+q remains at the fixed world bearing.
    const auto learned = GimbalYawLockResolver::update(state, legacyResolverInput(true, 90.0, 143.613));
    QCOMPARE(learned.selection, GimbalYawLockResolver::Selection::VehicleHeading);
    QCOMPARE(learned.azimuth.source, GimbalAzimuthPolicy::Source::YawLockVehicleHeadingCompatibility);
    QVERIFY(anglesEqual(learned.azimuth.absoluteYawDegrees, -126.387, 1e-6));
}

void GimbalAzimuthPolicyTest::resolverLeavesAmbiguousLegacyTransitionOnProtocolResult() {
    GimbalYawLockResolver::State state;

    const auto follow = GimbalYawLockResolver::update(state, legacyResolverInput(false, 2.0, 68.0));
    const auto lock = GimbalYawLockResolver::update(
        state, legacyResolverInput(true, 2.0, 68.2, true, follow.azimuth.absoluteYawDegrees, 68.0));

    // Near North the two hypotheses differ by too little to justify a vendor
    // override. Preserve the protocol-safe direct-q result until real motion
    // creates at least the configured advantage.
    QCOMPARE(lock.selection, GimbalYawLockResolver::Selection::Undecided);
    QCOMPARE(lock.azimuth.source, GimbalAzimuthPolicy::Source::LegacyEarthFrame);
    QVERIFY(anglesEqual(lock.azimuth.absoluteYawDegrees, 68.2, 1e-6));
}

void GimbalAzimuthPolicyTest::resolverRequiresMotionAfterFollowForReportedYaw() {
    GimbalYawLockResolver::State state;

    const auto follow = GimbalYawLockResolver::update(state, resolverInput(false, 70.0, 50.0));
    QVERIFY(anglesEqual(follow.azimuth.absoluteYawDegrees, 70.0));

    // The target bridge changes q from body yaw to earth yaw at lock entry but
    // leaves YAW_IN_VEHICLE_FRAME set. One packet is insufficient evidence.
    const auto lock = GimbalYawLockResolver::update(state, resolverInput(true, 90.0, 70.0));
    QCOMPARE(lock.selection, GimbalYawLockResolver::Selection::Undecided);
    QCOMPARE(lock.azimuth.source, GimbalAzimuthPolicy::Source::VehicleHeadingFallback);
    QVERIFY(anglesEqual(lock.azimuth.absoluteYawDegrees, 90.0));

    const auto baseTurn = GimbalYawLockResolver::update(state, resolverInput(true, 98.0, 70.5));
    QCOMPARE(baseTurn.selection, GimbalYawLockResolver::Selection::ReportedYaw);
    QCOMPARE(baseTurn.azimuth.source, GimbalAzimuthPolicy::Source::YawLockReportedYawCompatibility);
    QVERIFY(anglesEqual(baseTurn.azimuth.absoluteYawDegrees, 70.5));

    const auto yawCommand = GimbalYawLockResolver::update(state, resolverInput(true, 123.0, 95.0));
    QVERIFY(anglesEqual(yawCommand.azimuth.absoluteYawDegrees, 95.0));

    const auto unlock = GimbalYawLockResolver::update(state, resolverInput(false, 95.0, -15.0));
    QCOMPARE(unlock.selection, GimbalYawLockResolver::Selection::Undecided);
    QCOMPARE(unlock.azimuth.source, GimbalAzimuthPolicy::Source::VehicleHeadingFallback);
    QVERIFY(anglesEqual(unlock.azimuth.absoluteYawDegrees, 95.0));
}

void GimbalAzimuthPolicyTest::resolverRequiresMotionAfterFollowForStandard() {
    GimbalYawLockResolver::State state;

    const auto follow = GimbalYawLockResolver::update(state, resolverInput(false, 70.0, 50.0));
    QVERIFY(anglesEqual(follow.azimuth.absoluteYawDegrees, 70.0));
    const auto lock = GimbalYawLockResolver::update(state, resolverInput(true, 70.0, 50.0));
    QCOMPARE(lock.selection, GimbalYawLockResolver::Selection::Undecided);
    QCOMPARE(lock.azimuth.source, GimbalAzimuthPolicy::Source::VehicleHeadingFallback);
    QVERIFY(anglesEqual(lock.azimuth.absoluteYawDegrees, 70.0));

    // A compliant body-frame q counter-rotates as heading changes.
    const auto baseTurn = GimbalYawLockResolver::update(state, resolverInput(true, 70.5, 40.0));
    QCOMPARE(baseTurn.selection, GimbalYawLockResolver::Selection::Standard);
    QVERIFY(anglesEqual(baseTurn.azimuth.absoluteYawDegrees, 70.5));

    const auto yawCommand = GimbalYawLockResolver::update(state, resolverInput(true, 95.0, -15.0));
    QVERIFY(anglesEqual(yawCommand.azimuth.absoluteYawDegrees, 95.0));
}

void GimbalAzimuthPolicyTest::resolverLearnsReportedYawWhenStartingLocked() {
    GimbalYawLockResolver::State state;

    const auto first = GimbalYawLockResolver::update(state, resolverInput(true, 90.0, 70.0));
    QCOMPARE(first.selection, GimbalYawLockResolver::Selection::Undecided);
    QVERIFY(anglesEqual(first.azimuth.absoluteYawDegrees, 90.0));

    const auto learned = GimbalYawLockResolver::update(state, resolverInput(true, 98.0, 70.5));
    QCOMPARE(learned.selection, GimbalYawLockResolver::Selection::ReportedYaw);
    QCOMPARE(learned.azimuth.source, GimbalAzimuthPolicy::Source::YawLockReportedYawCompatibility);
    QVERIFY(anglesEqual(learned.azimuth.absoluteYawDegrees, 70.5));
}

void GimbalAzimuthPolicyTest::resolverToleratesLockedYawDrift() {
    GimbalYawLockResolver::State state;

    const auto first = GimbalYawLockResolver::update(state, resolverInput(true, 90.0, 70.0));
    QCOMPARE(first.selection, GimbalYawLockResolver::Selection::Undecided);

    // The physical lock may drift by more than a narrow noise threshold. It is
    // still the better interpretation when the protocol candidate follows a
    // much larger base rotation.
    const auto learned = GimbalYawLockResolver::update(state, resolverInput(true, 110.0, 72.0));
    QCOMPARE(learned.selection, GimbalYawLockResolver::Selection::ReportedYaw);
    QVERIFY(anglesEqual(learned.azimuth.absoluteYawDegrees, 72.0));
}

void GimbalAzimuthPolicyTest::resolverLearnsCompliantVehicleFrameWhenStartingLocked() {
    GimbalYawLockResolver::State state;

    const auto first = GimbalYawLockResolver::update(state, resolverInput(true, 70.0, 50.0));
    QCOMPARE(first.selection, GimbalYawLockResolver::Selection::Undecided);
    const auto learned = GimbalYawLockResolver::update(state, resolverInput(true, 70.5, 40.0));

    QCOMPARE(learned.selection, GimbalYawLockResolver::Selection::Standard);
    QCOMPARE(learned.azimuth.source, GimbalAzimuthPolicy::Source::VehicleHeadingFallback);
    QVERIFY(anglesEqual(learned.azimuth.absoluteYawDegrees, 70.5));
}

void GimbalAzimuthPolicyTest::resolverReanchorsDuringCameraMotion() {
    GimbalYawLockResolver::State state;

    const auto first = GimbalYawLockResolver::update(state, resolverInput(true, 90.0, 70.0));
    QCOMPARE(first.selection, GimbalYawLockResolver::Selection::Undecided);
    const auto cameraMotion = GimbalYawLockResolver::update(state, resolverInput(true, 100.0, 80.0));
    QCOMPARE(cameraMotion.selection, GimbalYawLockResolver::Selection::Undecided);

    // Once the command settles, base motion makes only the incorrect standard
    // candidate move and the resolver can safely identify the target bridge.
    const auto baseTurn = GimbalYawLockResolver::update(state, resolverInput(true, 108.0, 80.5));
    QCOMPARE(baseTurn.selection, GimbalYawLockResolver::Selection::ReportedYaw);
    QVERIFY(anglesEqual(baseTurn.azimuth.absoluteYawDegrees, 80.5));
}

void GimbalAzimuthPolicyTest::resolverHandlesLockFlagQuaternionSkew() {
    GimbalYawLockResolver::State state;

    const auto follow = GimbalYawLockResolver::update(state, resolverInput(false, 70.0, 50.0));
    QVERIFY(anglesEqual(follow.azimuth.absoluteYawDegrees, 70.0));

    // The flag arrives one sample before this target bridge changes q to its
    // earth-frame lock representation. Both candidates move together during
    // that update, so the observation anchor moves with them without training.
    const auto earlyLock = GimbalYawLockResolver::update(state, resolverInput(true, 70.0, 50.0));
    QCOMPARE(earlyLock.selection, GimbalYawLockResolver::Selection::Undecided);
    const auto frameChange = GimbalYawLockResolver::update(state, resolverInput(true, 90.0, 70.0));
    QCOMPARE(frameChange.selection, GimbalYawLockResolver::Selection::Undecided);

    const auto corrected = GimbalYawLockResolver::update(state, resolverInput(true, 98.0, 70.5));
    QCOMPARE(corrected.selection, GimbalYawLockResolver::Selection::ReportedYaw);
    QCOMPARE(corrected.azimuth.source, GimbalAzimuthPolicy::Source::YawLockReportedYawCompatibility);
    QVERIFY(anglesEqual(corrected.azimuth.absoluteYawDegrees, 70.5));
}

void GimbalAzimuthPolicyTest::resolverHandlesCoincidentYawAtLock() {
    GimbalYawLockResolver::State state;

    const auto follow = GimbalYawLockResolver::update(state, resolverInput(false, 70.0, 50.0));
    QVERIFY(anglesEqual(follow.azimuth.absoluteYawDegrees, 70.0));

    // A coincident yaw update can make direct q look continuous on the first
    // packet, but must not override the explicitly declared protocol frame.
    const auto earlyLock = GimbalYawLockResolver::update(state, resolverInput(true, 90.0, 70.0));
    QCOMPARE(earlyLock.selection, GimbalYawLockResolver::Selection::Undecided);
    const auto bodyFrameSettled = GimbalYawLockResolver::update(state, resolverInput(true, 70.0, 50.0));
    QCOMPARE(bodyFrameSettled.selection, GimbalYawLockResolver::Selection::Undecided);

    const auto corrected = GimbalYawLockResolver::update(state, resolverInput(true, 70.5, 40.0));
    QCOMPARE(corrected.selection, GimbalYawLockResolver::Selection::Standard);
    QCOMPARE(corrected.azimuth.source, GimbalAzimuthPolicy::Source::VehicleHeadingFallback);
    QVERIFY(anglesEqual(corrected.azimuth.absoluteYawDegrees, 70.5));
}

void GimbalAzimuthPolicyTest::resolverReevaluatesChangedProtocolSource() {
    GimbalYawLockResolver::State state;

    const auto follow = GimbalYawLockResolver::update(state, resolverInput(false, 70.0, 50.0));
    QVERIFY(anglesEqual(follow.azimuth.absoluteYawDegrees, 70.0));
    const auto lock = GimbalYawLockResolver::update(state, resolverInput(true, 70.0, 50.0));
    QCOMPARE(lock.selection, GimbalYawLockResolver::Selection::Undecided);
    const auto bodyMotion = GimbalYawLockResolver::update(state, resolverInput(true, 70.5, 40.0));
    QCOMPARE(bodyMotion.selection, GimbalYawLockResolver::Selection::Standard);

    // A newly supported delta_yaw is authoritative on its first sample. Later
    // motion evidence may still identify an inconsistent target bridge.
    const auto changedSource =
        GimbalYawLockResolver::update(state, resolverInput(true, 120.0, 70.0, GimbalAzimuthPolicy::Source::DeltaYaw));
    QCOMPARE(changedSource.selection, GimbalYawLockResolver::Selection::Standard);
    QCOMPARE(changedSource.azimuth.source, GimbalAzimuthPolicy::Source::DeltaYaw);
    QVERIFY(anglesEqual(changedSource.azimuth.absoluteYawDegrees, 120.0));

    const auto incompatibleDelta =
        GimbalYawLockResolver::update(state, resolverInput(true, 128.0, 70.5, GimbalAzimuthPolicy::Source::DeltaYaw));
    QCOMPARE(incompatibleDelta.selection, GimbalYawLockResolver::Selection::ReportedYaw);
    QCOMPARE(incompatibleDelta.azimuth.source, GimbalAzimuthPolicy::Source::YawLockReportedYawCompatibility);
    QVERIFY(anglesEqual(incompatibleDelta.azimuth.absoluteYawDegrees, 70.5));
}

void GimbalAzimuthPolicyTest::resolverReevaluatesReportedYawSelection() {
    GimbalYawLockResolver::State state;

    const auto first = GimbalYawLockResolver::update(state, resolverInput(true, 90.0, 70.0));
    QCOMPARE(first.selection, GimbalYawLockResolver::Selection::Undecided);
    const auto reported = GimbalYawLockResolver::update(state, resolverInput(true, 98.0, 70.5));
    QCOMPARE(reported.selection, GimbalYawLockResolver::Selection::ReportedYaw);

    // Keep evaluating both candidates after learning. If sender behavior
    // becomes protocol-compliant, the stable standard result wins again.
    const auto corrected = GimbalYawLockResolver::update(state, resolverInput(true, 98.5, 60.0));
    QCOMPARE(corrected.selection, GimbalYawLockResolver::Selection::Standard);
    QCOMPARE(corrected.azimuth.source, GimbalAzimuthPolicy::Source::VehicleHeadingFallback);
    QVERIFY(anglesEqual(corrected.azimuth.absoluteYawDegrees, 98.5));
}

void GimbalAzimuthPolicyTest::resolverReevaluatesStandardSelection() {
    GimbalYawLockResolver::State state;

    const auto first = GimbalYawLockResolver::update(state, resolverInput(true, 70.0, 50.0));
    QCOMPARE(first.selection, GimbalYawLockResolver::Selection::Undecided);
    const auto standard = GimbalYawLockResolver::update(state, resolverInput(true, 70.5, 40.0));
    QCOMPARE(standard.selection, GimbalYawLockResolver::Selection::Standard);

    // The reverse transition is also possible without changing the protocol
    // source enum: continued motion evidence must correct the old choice.
    const auto corrected = GimbalYawLockResolver::update(state, resolverInput(true, 80.5, 40.5));
    QCOMPARE(corrected.selection, GimbalYawLockResolver::Selection::ReportedYaw);
    QCOMPARE(corrected.azimuth.source, GimbalAzimuthPolicy::Source::YawLockReportedYawCompatibility);
    QVERIFY(anglesEqual(corrected.azimuth.absoluteYawDegrees, 40.5));
}

void GimbalAzimuthPolicyTest::resolverRetainsSelectionAcrossInvalidSample() {
    GimbalYawLockResolver::State state;

    const auto first = GimbalYawLockResolver::update(state, resolverInput(true, 90.0, 70.0));
    QCOMPARE(first.selection, GimbalYawLockResolver::Selection::Undecided);
    const auto learned = GimbalYawLockResolver::update(state, resolverInput(true, 98.0, 70.5));
    QCOMPARE(learned.selection, GimbalYawLockResolver::Selection::ReportedYaw);

    auto invalidInput = resolverInput(true, 0.0, 0.0);
    invalidInput.standardResult.valid = false;
    invalidInput.reportedYawResult.valid = false;
    const auto invalid = GimbalYawLockResolver::update(state, invalidInput);
    QVERIFY(!invalid.azimuth.valid);
    QCOMPARE(invalid.selection, GimbalYawLockResolver::Selection::ReportedYaw);
    QVERIFY(!invalid.selectionChanged);

    const auto recovered = GimbalYawLockResolver::update(state, resolverInput(true, 98.5, 71.0));
    QCOMPARE(recovered.selection, GimbalYawLockResolver::Selection::ReportedYaw);
    QCOMPARE(recovered.azimuth.source, GimbalAzimuthPolicy::Source::YawLockReportedYawCompatibility);
    QVERIFY(anglesEqual(recovered.azimuth.absoluteYawDegrees, 71.0));
}

void GimbalAzimuthPolicyTest::resolverUsesVehicleHeadingWhenDeltaFreezes() {
    GimbalYawLockResolver::State state;

    const auto first = GimbalYawLockResolver::update(state, resolverInputWithHeading(70.0, 50.0, 70.0));
    QCOMPARE(first.selection, GimbalYawLockResolver::Selection::Undecided);

    // World yaw remains 70 degrees. A frozen 20-degree delta makes both the
    // protocol result and raw body yaw move with the base; q + heading does not.
    const auto learned = GimbalYawLockResolver::update(state, resolverInputWithHeading(50.0, 30.0, 70.0));
    QCOMPARE(learned.selection, GimbalYawLockResolver::Selection::VehicleHeading);
    QCOMPARE(learned.azimuth.source, GimbalAzimuthPolicy::Source::YawLockVehicleHeadingCompatibility);
    QVERIFY(anglesEqual(learned.azimuth.absoluteYawDegrees, 70.0));
}

void GimbalAzimuthPolicyTest::resolverUsesVehicleHeadingWhenSupportedDeltaStaysZero() {
    GimbalYawLockResolver::State state;

    const auto first = GimbalYawLockResolver::update(state, resolverInputWithHeading(50.0, 50.0, 70.0));
    QCOMPARE(first.selection, GimbalYawLockResolver::Selection::Undecided);

    // This represents delta support inferred from delta_yaw_velocity while
    // the finite delta_yaw field itself remains an incorrect default zero.
    const auto learned = GimbalYawLockResolver::update(state, resolverInputWithHeading(30.0, 30.0, 70.0));
    QCOMPARE(learned.selection, GimbalYawLockResolver::Selection::VehicleHeading);
    QCOMPARE(learned.azimuth.source, GimbalAzimuthPolicy::Source::YawLockVehicleHeadingCompatibility);
    QVERIFY(anglesEqual(learned.azimuth.absoluteYawDegrees, 70.0));
}

void GimbalAzimuthPolicyTest::resolverKeepsDeltaWhenHeadingAlsoStable() {
    GimbalYawLockResolver::State state;

    const auto first = GimbalYawLockResolver::update(state, resolverInputWithHeading(70.0, 50.0, 70.0));
    QCOMPARE(first.selection, GimbalYawLockResolver::Selection::Undecided);
    const auto learned = GimbalYawLockResolver::update(state, resolverInputWithHeading(70.5, 30.0, 70.5));

    // Standard and heading are equally stable on a compliant sender. DeltaYaw
    // remains the protocol-safe result instead of being replaced on a tie.
    QCOMPARE(learned.selection, GimbalYawLockResolver::Selection::Standard);
    QCOMPARE(learned.azimuth.source, GimbalAzimuthPolicy::Source::DeltaYaw);
    QVERIFY(anglesEqual(learned.azimuth.absoluteYawDegrees, 70.5));
}

void GimbalAzimuthPolicyTest::resolverKeepsReportedYawWithHeadingCandidate() {
    GimbalYawLockResolver::State state;

    const auto first = GimbalYawLockResolver::update(state, resolverInputWithHeading(90.0, 70.0, 90.0));
    QCOMPARE(first.selection, GimbalYawLockResolver::Selection::Undecided);
    const auto learned = GimbalYawLockResolver::update(state, resolverInputWithHeading(110.0, 70.5, 110.0));

    QCOMPARE(learned.selection, GimbalYawLockResolver::Selection::ReportedYaw);
    QCOMPARE(learned.azimuth.source, GimbalAzimuthPolicy::Source::YawLockReportedYawCompatibility);
    QVERIFY(anglesEqual(learned.azimuth.absoluteYawDegrees, 70.5));
}

void GimbalAzimuthPolicyTest::resolverReevaluatesVehicleHeadingSelection() {
    GimbalYawLockResolver::State state;

    const auto first = GimbalYawLockResolver::update(state, resolverInputWithHeading(70.0, 50.0, 70.0));
    QCOMPARE(first.selection, GimbalYawLockResolver::Selection::Undecided);
    const auto heading = GimbalYawLockResolver::update(state, resolverInputWithHeading(50.0, 30.0, 70.0));
    QCOMPARE(heading.selection, GimbalYawLockResolver::Selection::VehicleHeading);

    // Reconfirm the same selection across another complete motion window. Its
    // anchors must advance so later sender behaviour is judged from fresh data.
    const auto confirmed = GimbalYawLockResolver::update(state, resolverInputWithHeading(30.0, 10.0, 70.5));
    QCOMPARE(confirmed.selection, GimbalYawLockResolver::Selection::VehicleHeading);
    QCOMPARE(confirmed.azimuth.source, GimbalAzimuthPolicy::Source::YawLockVehicleHeadingCompatibility);

    // If the sender later changes q to an earth-frame relationship, raw q is
    // now the only stable candidate and must replace the learned heading path.
    const auto corrected = GimbalYawLockResolver::update(state, resolverInputWithHeading(50.0, 10.5, 90.5));
    QCOMPARE(corrected.selection, GimbalYawLockResolver::Selection::ReportedYaw);
    QCOMPARE(corrected.azimuth.source, GimbalAzimuthPolicy::Source::YawLockReportedYawCompatibility);
    QVERIFY(anglesEqual(corrected.azimuth.absoluteYawDegrees, 10.5));
}

void GimbalAzimuthPolicyTest::resolverDropsVehicleHeadingWhenReferenceExpires() {
    GimbalYawLockResolver::State state;

    const auto first = GimbalYawLockResolver::update(state, resolverInputWithHeading(70.0, 50.0, 70.0));
    QCOMPARE(first.selection, GimbalYawLockResolver::Selection::Undecided);
    const auto heading = GimbalYawLockResolver::update(state, resolverInputWithHeading(50.0, 30.0, 70.0));
    QCOMPARE(heading.selection, GimbalYawLockResolver::Selection::VehicleHeading);

    const auto expired =
        GimbalYawLockResolver::update(state, resolverInput(true, 50.0, 30.0, GimbalAzimuthPolicy::Source::DeltaYaw));
    QCOMPARE(expired.selection, GimbalYawLockResolver::Selection::Standard);
    QVERIFY(expired.selectionChanged);
    QCOMPARE(expired.azimuth.source, GimbalAzimuthPolicy::Source::DeltaYaw);
    QVERIFY(anglesEqual(expired.azimuth.absoluteYawDegrees, 50.0));
}

void GimbalAzimuthPolicyTest::resolverUsesWrappedVehicleHeadingEvidence() {
    GimbalYawLockResolver::State state;

    const auto first = GimbalYawLockResolver::update(state, resolverInputWithHeading(170.0, 160.0, 179.0));
    QCOMPARE(first.selection, GimbalYawLockResolver::Selection::Undecided);
    const auto wrapped = GimbalYawLockResolver::update(state, resolverInputWithHeading(-170.0, -180.0, -180.0));

    QCOMPARE(wrapped.selection, GimbalYawLockResolver::Selection::VehicleHeading);
    QCOMPARE(wrapped.azimuth.source, GimbalAzimuthPolicy::Source::YawLockVehicleHeadingCompatibility);
    QVERIFY(anglesEqual(wrapped.azimuth.absoluteYawDegrees, -180.0));
}

void GimbalAzimuthPolicyTest::resolverUsesWrappedMotionEvidence() {
    GimbalYawLockResolver::State state;

    const auto first = GimbalYawLockResolver::update(state, resolverInput(true, 170.0, 179.0));
    QCOMPARE(first.selection, GimbalYawLockResolver::Selection::Undecided);
    const auto wrapped = GimbalYawLockResolver::update(state, resolverInput(true, -180.0, -180.0));

    QCOMPARE(wrapped.selection, GimbalYawLockResolver::Selection::ReportedYaw);
    QVERIFY(anglesEqual(wrapped.azimuth.absoluteYawDegrees, -180.0));
}

void GimbalAzimuthPolicyTest::resolverResetDoesNotReuseCompatibilitySelection() {
    GimbalYawLockResolver::State state;

    const auto first = GimbalYawLockResolver::update(state, resolverInput(true, 90.0, 70.0));
    QCOMPARE(first.selection, GimbalYawLockResolver::Selection::Undecided);
    const auto learned = GimbalYawLockResolver::update(state, resolverInput(true, 98.0, 70.5));
    QCOMPARE(learned.selection, GimbalYawLockResolver::Selection::ReportedYaw);

    GimbalYawLockResolver::reset(state);
    const auto afterReset = GimbalYawLockResolver::update(state, resolverInput(true, 90.0, 70.0));
    QCOMPARE(afterReset.selection, GimbalYawLockResolver::Selection::Undecided);
    QCOMPARE(afterReset.azimuth.source, GimbalAzimuthPolicy::Source::VehicleHeadingFallback);
    QVERIFY(anglesEqual(afterReset.azimuth.absoluteYawDegrees, 90.0));
}

void GimbalAzimuthPolicyTest::resolverDetectsSenderRestartAtZeroBootTime() {
    QVERIFY(GimbalYawLockResolver::senderRestarted(10000U, 0U));
    QVERIFY(GimbalYawLockResolver::senderRestarted(10000U, 100U));
    QVERIFY(!GimbalYawLockResolver::senderRestarted(10000U, 9000U));
    QVERIFY(!GimbalYawLockResolver::senderRestarted(10000U, 9500U));
    QVERIFY(!GimbalYawLockResolver::senderRestarted(0U, 0U));
    QVERIFY(!GimbalYawLockResolver::senderRestarted(0U, 100U));
    QVERIFY(!GimbalYawLockResolver::senderRestarted(10000U, 11000U));
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
