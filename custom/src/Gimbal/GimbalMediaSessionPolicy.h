/****************************************************************************
 *
 * Local media session state-transition policy.
 *
 ****************************************************************************/

#pragma once

class GimbalMediaSessionPolicy
{
public:
    struct LocalState {
        bool intent = false;
        bool settingEnabled = false;
        bool streaming = false;
        bool actualRecording = false;
        bool active = false;
        // True only after the recorder start result has matched the requested
        // output. A provisional start is represented by startPending instead.
        bool owned = false;
        bool usingExternal = false;
        bool startPending = false;
        bool stopPending = false;
        bool startBlocked = false;
    };

    enum LocalAction {
        None,
        StartOwned,
        StopOwned,
        AdoptExternal,
        ConfirmOwned,
        ReleaseExternal,
    };

    [[nodiscard]] static LocalAction localAction(const LocalState& state);
    [[nodiscard]] static bool recordingSessionCapturing(bool cameraRecording,
                                                        bool cameraPending,
                                                        bool localActive);
    [[nodiscard]] static bool recordingAvailable(bool sessionActive,
                                                 bool cameraPending,
                                                 bool localPending,
                                                 bool localEnabled,
                                                 bool streaming,
                                                 bool cameraEnabled,
                                                 bool cameraStatusKnown);
};
