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
#include <QtCore/QQueue>
#include <QtCore/QSize>
#include <QtCore/QTimer>

class Fact;
class GimbalControlSettings;
class SiyiSdk;
class VideoManager;

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
    Q_PROPERTY(int photoCount READ photoCount NOTIFY photoCountChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

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
        return _absoluteZoomPending || _stableZoomConfirmationPending;
    }
    bool zoomValueUncertain() const { return _zoomValueUncertain; }
    bool continuousZoomActive() const { return _continuousZoomActive; }
    bool cameraStatusKnown() const { return _cameraStatusKnown; }
    bool recording() const { return _recording; }
    bool recordingCommandPending() const { return _recordingCommandPending; }
    int photoCount() const { return _photoCount; }
    QString lastError() const { return _lastError; }

    Q_INVOKABLE bool zoomIn();
    Q_INVOKABLE bool zoomOut();
    Q_INVOKABLE bool setZoom(double zoomLevel);
    Q_INVOKABLE bool startZoom(int direction);
    Q_INVOKABLE bool stopZoom();
    Q_INVOKABLE bool takePhoto();
    Q_INVOKABLE bool toggleVideoRecording();
    Q_INVOKABLE bool requestCurrentZoom();
    Q_INVOKABLE bool requestCameraStatus();

    /// Accepts the decoded main-stream size reported by the negotiated video
    /// sink. The caller must invoke this method on the manager's Qt thread.
    void setNegotiatedPulledVideoResolution(const QSize& videoSize);

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
    void photoCountChanged();
    void lastErrorChanged();

private slots:
    void _settingsChanged();
    void _handleZoomStepChanged();
    void _handleAbsoluteZoomFeedback(bool accepted);
    void _handleMaximumZoom(double maximumZoom);
    void _handleCurrentZoom(double zoomLevel);
    void _handlePulledVideoSize();
    void _handleVideoDecodingChanged();
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
    void _sendNextContinuousZoomStep();
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
                                 bool alignmentCorrection = false);
    bool _queueZoomStep(int direction);
    bool _zoomPlanningReference(double* zoomLevel) const;
    bool _zoomDirectionAvailable(int direction) const;
    void _clearQueuedZoomSteps();
    void _dispatchNextZoomStep();
    AlignmentAttemptResult _dispatchPendingZoomIntentFrom(double referenceZoom);
    bool _sendAlignmentCorrection(double targetZoom, double sourceZoom);
    bool _sendCurrentZoomQuery(bool startOperationDeadline);
    void _cancelOutstandingZoomQuery();
    bool _stopContinuousZoom();
    void _handleStableUnexpectedZoom(double zoomLevel);
    AlignmentAttemptResult _tryRealignStableZoom(double zoomLevel,
                                                  int direction = 0);
    bool _automaticAlignmentSuppressedFor(double zoomLevel) const;
    void _suppressAutomaticAlignment(double zoomLevel);
    void _clearAutomaticAlignmentSuppression();
    void _clearStableZoomConfirmation();
    void _beginStableZoomConfirmation(bool normalizeToStepGrid, int direction);
    void _finalizeConfirmedZoom(double zoomLevel);
    void _resetMaximumZoomCapability();
    void _refreshMaximumZoomCapability(const char* sourceDescription);
    void _tryConfirmPulledVideoResolution();
    void _schedulePulledVideoResolutionFallback();
    void _tryConfirmPulledVideoResolutionFallback();
    void _invalidatePulledVideoResolutionCapability(const QSize& videoSize,
                                                    const char* sourceDescription);
    void _applyMaximumZoomCapability(double maximumZoom,
                                     const char* sourceDescription);
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
    void _syncCameraStatus();
    void _setLastError(const QString& message);
    bool _cameraCommandAvailable();
    quint16 _sdkPort() const;

    static constexpr double kMinZoom = 1.0;
    static constexpr double kDefaultMaxZoom = 5.5;
    static constexpr double kProtocolMaxZoom = 6.0;
    static constexpr int kMaximumAlignmentAttempts = 2;
    static constexpr int kMaximumQueuedZoomSteps = 32;

    GimbalControlSettings* _settings = nullptr;
    SiyiSdk* _sdk = nullptr;
    VideoManager* _videoManager = nullptr;
    QTimer _sdkResponseTimer;
    QTimer _zoomOperationTimer;
    QTimer _zoomQueryTimeoutTimer;
    QTimer _sdkPollTimer;
    QTimer _continuousZoomWatchdog;
    QTimer _continuousZoomStepTimer;
    QTimer _zoomSyncTimer;
    QTimer _pulledVideoFallbackTimer;
    QTimer _photoFeedbackTimer;
    QTimer _recordingStatusDelayTimer;
    QTimer _recordingCommandTimeoutTimer;
    QElapsedTimer _absoluteZoomElapsed;
    double _currentZoom = kMinZoom;
    double _maximumZoom = kDefaultMaxZoom;
    double _capabilityMaximumZoom = kDefaultMaxZoom;
    double _pulledVideoMaximumZoom = kDefaultMaxZoom;
    double _deviceMaximumZoom = kProtocolMaxZoom;
    double _requestedZoom = kMinZoom;
    bool _lastEnabled = false;
    bool _sdkResponding = false;
    bool _maximumZoomKnown = false;
    bool _videoStreamAvailable = false;
    bool _pulledVideoResolutionConfirmed = false;
    bool _deviceMaximumZoomKnown = false;
    QSize _negotiatedPulledVideoSize;
    QSize _videoManagerFallbackCandidate;
    QSize _lastRejectedPulledVideoSize;
    bool _zoomStatusKnown = false;
    bool _zoomValueUncertain = false;
    bool _zoomResponseBlocked = false;
    bool _zoomQueryOutstanding = false;
    bool _absoluteZoomPending = false;
    bool _latestActualZoomKnown = false;
    double _latestActualZoom = kMinZoom;
    QQueue<int> _queuedZoomDirections;
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
    bool _cameraStatusKnown = false;
    bool _recording = false;
    bool _recordingCommandPending = false;
    bool _recordingCommandTarget = false;
    bool _recordingStatusResponseAllowed = false;
    int _photoCount = 0;
    bool _photoCommandPending = false;
    QString _lastError;
};
