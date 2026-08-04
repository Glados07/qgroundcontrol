/****************************************************************************
 *
 * Local media session state-transition policy.
 *
 ****************************************************************************/

#include "GimbalMediaSessionPolicy.h"

GimbalMediaSessionPolicy::LocalAction
GimbalMediaSessionPolicy::localAction(const LocalState& state)
{
    if (!state.intent || !state.settingEnabled) {
        // A session adopted from outside this manager must never be stopped.
        // A provisional start is also not safe to stop: wait for its matching
        // receiver result to confirm ownership, then compensate if necessary.
        if (state.owned
            || (state.stopPending
                && !state.usingExternal)) {
            return StopOwned;
        }
        if (state.usingExternal || state.active) {
            return ReleaseExternal;
        }
        return None;
    }

    if (state.startPending || state.stopPending) {
        return None;
    }

    if (state.actualRecording) {
        if (!state.owned) {
            return AdoptExternal;
        }
        return state.active ? None : ConfirmOwned;
    }

    if (state.streaming && !state.startBlocked) {
        return StartOwned;
    }

    return None;
}

bool GimbalMediaSessionPolicy::recordingSessionCapturing(
    bool cameraRecording,
    bool cameraPending,
    bool localActive)
{
    return localActive || (cameraRecording && !cameraPending);
}

bool GimbalMediaSessionPolicy::recordingAvailable(
    bool sessionActive,
    bool cameraPending,
    bool localPending,
    bool localEnabled,
    bool streaming,
    bool cameraEnabled,
    bool cameraStatusKnown)
{
    if (cameraPending || localPending) {
        return false;
    }
    if (sessionActive) {
        return true;
    }

    const bool localAvailable = localEnabled && streaming;
    const bool cameraAvailable = cameraEnabled && cameraStatusKnown;
    return localAvailable || cameraAvailable;
}
