/****************************************************************************
 *
 * 思翼云台相机控制管理器。
 * QML 只调用该类的缩放/拍照/录像接口；协议封包和 UDP 发送由 SiyiSdk/SiyiProtocol 处理。
 *
 ****************************************************************************/

#pragma once

#include <QtCore/QObject>
#include <QtCore/QTimer>

class Fact;
class GimbalControlSettings;
class SiyiSdk;

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

signals:
    void enabledChanged();
    void currentZoomChanged();
    void zoomStepChanged();
    void maximumZoomChanged();
    void sdkRespondingChanged();
    void zoomStatusKnownChanged();
    void continuousZoomActiveChanged();
    void cameraStatusKnownChanged();
    void recordingChanged();
    void recordingCommandPendingChanged();
    void photoCountChanged();
    void lastErrorChanged();

private slots:
    void _settingsChanged();
    void _handleMaximumZoom(double zoomLevel);
    void _handleCurrentZoom(double zoomLevel);
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
    void _pollSdk();
    void _requestZoomAfterSettle();
    void _stopContinuousZoomForSafety();
    void _requestRecordingStatusAfterDelay();
    void _handleRecordingCommandTimeout();

private:
    void _configureSdkEndpoint();
    bool _stopContinuousZoom(bool scheduleZoomSync);
    void _clearStableZoomConfirmation();
    void _resetMaximumZoomCapability();
    void _scheduleZoomSync();
    void _setCurrentZoom(double zoomLevel);
    void _setMaximumZoom(double zoomLevel);
    void _setSdkResponding(bool responding);
    void _setZoomStatusKnown(bool known);
    void _setContinuousZoomState(bool active, int direction = 0);
    void _setCameraStatusKnown(bool known);
    void _setRecording(bool recording);
    void _setRecordingCommandPending(bool pending);
    void _finishRecordingCommand();
    void _syncCameraStatus();
    void _setLastError(const QString& message);
    bool _cameraCommandAvailable();
    bool _sendZoomStopTo(const QString& host, quint16 port);
    QString _sdkHost() const;
    double _clampZoom(double zoomLevel) const;
    quint16 _sdkPort() const;

    static constexpr double kMinZoom = 1.0;
    static constexpr double kDefaultMaxZoom = 5.5;
    static constexpr double kProtocolMaxZoom = 6.0;

    GimbalControlSettings* _settings = nullptr;
    SiyiSdk* _sdk = nullptr;
    QTimer _sdkResponseTimer;
    QTimer _zoomQueryResponseTimer;
    QTimer _sdkPollTimer;
    QTimer _continuousZoomWatchdog;
    QTimer _zoomSyncTimer;
    QTimer _photoFeedbackTimer;
    QTimer _recordingStatusDelayTimer;
    QTimer _recordingCommandTimeoutTimer;
    double _currentZoom = kMinZoom;
    double _maximumZoom = kDefaultMaxZoom;
    double _requestedZoom = kMinZoom;
    bool _lastEnabled = false;
    bool _sdkResponding = false;
    bool _maximumZoomKnown = false;
    bool _zoomStatusKnown = false;
    bool _zoomResponseBlocked = false;
    bool _absoluteZoomPending = false;
    bool _stableZoomConfirmationPending = false;
    bool _stableZoomCandidateValid = false;
    double _stableZoomCandidate = kMinZoom;
    bool _continuousZoomActive = false;
    int _continuousZoomDirection = 0;
    QString _continuousZoomHost;
    quint16 _continuousZoomPort = 0;
    bool _cameraStatusKnown = false;
    bool _recording = false;
    bool _recordingCommandPending = false;
    bool _recordingCommandTarget = false;
    bool _recordingStatusResponseAllowed = false;
    int _photoCount = 0;
    bool _photoCommandPending = false;
    QString _lastError;
};
