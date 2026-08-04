/****************************************************************************
 *
 * 思翼云台相机控制管理器。
 * QML 只调用该类的缩放/拍照/录像接口；协议封包和 UDP 发送由 SiyiSdk/SiyiProtocol 处理。
 *
 ****************************************************************************/

#pragma once

#include "A8MiniZoomPolicy.h"

#include <QtCore/QElapsedTimer>
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QSize>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QTimer>

class Fact;
class GimbalControlSettings;
class QQuickItem;
class SiyiSdk;
class VideoManager;
class VideoReceiver;

class GimbalControlManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled NOTIFY enabledChanged)
    Q_PROPERTY(double currentZoom READ currentZoom NOTIFY currentZoomChanged)
    Q_PROPERTY(double zoomStep READ zoomStep NOTIFY zoomStepChanged)
    Q_PROPERTY(double minimumZoom READ minimumZoom CONSTANT)
    Q_PROPERTY(double maximumZoom READ maximumZoom NOTIFY maximumZoomChanged)
    Q_PROPERTY(bool sdkResponding READ sdkResponding NOTIFY sdkRespondingChanged)
    Q_PROPERTY(bool zoomStatusKnown READ zoomStatusKnown NOTIFY zoomStatusKnownChanged)
    Q_PROPERTY(bool zoomInAvailable READ zoomInAvailable NOTIFY zoomAvailabilityChanged)
    Q_PROPERTY(bool zoomOutAvailable READ zoomOutAvailable NOTIFY zoomAvailabilityChanged)
    Q_PROPERTY(bool zoomControlsUnlocked READ zoomControlsUnlocked NOTIFY zoomAvailabilityChanged)
    Q_PROPERTY(bool zoomCommandPending READ zoomCommandPending NOTIFY zoomAvailabilityChanged)
    Q_PROPERTY(bool zoomValueUncertain READ zoomValueUncertain NOTIFY zoomAvailabilityChanged)
    Q_PROPERTY(bool continuousZoomActive READ continuousZoomActive NOTIFY continuousZoomActiveChanged)
    Q_PROPERTY(bool cameraStatusKnown READ cameraStatusKnown NOTIFY cameraStatusKnownChanged)
    Q_PROPERTY(bool recording READ recording NOTIFY recordingChanged)
    Q_PROPERTY(bool recordingCommandPending READ recordingCommandPending NOTIFY recordingCommandPendingChanged)
    Q_PROPERTY(bool cameraRecordingPending READ cameraRecordingPending NOTIFY recordingSessionStateChanged)
    Q_PROPERTY(bool localMediaStorageEnabled READ localMediaStorageEnabled NOTIFY localMediaStorageEnabledChanged)
    Q_PROPERTY(bool localRecording READ localRecording NOTIFY localRecordingStateChanged)
    Q_PROPERTY(bool localRecordingPending READ localRecordingPending NOTIFY localRecordingStateChanged)
    Q_PROPERTY(bool recordingSessionActive READ recordingSessionActive NOTIFY recordingSessionStateChanged)
    Q_PROPERTY(bool recordingSessionCapturing READ recordingSessionCapturing NOTIFY recordingSessionStateChanged)
    Q_PROPERTY(bool videoRecordingAvailable READ videoRecordingAvailable NOTIFY recordingSessionStateChanged)
    Q_PROPERTY(int photoCount READ photoCount NOTIFY photoCountChanged)
    Q_PROPERTY(int localPhotoCount READ localPhotoCount NOTIFY localPhotoCountChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(QString localMediaError READ localMediaError NOTIFY localMediaErrorChanged)

public:
    explicit GimbalControlManager(GimbalControlSettings* settings, QObject* parent = nullptr);
    ~GimbalControlManager() override;

    bool enabled() const;
    double currentZoom() const { return _currentZoom; }
    double zoomStep() const;
    double minimumZoom() const { return kMinZoom; }
    double maximumZoom() const { return _maximumZoom; }
    bool sdkResponding() const { return _sdkResponding; }
    bool zoomStatusKnown() const { return _zoomStatusKnown; }
    bool zoomInAvailable() const;
    bool zoomOutAvailable() const;
    bool zoomControlsUnlocked() const {
        return enabled() && _videoStreamAvailable && _maximumZoomKnown;
    }
    bool zoomCommandPending() const {
        return _absoluteZoomPending
            || _stableZoomConfirmationPending
            || _manualZoomFinalizePending;
    }
    bool zoomValueUncertain() const { return _zoomValueUncertain; }
    bool continuousZoomActive() const { return _continuousZoomActive; }
    bool cameraStatusKnown() const { return _cameraStatusKnown; }
    bool recording() const { return _recording; }
    bool recordingCommandPending() const { return _recordingCommandPending; }
    bool cameraRecordingPending() const;
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

    /// Accepts the decoded main-stream size reported by the negotiated video
    /// sink. The caller must invoke this method on the manager's Qt thread.
    void setNegotiatedPulledVideoResolution(const QSize& videoSize);
    /// Retains the main rendered video item for local frame snapshots. The
    /// QPointer is cleared automatically when the QML item is destroyed.
    void setMainVideoItem(QQuickItem* videoItem);
    /// Retains the non-thermal receiver so local recording affects only the
    /// current main video instead of VideoManager's full receiver list.
    void setMainVideoReceiver(VideoReceiver* receiver);
    /// Stops only a local recording session started by this manager.
    void shutdownLocalMedia(bool waitForStop = false);
    /// Correlates the main receiver's asynchronous recorder result with the
    /// filename requested by this manager before ownership is confirmed.
    void handleMainVideoRecordingStartResult(bool success,
                                             const QString& outputFile);

signals:
    void enabledChanged();
    void currentZoomChanged();
    void zoomStepChanged();
    void maximumZoomChanged();
    void sdkRespondingChanged();
    void zoomStatusKnownChanged();
    void zoomAvailabilityChanged();
    void continuousZoomActiveChanged();
    void cameraStatusKnownChanged();
    void recordingChanged();
    void recordingCommandPendingChanged();
    void localMediaStorageEnabledChanged();
    void localRecordingStateChanged();
    void recordingSessionStateChanged();
    void photoCountChanged();
    void localPhotoCountChanged();
    void lastErrorChanged();
    void localMediaErrorChanged();

private slots:
    void _settingsChanged();
    void _handleZoomStepChanged();
    void _handleManualZoomFeedback(double zoomLevel);
    void _handleAbsoluteZoomFeedback(bool accepted);
    void _handleMaximumZoom(double maximumZoom);
    void _handleRecordingStreamParameters(quint8 videoEncodingType,
                                          quint16 width,
                                          quint16 height,
                                          quint16 bitrateKbps,
                                          quint8 frameRate);
    void _handleCurrentZoom(double zoomLevel);
    void _expireRecordingResolutionCapability();
    void _handlePulledVideoSize();
    void _handleVideoDecodingChanged();
    void _handleVideoStreamingChanged();
    void _handleVideoRecordingChanged();
    void _handleLocalMediaStorageEnabledChanged();
    void _handleLocalRecordingStartTimeout();
    void _handleLocalRecordingStopTimeout();
    void _handleCameraSystemStatus(quint8 hdrStatus,
                                   quint8 recordingStatus,
                                   quint8 gimbalMotionMode,
                                   quint8 gimbalMountingDirection,
                                   quint8 videoOutputStatus,
                                   quint8 zoomLinkage);
    void _handleFunctionFeedback(quint8 infoType);
    void _handleCommunicationError(const QString& message);
    void _markSdkNotResponding();
    void _markZoomStatusUnknown();
    void _handleZoomQueryTimeout();
    void _pollSdk();
    void _requestZoomAfterSettle();
    void _pollContinuousZoom();
    void _retryManualZoomStop();
    void _expireManualZoomFinalize();
    void _stopContinuousZoomForSafety();
    void _requestRecordingStatusAfterDelay();
    void _handleRecordingCommandTimeout();

private:
    enum class AlignmentAttemptResult {
        NotNeeded,
        CommandSent,
        SendFailed,
    };

    void _configureSdkEndpoint();
    bool _sendAbsoluteZoomTarget(double zoomLevel,
                                 bool alignmentCorrection = false,
                                 bool replacePendingTarget = false,
                                 bool manualFinalizeCorrection = false);
    bool _sendZoomStep(int direction);
    bool _advanceHeldZoomDisplayTarget();
    bool _heldZoomDisplayAtTerminal() const;
    bool _zoomPlanningReference(double* zoomLevel) const;
    bool _zoomDirectionAvailable(int direction) const;
    bool _sendAlignmentCorrection(double targetZoom, double sourceZoom);
    bool _sendCurrentZoomQuery(bool startOperationDeadline);
    void _cancelOutstandingZoomQuery();
    bool _stopContinuousZoom(bool finalizeAfterStop = true);
    bool _flushPendingManualZoomStop();
    bool _sendPendingManualZoomStop();
    bool _manualZoomFinalizeDeadlineOpen() const;
    void _cancelManualZoomFinalize();
    void _finishManualZoomStop(double zoomLevel);
    AlignmentAttemptResult _tryRealignStableZoom(double zoomLevel,
                                                  int direction = 0);
    bool _automaticAlignmentSuppressedFor(double zoomLevel) const;
    void _suppressAutomaticAlignment(double zoomLevel);
    void _clearAutomaticAlignmentSuppression();
    void _clearStableZoomConfirmation();
    void _beginStableZoomConfirmation(bool normalizeToStepGrid, int direction);
    void _finalizeConfirmedZoom(double zoomLevel);
    void _resetMaximumZoomCapability();
    void _refreshMaximumZoomCapability();
    void _tryConfirmPulledVideoResolution();
    void _schedulePulledVideoResolutionFallback();
    void _tryConfirmPulledVideoResolutionFallback();
    void _invalidatePulledVideoResolutionCapability(const QSize& videoSize,
                                                    const char* sourceDescription);
    void _applyMaximumZoomCapability(double maximumZoom);
    bool _confirmPulledVideoResolution(const QSize& videoSize,
                                       const char* sourceDescription);
    void _scheduleZoomSync();
    void _setCurrentZoom(double zoomLevel);
    void _setMaximumZoom(double zoomLevel);
    void _setMaximumZoomKnown(bool known);
    void _setVideoStreamAvailable(bool available);
    void _setSdkResponding(bool responding);
    void _setZoomStatusKnown(bool known);
    void _setZoomValueUncertain(bool uncertain);
    void _setContinuousZoomState(bool active, int direction = 0);
    void _setCameraStatusKnown(bool known);
    void _setRecording(bool recording);
    void _setRecordingCommandPending(bool pending);
    void _finishRecordingCommand();
    bool _startRecordingSession();
    bool _stopRecordingSession();
    bool _sendCameraRecordingToggle(bool targetRecording);
    void _reconcileCameraRecordingIntent();
    bool _captureLocalVideoFrame();
    void _reconcileLocalRecording();
    void _startLocalRecording();
    void _stopLocalRecording();
    bool _setLocalRecordingActive(bool active);
    void _setLocalMediaError(const QString& message);
    void _notifyRecordingSessionStateChanged();
    void _syncCameraStatus();
    void _setLastError(const QString& message);
    bool _cameraCommandAvailable();
    quint16 _sdkPort() const;

    static constexpr double kMinZoom = 1.0;
    static constexpr double kDefaultMaxZoom = 5.5;
    static constexpr double kProtocolMaxZoom = 6.0;
    static constexpr int kMaximumAlignmentAttempts = 2;
    static constexpr int kDefaultZoomQueryTimeoutMs = 1000;
    static constexpr int kManualZoomPollIntervalMs = 120;
    static constexpr int kHeldZoomPressThresholdMs = 420;
    static constexpr int kHeldZoomStepPeriodMs = 600;
    static constexpr int kManualZoomQueryTimeoutMs = 250;
    static constexpr int kManualZoomStopQueryDelayMs = 350;
    static constexpr int kManualZoomStopRetryMs = 80;
    static constexpr int kRecordingCapabilityTimeoutMs = 4500;
    static constexpr int kManualZoomFinalizeTimeoutMs = 1000;
    static constexpr int kManualZoomFinalConfirmationCount = 2;
    static constexpr int kManualZoomStopMaximumRetryAttempts = 2;

    GimbalControlSettings* _settings = nullptr;
    SiyiSdk* _sdk = nullptr;
    VideoManager* _videoManager = nullptr;
    QTimer _sdkResponseTimer;
    QTimer _zoomOperationTimer;
    QTimer _zoomQueryTimeoutTimer;
    QTimer _sdkPollTimer;
    QTimer _recordingCapabilityTimeoutTimer;
    QTimer _continuousZoomWatchdog;
    QTimer _continuousZoomStepTimer;
    QTimer _manualZoomStopRetryTimer;
    QTimer _manualZoomFinalizeTimer;
    QTimer _zoomSyncTimer;
    QTimer _pulledVideoFallbackTimer;
    QTimer _photoFeedbackTimer;
    QTimer _recordingStatusDelayTimer;
    QTimer _recordingCommandTimeoutTimer;
    QTimer _localRecordingStartTimer;
    QTimer _localRecordingStopTimer;
    QElapsedTimer _manualZoomFinalizeElapsed;
    QElapsedTimer _heldZoomElapsed;
    double _currentZoom = kMinZoom;
    double _maximumZoom = kDefaultMaxZoom;
    double _capabilityMaximumZoom = kDefaultMaxZoom;
    double _recordingResolutionMaximumZoom = kDefaultMaxZoom;
    double _deviceMaximumZoom = kProtocolMaxZoom;
    double _requestedZoom = kMinZoom;
    bool _lastEnabled = false;
    bool _sdkResponding = false;
    bool _maximumZoomKnown = false;
    bool _videoStreamAvailable = false;
    bool _pulledVideoResolutionConfirmed = false;
    bool _recordingResolutionConfirmed = false;
    bool _deviceMaximumZoomKnown = false;
    QSize _negotiatedPulledVideoSize;
    QSize _recordingVideoSize;
    QSize _videoManagerFallbackCandidate;
    QSize _lastRejectedPulledVideoSize;
    QSize _lastRejectedRecordingVideoSize;
    bool _zoomStatusKnown = false;
    bool _zoomValueUncertain = false;
    bool _zoomResponseBlocked = false;
    bool _zoomQueryOutstanding = false;
    bool _absoluteZoomPending = false;
    bool _latestActualZoomKnown = false;
    double _latestActualZoom = kMinZoom;
    int _alignmentAttemptCount = 0;
    bool _automaticAlignmentSuppressed = false;
    double _suppressedAlignmentZoom = kMinZoom;
    double _suppressedAlignmentStep = 1.0;
    double _suppressedAlignmentMaximum = kDefaultMaxZoom;
    bool _alignmentSourceZoomValid = false;
    double _alignmentSourceZoom = kMinZoom;
    A8MiniZoomPolicy::TargetTracker _absoluteZoomTracker;
    bool _stableZoomConfirmationPending = false;
    bool _stableZoomCandidateValid = false;
    double _stableZoomCandidate = kMinZoom;
    bool _normalizeAfterStableZoom = false;
    int _stableZoomDirection = 0;
    bool _continuousZoomActive = false;
    int _continuousZoomDirection = 0;
    double _heldZoomStartTarget = kMinZoom;
    double _heldZoomLastTarget = kMinZoom;
    int _heldZoomInitialPressDurationMs = kHeldZoomPressThresholdMs;
    bool _manualZoomFinalizePending = false;
    int _manualZoomFinalizeDirection = 0;
    bool _manualZoomFinalCandidateValid = false;
    double _manualZoomFinalCandidate = kMinZoom;
    int _manualZoomFinalMatchCount = 0;
    bool _suppressIdleAlignmentUntilExplicitZoom = false;
    // A native 0x05 hold deliberately retains its legal elapsed-time target
    // instead of letting delayed or quantized 0x18 feedback rename it or cause
    // a reverse correction. Keep this latch until a later explicit target is
    // accepted or the camera capability/session is reset.
    bool _nativeHoldTargetLatched = false;
    // Log exactly one solicited 0x18 observation after a native hold stops.
    // Periodic idle feedback remains silent.
    bool _nativeHoldFeedbackLogPending = false;
    QString _manualZoomSessionHost;
    quint16 _manualZoomSessionPort = 0;
    int _manualZoomStopRetryAttemptsRemaining = 0;
    bool _cameraStatusKnown = false;
    bool _recording = false;
    bool _recordingCommandPending = false;
    bool _recordingCommandTarget = false;
    bool _recordingStatusResponseAllowed = false;
    bool _recordingSessionRequested = false;
    bool _cameraRecordingIntentValid = false;
    bool _cameraRecordingIntentTarget = false;
    bool _cameraRecordingStartBlocked = false;
    bool _cameraRecordingStopBlocked = false;
    bool _localRecordingIntent = false;
    bool _localRecordingActive = false;
    bool _localRecordingOwned = false;
    bool _localRecordingOwnershipConfirmed = false;
    bool _localRecordingUsingExternalSession = false;
    bool _localRecordingStartPending = false;
    bool _localRecordingStopPending = false;
    bool _localRecordingStartBlocked = false;
    bool _localRecordingResumeOnStream = false;
    bool _localRecordingRetryAfterGenerationResolved = false;
    int _localRecordingStopRetryCount = 0;
    QString _localRecordingFileBase;
    // Successful receiver callbacks include the requested basename. Retain
    // unresolved generations across the UI timeout so a late success can
    // still be recognized as ours and stopped safely.
    QStringList _localRecordingIssuedFileBases;
    quint64 _localRecordingSegmentCounter = 0;
    QPointer<QQuickItem> _mainVideoItem;
    QPointer<VideoReceiver> _mainVideoReceiver;
    int _photoCount = 0;
    int _localPhotoCount = 0;
    quint64 _localPhotoRequestSequence = 0;
    bool _photoCommandPending = false;
    QString _lastError;
    QString _localMediaError;
};
