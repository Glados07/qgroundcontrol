/****************************************************************************
 *
 * UniPod MT11 camera-control and local-media coordinator.
 *
 ****************************************************************************/

#pragma once

#include <QtCore/QElapsedTimer>
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QSize>
#include <QtCore/QStringList>
#include <QtCore/QThreadPool>
#include <QtCore/QTimer>

class Fact;
class GimbalControlSettings;
class Mt11Sdk;
class QQuickItem;
class VideoReceiver;

class Mt11ControlManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled NOTIFY enabledChanged)
    Q_PROPERTY(bool sdkResponding READ sdkResponding NOTIFY sdkRespondingChanged)
    Q_PROPERTY(double currentZoom READ currentZoom NOTIFY currentZoomChanged)
    Q_PROPERTY(double actualZoom READ actualZoom NOTIFY actualZoomChanged)
    Q_PROPERTY(bool actualZoomKnown READ actualZoomKnown NOTIFY actualZoomKnownChanged)
    Q_PROPERTY(double zoomStep READ zoomStep NOTIFY zoomStepChanged)
    Q_PROPERTY(double minimumZoom READ minimumZoom CONSTANT)
    Q_PROPERTY(double maximumZoom READ maximumZoom NOTIFY maximumZoomChanged)
    Q_PROPERTY(bool zoomStatusKnown READ zoomStatusKnown NOTIFY zoomStatusKnownChanged)
    Q_PROPERTY(bool zoomInAvailable READ zoomInAvailable NOTIFY zoomAvailabilityChanged)
    Q_PROPERTY(bool zoomOutAvailable READ zoomOutAvailable NOTIFY zoomAvailabilityChanged)
    Q_PROPERTY(bool zoomInTapAvailable READ zoomInTapAvailable NOTIFY zoomAvailabilityChanged)
    Q_PROPERTY(bool zoomOutTapAvailable READ zoomOutTapAvailable NOTIFY zoomAvailabilityChanged)
    Q_PROPERTY(bool zoomInHoldAvailable READ zoomInHoldAvailable NOTIFY zoomAvailabilityChanged)
    Q_PROPERTY(bool zoomOutHoldAvailable READ zoomOutHoldAvailable NOTIFY zoomAvailabilityChanged)
    Q_PROPERTY(bool zoomControlsUnlocked READ zoomControlsUnlocked NOTIFY zoomAvailabilityChanged)
    Q_PROPERTY(bool zoomCommandPending READ zoomCommandPending NOTIFY zoomAvailabilityChanged)
    Q_PROPERTY(bool continuousZoomActive READ continuousZoomActive NOTIFY continuousZoomActiveChanged)
    Q_PROPERTY(bool cameraStatusKnown READ cameraStatusKnown NOTIFY cameraStatusKnownChanged)
    Q_PROPERTY(bool recording READ recording NOTIFY recordingChanged)
    Q_PROPERTY(bool recordingCommandPending READ recordingCommandPending NOTIFY recordingCommandPendingChanged)
    Q_PROPERTY(bool cameraRecordingPending READ cameraRecordingPending NOTIFY recordingSessionStateChanged)
    Q_PROPERTY(bool photoCommandPending READ photoCommandPending NOTIFY photoCommandPendingChanged)
    Q_PROPERTY(bool localMediaStorageEnabled READ localMediaStorageEnabled NOTIFY localMediaStorageEnabledChanged)
    Q_PROPERTY(bool localRecording READ localRecording NOTIFY localRecordingStateChanged)
    Q_PROPERTY(bool localRecordingPending READ localRecordingPending NOTIFY localRecordingStateChanged)
    Q_PROPERTY(bool recordingSessionActive READ recordingSessionActive NOTIFY recordingSessionStateChanged)
    Q_PROPERTY(bool recordingSessionCapturing READ recordingSessionCapturing NOTIFY recordingSessionStateChanged)
    Q_PROPERTY(bool videoRecordingAvailable READ videoRecordingAvailable NOTIFY recordingSessionStateChanged)
    Q_PROPERTY(int photoCount READ photoCount NOTIFY photoCountChanged)
    Q_PROPERTY(int localPhotoCount READ localPhotoCount NOTIFY localPhotoCountChanged)
    Q_PROPERTY(bool videoModeKnown READ videoModeKnown NOTIFY videoModeKnownChanged)
    Q_PROPERTY(int videoMode READ videoMode NOTIFY videoModeChanged)
    Q_PROPERTY(bool videoModePending READ videoModePending NOTIFY videoModePendingChanged)
    // Compatibility properties retained for the former binary mode control.
    Q_PROPERTY(bool thermalModeKnown READ thermalModeKnown NOTIFY thermalModeKnownChanged)
    Q_PROPERTY(bool thermalModeEnabled READ thermalModeEnabled NOTIFY thermalModeEnabledChanged)
    Q_PROPERTY(bool thermalCommandPending READ thermalCommandPending NOTIFY thermalCommandPendingChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(QString localMediaError READ localMediaError NOTIFY localMediaErrorChanged)

public:
    enum VideoMode {
        VideoModeZoom = 0,
        VideoModeThermal = 2,
        VideoModeZoomAndThermal = 3,
    };
    Q_ENUM(VideoMode)

    explicit Mt11ControlManager(GimbalControlSettings* settings,
                                QObject* parent = nullptr);
    ~Mt11ControlManager() override;

    bool enabled() const;
    bool sdkResponding() const { return _sdkResponding; }
    double currentZoom() const { return _currentZoom; }
    double actualZoom() const { return _measuredZoom; }
    bool actualZoomKnown() const { return _measuredZoomKnown; }
    double zoomStep() const;
    double minimumZoom() const { return kMinimumZoom; }
    double maximumZoom() const { return _maximumZoom; }
    bool zoomStatusKnown() const { return _zoomStatusKnown; }
    bool zoomInAvailable() const;
    bool zoomOutAvailable() const;
    bool zoomInTapAvailable() const;
    bool zoomOutTapAvailable() const;
    bool zoomInHoldAvailable() const;
    bool zoomOutHoldAvailable() const;
    bool zoomControlsUnlocked() const {
        return enabled() && _sdkResponding && _maximumZoomKnown
            && _measuredZoomKnown && _zoomStatusKnown
            && !_videoModePending;
    }
    bool zoomCommandPending() const { return _zoomCommandPending; }
    bool continuousZoomActive() const { return _continuousZoomActive; }
    bool cameraStatusKnown() const { return _cameraStatusKnown; }
    bool recording() const { return _recording; }
    bool recordingCommandPending() const { return _recordingCommandPending; }
    bool cameraRecordingPending() const { return _recordingCommandPending; }
    bool photoCommandPending() const { return _photoCommandPending; }
    bool localMediaStorageEnabled() const;
    bool localRecording() const { return _localRecordingActive; }
    bool localRecordingPending() const {
        return _localRecordingStartPending || _localRecordingStopPending;
    }
    bool recordingSessionActive() const;
    bool recordingSessionCapturing() const;
    bool videoRecordingAvailable() const;
    int photoCount() const { return _photoCount; }
    int localPhotoCount() const { return _localPhotoCount; }
    bool videoModeKnown() const { return _videoModeKnown; }
    int videoMode() const { return _videoMode; }
    bool videoModePending() const { return _videoModePending; }
    bool thermalModeKnown() const { return videoModeKnown(); }
    bool thermalModeEnabled() const {
        return _videoMode == VideoModeThermal;
    }
    bool thermalCommandPending() const { return videoModePending(); }
    QString lastError() const { return _lastError; }
    QString localMediaError() const { return _localMediaError; }

    Q_INVOKABLE bool zoomIn();
    Q_INVOKABLE bool zoomOut();
    Q_INVOKABLE bool setZoom(double zoomLevel);
    Q_INVOKABLE bool startZoom(int direction);
    Q_INVOKABLE bool startZoomWithPressDuration(int direction,
                                                int pressDurationMs);
    Q_INVOKABLE bool stopZoom();
    Q_INVOKABLE bool cancelZoom();
    Q_INVOKABLE bool takePhoto();
    Q_INVOKABLE bool toggleVideoRecording();
    Q_INVOKABLE bool requestCurrentZoom();
    Q_INVOKABLE bool requestCameraStatus();
    Q_INVOKABLE bool setVideoMode(int mode);
    // Compatibility action retained for the former binary mode control.
    Q_INVOKABLE bool toggleThermalMode();

    void setVideoItem(QQuickItem* videoItem);
    void setVideoReceiver(VideoReceiver* receiver);
    /// Accepts a decoded Video 2 frame size only when it belongs to the
    /// currently attached receiver. It is used instead of the current PIP.
    void setNegotiatedPulledVideoResolution(VideoReceiver* sourceReceiver,
                                            const QSize& videoSize);
    void handleVideoRecordingStartResult(bool success,
                                         const QString& outputFile);
    void shutdownLocalMedia(bool waitForStop = false);
    void finalizeDetachedLocalMedia();

signals:
    void enabledChanged();
    void sdkRespondingChanged();
    void currentZoomChanged();
    void actualZoomChanged();
    void actualZoomKnownChanged();
    void zoomStepChanged();
    void maximumZoomChanged();
    void zoomStatusKnownChanged();
    void zoomAvailabilityChanged();
    void continuousZoomActiveChanged();
    void cameraStatusKnownChanged();
    void recordingChanged();
    void recordingCommandPendingChanged();
    void photoCommandPendingChanged();
    void localMediaStorageEnabledChanged();
    void localRecordingStateChanged();
    void recordingSessionStateChanged();
    void photoCountChanged();
    void localPhotoCountChanged();
    void videoModeKnownChanged();
    void videoModeChanged();
    void videoModePendingChanged();
    void thermalModeKnownChanged();
    void thermalModeEnabledChanged();
    void thermalCommandPendingChanged();
    void lastErrorChanged();
    void localMediaErrorChanged();

private slots:
    void _settingsChanged();
    void _zoomStepChanged();
    void _pollSdk();
    void _markSdkNotResponding();
    void _handleManualZoom(double zoomLevel);
    void _handleAbsoluteZoomFeedback(bool accepted);
    void _handleMaximumZoom(double maximumZoom);
    void _handleRecordingStreamParameters(quint8 videoEncodingType,
                                          quint16 width,
                                          quint16 height,
                                          quint16 bitrateKbps,
                                          quint8 frameRate);
    void _expireRecordingResolutionCapability();
    void _handleCurrentZoom(double zoomLevel);
    void _handleCameraSystemStatus(quint8 hdrStatus,
                                   quint8 recordingStatus,
                                   quint8 gimbalMotionMode,
                                   quint8 gimbalMountingDirection,
                                   quint8 videoOutputStatus,
                                   quint8 zoomLinkage);
    void _handleFunctionFeedback(quint8 infoType);
    void _handleVideoMode(quint8 mainStream, quint8 subStream);
    void _handleCommunicationError(const QString& message);
    void _handleReceiverStreamingChanged(bool active);
    void _handleReceiverDecodingChanged(bool active);
    void _handleReceiverRecordingChanged(bool active);
    void _handleReceiverStopRecordingComplete(int status);
    void _handleRecordingCommandTimeout();
    void _handleVideoModeCommandTimeout();
    void _handlePhotoCommandTimeout();
    void _handleLocalPhotoTimeout();
    void _handleLocalRecordingStartTimeout();
    void _handleLocalRecordingStopTimeout();

private:
    enum class ContinuousZoomPhase {
        Idle,
        ManualContinuous,
    };

    void _configureSdkEndpoint();
    bool _cameraCommandAvailable();
    bool _sendZoomStep(int direction);
    bool _zoomTapAvailable(int direction) const;
    bool _zoomHoldAvailable(int direction) const;
    bool _zoomBoundaryReached(int direction) const;
    void _retireContinuousZoomStopRetry();
    void _observeZoomFeedback(double zoomLevel);
    void _alignDisplayToMeasured(int preferredDirection = 0);
    void _pollPendingAbsoluteZoom();
    void _handleAbsoluteZoomConfirmationTimeout();
    void _setAbsoluteZoomTakeoverHint(double target);
    void _clearAbsoluteZoomTakeoverHint();
    void _startPendingContinuousZoom();
    void _pollContinuousZoom();
    void _retryContinuousZoomStop();
    void _finishContinuousZoomState();
    void _invalidateZoomState(bool preserveAbsoluteTakeoverHint = false);
    void _requestZoomState();
    bool _sendCameraRecordingToggle(bool targetRecording);
    void _requestRecordingStatusAfterDelay();
    void _setSdkResponding(bool responding);
    void _setCurrentZoom(double zoomLevel);
    void _setMaximumZoom(double zoomLevel);
    void _setMaximumZoomKnown(bool known);
    void _setZoomStatusKnown(bool known);
    void _setZoomCommandPending(bool pending);
    void _setCameraStatusKnown(bool known);
    void _setRecording(bool recording);
    void _setRecordingCommandPending(bool pending);
    void _setPhotoCommandPending(bool pending);
    void _setVideoModeKnown(bool known);
    void _setVideoMode(int mode);
    void _setVideoModePending(bool pending);
    void _setLastError(const QString& message);
    void _setLocalMediaError(const QString& message);
    void _notifyRecordingSessionStateChanged();
    void _clearRecordingResolutionCapability();
    bool _captureLocalVideoFrame();
    bool _startRecordingSession();
    bool _stopRecordingSession();
    void _startLocalRecording();
    void _stopLocalRecording();
    void _resetLocalRecordingState();
    void _finishLocalRecording(const QString& outputFile);
    bool _receiverStreaming() const;
    bool _receiverDecoding() const;
    bool _receiverRecording() const;
    quint16 _configuredSdkPort() const;

    static constexpr double kMinimumZoom = 1.0;
    // Command 0x0f can address 1.0x-30.0x. Commands 0x05/0x16/0x18
    // report the wider device/hybrid range independently. The wire field can
    // represent 255.9x, while this MT11 product policy supports up to 165.1x.
    static constexpr double kAbsoluteCommandMaximumZoom = 30.0;
    static constexpr double kSupportedHybridMaximumZoom = 165.1;
    static constexpr double kFeedbackMaximumZoom = 255.9;
    static constexpr int kRecordingCapabilityTimeoutMs = 4500;

    GimbalControlSettings* _settings = nullptr;
    Mt11Sdk* _sdk = nullptr;
    QTimer _pollTimer;
    QTimer _sdkResponseTimer;
    QTimer _maximumZoomFreshnessTimer;
    QTimer _recordingCapabilityTimeoutTimer;
    QTimer _zoomStatusFreshnessTimer;
    QTimer _absoluteZoomPollTimer;
    QTimer _absoluteZoomConfirmationTimer;
    QTimer _continuousZoomDirectionRetryTimer;
    QTimer _continuousZoomPollTimer;
    QTimer _continuousZoomWatchdog;
    QTimer _continuousZoomStopRetryTimer;
    QTimer _recordingStatusDelayTimer;
    QTimer _recordingCommandTimer;
    QTimer _videoModeCommandTimer;
    QTimer _photoCommandTimer;
    QTimer _localPhotoTimer;
    QTimer _localRecordingStartTimer;
    QTimer _localRecordingStopTimer;
    QThreadPool _localPhotoSaveThreadPool;
    bool _lastEnabled = false;
    bool _restoringSettings = false;
    QString _appliedSdkHost;
    quint16 _appliedSdkPort = 0;
    bool _sdkResponding = false;
    double _currentZoom = kMinimumZoom;
    double _measuredZoom = kMinimumZoom;
    bool _measuredZoomKnown = false;
    double _maximumZoom = kAbsoluteCommandMaximumZoom;
    bool _maximumZoomKnown = false;
    bool _zoomStatusKnown = false;
    bool _zoomCommandPending = false;
    double _pendingAbsoluteZoomTarget = kMinimumZoom;
    int _absoluteZoomTargetFeedbackCount = 0;
    // A confirmed 0x18 target does not prove that the firmware has released
    // the 0x0f controller/autofocus cycle. Retain this hint until the first
    // native 0x05 direction is written and the active gesture takes over, or
    // until the whole zoom state is invalidated.
    bool _absoluteZoomTakeoverHintValid = false;
    double _absoluteZoomTakeoverHintTarget = kMinimumZoom;
    bool _continuousZoomActive = false;
    int _continuousZoomDirection = 0;
    ContinuousZoomPhase _continuousZoomPhase = ContinuousZoomPhase::Idle;
    bool _continuousZoomDirectionSent = false;
    bool _continuousZoomDirectionRetryRequired = false;
    QElapsedTimer _continuousZoomMotionElapsed;
    double _continuousZoomMotionReference = kMinimumZoom;
    double _continuousZoomLastFeedback = kMinimumZoom;
    double _continuousZoomProgressWatermark = kMinimumZoom;
    int _continuousZoomDirectedProgressCount = 0;
    int _continuousZoomEndpointFeedbackCount = 0;
    bool _continuousZoomRequestedMotionObserved = false;
    // Reliable endpoint evidence never ends a still-pressed gesture. It only
    // allows a normal pointer release to omit the autofocus-triggering stop;
    // cancellation/lifecycle teardown still explicitly neutralizes it.
    int _continuousZoomEndpointLatchedDirection = 0;
    bool _postHoldZoomFeedbackPending = false;
    int _postHoldBoundaryCandidate = 0;
    int _postHoldBoundaryFeedbackCount = 0;
    bool _cameraStatusKnown = false;
    bool _recording = false;
    bool _recordingCommandPending = false;
    bool _recordingCommandTarget = false;
    bool _recordingSessionRequested = false;
    bool _photoCommandPending = false;
    int _photoCount = 0;
    int _localPhotoCount = 0;
    bool _videoModeKnown = false;
    int _videoMode = VideoModeZoom;
    bool _videoModePending = false;
    int _videoModeCommandTarget = VideoModeZoom;
    quint8 _mainVideoSource = 0xff;
    quint8 _subVideoSource = 0xff;
    QSize _recordingVideoSize;
    QSize _recordingResolutionCandidate;
    QSize _negotiatedPulledVideoSize;
    int _recordingResolutionConfirmationCount = 0;
    bool _recordingResolutionConfirmed = false;
    QPointer<QQuickItem> _videoItem;
    QPointer<VideoReceiver> _videoReceiver;
    QPointer<QObject> _localPhotoGrabLifetime;
    int _localPhotoGrabLifetimeCount = 0;
    quint64 _localPhotoSequence = 0;
    bool _localPhotoPending = false;
    bool _localRecordingIntent = false;
    bool _localRecordingActive = false;
    bool _localRecordingStartPending = false;
    bool _localRecordingStopPending = false;
    bool _localRecordingOwned = false;
    bool _receiverStreamingActive = false;
    bool _receiverDecodingActive = false;
    bool _receiverRecordingActive = false;
    QString _localRecordingFileBase;
    QString _localRecordingOutputFile;
    QStringList _issuedLocalRecordingFileBases;
    QStringList _detachedRecordingOutputs;
    quint64 _localRecordingSegmentCounter = 0;
    QString _lastError;
    QString _localMediaError;
    bool _shuttingDown = false;
};
