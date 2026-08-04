/****************************************************************************
 *
 * Local media session state-transition policy regression tests.
 *
 ****************************************************************************/

#include "GimbalMediaSessionPolicy.h"

#include <QtTest/QTest>

class GimbalMediaSessionPolicyTest : public QObject
{
    Q_OBJECT

private slots:
    void localStartDoesNotDependOnSdState();
    void externalRecordingIsReleasedWithoutStopping();
    void ownedRecordingStopsWhenDisabled();
    void cancelledProvisionalStartWaitsForOwnershipResult();
    void lateConfirmedStartRequiresCompensatingStop();
    void pendingOperationsAreIdempotent();
    void expectedStopSuppressesRestartWhilePending();
    void provisionalOwnershipRemainsPending();
    void streamAndBlockGateOwnedStart();
    void actualRecordingIsAdoptedOrConfirmed();
    void confirmedOwnershipControlsStopVsRelease();
    void externalRecorderAdoptionIsIdempotent();
    void capturingExcludesOptimisticCameraState();
    void availabilitySupportsLocalOnlyRecording();
};

void GimbalMediaSessionPolicyTest::localStartDoesNotDependOnSdState()
{
    // LocalState deliberately contains no camera or SD-card status. A valid
    // local stream starts even when the camera branch is unavailable.
    GimbalMediaSessionPolicy::LocalState state;
    state.intent = true;
    state.settingEnabled = true;
    state.streaming = true;

    QCOMPARE(GimbalMediaSessionPolicy::localAction(state),
             GimbalMediaSessionPolicy::StartOwned);
}

void GimbalMediaSessionPolicyTest::externalRecordingIsReleasedWithoutStopping()
{
    GimbalMediaSessionPolicy::LocalState state;
    state.settingEnabled = true;
    state.actualRecording = true;
    state.active = true;
    state.usingExternal = true;

    QCOMPARE(GimbalMediaSessionPolicy::localAction(state),
             GimbalMediaSessionPolicy::ReleaseExternal);

    state.settingEnabled = false;
    state.stopPending = true;
    QCOMPARE(GimbalMediaSessionPolicy::localAction(state),
             GimbalMediaSessionPolicy::ReleaseExternal);

    // active also identifies a non-owned session even before the manager has
    // marked it explicitly as external.
    state.usingExternal = false;
    state.startPending = false;
    state.stopPending = false;
    QCOMPARE(GimbalMediaSessionPolicy::localAction(state),
             GimbalMediaSessionPolicy::ReleaseExternal);
}

void GimbalMediaSessionPolicyTest::ownedRecordingStopsWhenDisabled()
{
    GimbalMediaSessionPolicy::LocalState state;
    state.intent = true;
    state.active = true;
    state.owned = true;
    state.actualRecording = true;

    QCOMPARE(GimbalMediaSessionPolicy::localAction(state),
             GimbalMediaSessionPolicy::StopOwned);
}

void GimbalMediaSessionPolicyTest::cancelledProvisionalStartWaitsForOwnershipResult()
{
    GimbalMediaSessionPolicy::LocalState state;
    state.settingEnabled = true;
    state.streaming = true;
    state.startPending = true;

    // Manager deliberately passes owned=false until the receiver result
    // matches the unique requested basename. Cancellation must not stop a
    // concurrent recorder which may have won the receiver in the meantime.
    QCOMPARE(GimbalMediaSessionPolicy::localAction(state),
             GimbalMediaSessionPolicy::None);
}

void GimbalMediaSessionPolicyTest::lateConfirmedStartRequiresCompensatingStop()
{
    GimbalMediaSessionPolicy::LocalState state;
    state.settingEnabled = true;
    state.streaming = true;
    state.startPending = true;
    state.owned = true;

    // A late matching success turns provisional ownership into confirmed
    // ownership. Even before actualRecording arrives, cancelled intent must
    // select the owned stop path and retain ownership until finalization.
    QCOMPARE(GimbalMediaSessionPolicy::localAction(state),
             GimbalMediaSessionPolicy::StopOwned);

    state.startPending = false;
    state.stopPending = true;
    QCOMPARE(GimbalMediaSessionPolicy::localAction(state),
             GimbalMediaSessionPolicy::StopOwned);
}

void GimbalMediaSessionPolicyTest::pendingOperationsAreIdempotent()
{
    GimbalMediaSessionPolicy::LocalState state;
    state.intent = true;
    state.settingEnabled = true;
    state.streaming = true;
    state.startPending = true;

    QCOMPARE(GimbalMediaSessionPolicy::localAction(state),
             GimbalMediaSessionPolicy::None);

    state.startPending = false;
    state.stopPending = true;
    state.actualRecording = true;
    state.owned = true;
    QCOMPARE(GimbalMediaSessionPolicy::localAction(state),
             GimbalMediaSessionPolicy::None);
}

void GimbalMediaSessionPolicyTest::expectedStopSuppressesRestartWhilePending()
{
    GimbalMediaSessionPolicy::LocalState state;
    state.intent = true;
    state.settingEnabled = true;
    state.streaming = true;
    state.owned = true;
    state.stopPending = true;

    // actualRecording=false is the completion edge. While Manager still marks
    // it as an expected stop, the policy must not produce StartOwned.
    QCOMPARE(GimbalMediaSessionPolicy::localAction(state),
             GimbalMediaSessionPolicy::None);
}

void GimbalMediaSessionPolicyTest::provisionalOwnershipRemainsPending()
{
    // This mirrors Manager immediately after the main receiver start call:
    // raw ownership is provisional, so the policy receives owned=false.
    GimbalMediaSessionPolicy::LocalState state;
    state.intent = true;
    state.settingEnabled = true;
    state.streaming = true;
    state.startPending = true;

    QCOMPARE(GimbalMediaSessionPolicy::localAction(state),
             GimbalMediaSessionPolicy::None);

    // A matching receiver result can confirm ownership before the global
    // recordingChanged signal. Pending still suppresses a duplicate action.
    state.owned = true;
    QCOMPARE(GimbalMediaSessionPolicy::localAction(state),
             GimbalMediaSessionPolicy::None);

    // Conversely, a concurrent external recording may become visible before
    // our start result. It is not adopted until the pending result is settled.
    state.owned = false;
    state.actualRecording = true;
    QCOMPARE(GimbalMediaSessionPolicy::localAction(state),
             GimbalMediaSessionPolicy::None);
}

void GimbalMediaSessionPolicyTest::streamAndBlockGateOwnedStart()
{
    GimbalMediaSessionPolicy::LocalState state;
    state.intent = true;
    state.settingEnabled = true;

    QCOMPARE(GimbalMediaSessionPolicy::localAction(state),
             GimbalMediaSessionPolicy::None);

    state.streaming = true;
    state.startBlocked = true;
    QCOMPARE(GimbalMediaSessionPolicy::localAction(state),
             GimbalMediaSessionPolicy::None);

    state.startBlocked = false;
    QCOMPARE(GimbalMediaSessionPolicy::localAction(state),
             GimbalMediaSessionPolicy::StartOwned);
}

void GimbalMediaSessionPolicyTest::actualRecordingIsAdoptedOrConfirmed()
{
    GimbalMediaSessionPolicy::LocalState state;
    state.intent = true;
    state.settingEnabled = true;
    state.actualRecording = true;

    QCOMPARE(GimbalMediaSessionPolicy::localAction(state),
             GimbalMediaSessionPolicy::AdoptExternal);

    state.owned = true;
    QCOMPARE(GimbalMediaSessionPolicy::localAction(state),
             GimbalMediaSessionPolicy::ConfirmOwned);

    state.active = true;
    QCOMPARE(GimbalMediaSessionPolicy::localAction(state),
             GimbalMediaSessionPolicy::None);
}

void GimbalMediaSessionPolicyTest::confirmedOwnershipControlsStopVsRelease()
{
    GimbalMediaSessionPolicy::LocalState state;
    state.settingEnabled = true;
    state.actualRecording = true;
    state.active = true;
    state.owned = true;

    // User/session intent is gone: only a confirmed-owned recorder is stopped.
    QCOMPARE(GimbalMediaSessionPolicy::localAction(state),
             GimbalMediaSessionPolicy::StopOwned);

    state.owned = false;
    state.usingExternal = true;
    QCOMPARE(GimbalMediaSessionPolicy::localAction(state),
             GimbalMediaSessionPolicy::ReleaseExternal);
}

void GimbalMediaSessionPolicyTest::externalRecorderAdoptionIsIdempotent()
{
    GimbalMediaSessionPolicy::LocalState state;
    state.intent = true;
    state.settingEnabled = true;
    state.streaming = true;
    state.actualRecording = true;

    QCOMPARE(GimbalMediaSessionPolicy::localAction(state),
             GimbalMediaSessionPolicy::AdoptExternal);

    state.active = true;
    state.usingExternal = true;
    QCOMPARE(GimbalMediaSessionPolicy::localAction(state),
             GimbalMediaSessionPolicy::AdoptExternal);

    // A global recorder seen without a control-bar session must remain
    // unobserved; merely seeing actualRecording is not permission to adopt it.
    state.intent = false;
    state.active = false;
    state.usingExternal = false;
    QCOMPARE(GimbalMediaSessionPolicy::localAction(state),
             GimbalMediaSessionPolicy::None);
}

void GimbalMediaSessionPolicyTest::capturingExcludesOptimisticCameraState()
{
    QVERIFY(GimbalMediaSessionPolicy::recordingSessionCapturing(
        true, false, false));
    QVERIFY(!GimbalMediaSessionPolicy::recordingSessionCapturing(
        true, true, false));
    QVERIFY(GimbalMediaSessionPolicy::recordingSessionCapturing(
        true, true, true));
    QVERIFY(GimbalMediaSessionPolicy::recordingSessionCapturing(
        false, false, true));
    QVERIFY(!GimbalMediaSessionPolicy::recordingSessionCapturing(
        false, false, false));
}

void GimbalMediaSessionPolicyTest::availabilitySupportsLocalOnlyRecording()
{
    // SDK disabled/status unknown, but a local stream is sufficient.
    QVERIFY(GimbalMediaSessionPolicy::recordingAvailable(
        false, false, false, true, true, false, false));

    QVERIFY(!GimbalMediaSessionPolicy::recordingAvailable(
        false, true, false, true, true, false, false));
    QVERIFY(!GimbalMediaSessionPolicy::recordingAvailable(
        false, false, true, true, true, false, false));

    // An active session remains stoppable even when both backends are now
    // unavailable, provided no transition is already pending.
    QVERIFY(GimbalMediaSessionPolicy::recordingAvailable(
        true, false, false, false, false, false, false));

    QVERIFY(GimbalMediaSessionPolicy::recordingAvailable(
        false, false, false, false, false, true, true));
    QVERIFY(!GimbalMediaSessionPolicy::recordingAvailable(
        false, false, false, true, false, true, false));
}

QTEST_APPLESS_MAIN(GimbalMediaSessionPolicyTest)

#include "GimbalMediaSessionPolicyTest.moc"
