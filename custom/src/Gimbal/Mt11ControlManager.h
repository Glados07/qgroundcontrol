/****************************************************************************
 *
 * UniPod MT11 camera-control and local-media coordinator.
 *
 ****************************************************************************/

#pragma once

#include <QtCore/QElapsedTimer>
#include <QtCore/QObject>
#include <QtCore/QPointer>
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
    Q_PROPERTY(double zoomStep READ zoomStep NOTIFY zoomStepChanged)
    Q_PROPERTY(double minimumZoom READ minimumZoom CONSTANT)
    Q_PROPERTY(double maximumZoom READ maximumZoom NOTIFY maximumZoomChanged)
    Q_PROPERTY(bool zoomStatusKnown READ zoomStatusKnown NOTIFY zoomStatusKnownChanged)
    Q_PROPERTY(bool zoomInAvailable READ zoomInAvailable NOTIFY zoomAvailabilityChanged)
    Q_PROPERTY(bool zoomOutAvailable READ zoomOutAvailable NOTIFY zoomAvailabilityChanged)
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
    Q_PROPERTY(bool thermalModeKnown READ thermalModeKnown NOTIFY thermalModeKnownChanged)
    Q_PROPERTY(bool thermalModeEnabled READ thermalModeEnabled NOTIFY thermalModeEnabledChanged)
    Q_PROPERTY(bool thermalCommandPending READ thermalCommandPending NOTIFY thermalCommandPendingChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(QString localMediaError READ localMediaError NOTIFY localMediaErrorChanged)

public:
    explicit Mt11ControlManager(GimbalControlSettings* settings,
                                QObject* parent = nullptr);
    ~Mt11ControlManager() override;

    bool enabled() const;
    bool sdkResponding() const { return _sdkResponding; }
    double currentZoom() const { return _currentZoom; }
    double zoomStep() const;
    double minimumZoom() const { return kMinimumZoom; }
    double maximumZoom() const { return _maximumZoom; }
    bool zoomStatusKnown() const { return _zoomStatusKnown; }
    bool zoomInAvailable() const;
    bool zoomOutAvailable() const;
    bool zoomControlsUnlocked() const {
        return enabled() && _sdkResponding && _maximumZoomKnown
            && _zoomStatusKnown && !_thermalCommandPending;
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
    bool thermalModeKnown() const { return _thermalModeKnown; }
    bool thermalModeEnabled() const { return _thermalModeEnabled; }
    bool thermalCommandPending() const { return _thermalCommandPending; }
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
    Q_INVOKABLE bool toggleThermalMode();

    void setVideoItem(QQuickItem* videoItem);
    void setVideoReceiver(VideoReceiver* receiver);
    void handleVideoRecordingStartResult(bool success,
                                         const QString& outputFile);
    void shutdownLocalMedia(bool waitForStop = false);
    void finalizeDetachedLocalMedia();

signals:
    void enabledChanged();
    void sdkRespondingChanged();
    void currentZoomChanged();
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
    void _handleThermalCommandTimeout();
    void _handlePhotoCommandTimeout();
    void _handleLocalPhotoTimeout();
    void _handleLocalRecordingStartTimeout();
    void _handleLocalRecordingStopTimeout();

private:
    void _configureSdkEndpoint();
    bool _cameraCommandAvailable();
    bool _sendZoomStep(int direction);
    void _pollPendingAbsoluteZoom();
    void _handleAbsoluteZoomConfirmationTimeout();
    void _pollContinuousZoom();
    void _retryContinuousZoomStop();
    void _finishContinuousZoomState();
    void _checkContinuousZoomBoundary();
    void _invalidateZoomState();
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
    void _setThermalModeKnown(bool known);
    void _setThermalModeEnabled(bool enabled);
    void _setThermalCommandPending(bool pending);
    void _setLastError(const QString& message);
    void _setLocalMediaError(const QString& message);
    void _notifyRecordingSessionStateChanged();
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

    GimbalControlSettings* _settings = nullptr;
    Mt11Sdk* _sdk = nullptr;
    QTimer _pollTimer;
    QTimer _sdkResponseTimer;
    QTimer _maximumZoomFreshnessTimer;
    QTimer _zoomStatusFreshnessTimer;
    QTimer _absoluteZoomPollTimer;
    QTimer _absoluteZoomConfirmationTimer;
    QTimer _continuousZoomPollTimer;
    QTimer _continuousZoomWatchdog;
    QTimer _continuousZoomStopRetryTimer;
    QTimer _recordingStatusDelayTimer;
    QTimer _recordingCommandTimer;
    QTimer _thermalCommandTimer;
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
    double _maximumZoom = kAbsoluteCommandMaximumZoom;
    bool _maximumZoomKnown = false;
    bool _zoomStatusKnown = false;
    bool _zoomCommandPending = false;
    double _pendingAbsoluteZoomTarget = kMinimumZoom;
    bool _continuousZoomActive = false;
    int _continuousZoomDirection = 0;
    bool _cameraStatusKnown = false;
    bool _recording = false;
    bool _recordingCommandPending = false;
    bool _recordingCommandTarget = false;
    bool _recordingSessionRequested = false;
    bool _photoCommandPending = false;
    int _photoCount = 0;
    int _localPhotoCount = 0;
    bool _thermalModeKnown = false;
    bool _thermalModeEnabled = false;
    bool _thermalCommandPending = false;
    bool _thermalCommandTarget = false;
    quint8 _mainVideoSource = 0xff;
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
