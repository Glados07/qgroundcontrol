/****************************************************************************
 * Rouke payload controls with an independent local-media branch for Video 2.
 ****************************************************************************/
#pragma once

#include "RoukePayloadController.h"
#include "RoukeGimbalController.h"
#include <QtCore/QPointer>
#include <QtCore/QSize>
#include <QtCore/QStringList>
#include <QtCore/QThreadPool>

class GimbalControlSettings;
class QQuickItem;
class VideoReceiver;

class RoukeControlManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled NOTIFY enabledChanged)
    Q_PROPERTY(bool controlAvailable READ controlAvailable NOTIFY stateChanged)
    Q_PROPERTY(bool sdkResponding READ sdkResponding NOTIFY stateChanged)
    Q_PROPERTY(bool pending READ pending NOTIFY stateChanged)
    Q_PROPERTY(bool gimbalCenterPending READ gimbalCenterPending NOTIFY stateChanged)
    Q_PROPERTY(bool recording READ recording NOTIFY stateChanged)
    Q_PROPERTY(bool recordingKnown READ recordingKnown NOTIFY stateChanged)
    Q_PROPERTY(bool cameraRecordingPending READ cameraRecordingPending NOTIFY stateChanged)
    Q_PROPERTY(bool photoCommandPending READ photoCommandPending NOTIFY stateChanged)
    Q_PROPERTY(int viewType READ viewType NOTIFY stateChanged)
    Q_PROPERTY(bool viewKnown READ viewKnown NOTIFY stateChanged)
    Q_PROPERTY(int photoCount READ photoCount NOTIFY stateChanged)
    Q_PROPERTY(int discoveredPayloadAddress READ discoveredPayloadAddress NOTIFY stateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY stateChanged)
    Q_PROPERTY(bool localMediaStorageEnabled READ localMediaStorageEnabled NOTIFY localMediaStorageEnabledChanged)
    Q_PROPERTY(bool localRecording READ localRecording NOTIFY localRecordingStateChanged)
    Q_PROPERTY(bool localRecordingPending READ localRecordingPending NOTIFY localRecordingStateChanged)
    Q_PROPERTY(bool recordingSessionActive READ recordingSessionActive NOTIFY recordingSessionStateChanged)
    Q_PROPERTY(bool recordingSessionCapturing READ recordingSessionCapturing NOTIFY recordingSessionStateChanged)
    Q_PROPERTY(bool videoRecordingAvailable READ videoRecordingAvailable NOTIFY recordingSessionStateChanged)
    Q_PROPERTY(int localPhotoCount READ localPhotoCount NOTIFY localPhotoCountChanged)
    Q_PROPERTY(QString localMediaError READ localMediaError NOTIFY localMediaErrorChanged)

public:
    explicit RoukeControlManager(GimbalControlSettings *settings, QObject *parent = nullptr);
    ~RoukeControlManager() override;
    bool enabled() const;
    bool controlAvailable() const { return _controller.available(); }
    bool sdkResponding() const { return _controller.responding(); }
    bool pending() const { return _controller.pending(); }
    bool gimbalCenterPending() const { return _controller.gimbalCenterPending(); }
    bool recording() const { return _controller.recording(); }
    bool recordingKnown() const { return _controller.recordingKnown(); }
    bool cameraRecordingPending() const { return _controller.recordingStopQueued() || _controller.pendingCommand() == QStringLiteral("lr1_record"); }
    bool photoCommandPending() const { return _controller.pendingCommand() == QStringLiteral("lr1_take_photo"); }
    int viewType() const { return _controller.viewType(); }
    bool viewKnown() const { return _controller.viewKnown(); }
    int photoCount() const { return _controller.photoCount(); }
    int discoveredPayloadAddress() const { return _controller.discoveredPayloadAddress(); }
    QString lastError() const { return _controller.lastError(); }
    bool localMediaStorageEnabled() const;
    bool localRecording() const { return _localRecordingActive; }
    bool localRecordingPending() const { return _localRecordingStartPending || _localRecordingStopPending; }
    bool recordingSessionActive() const;
    bool recordingSessionCapturing() const;
    bool videoRecordingAvailable() const;
    int localPhotoCount() const { return _localPhotoCount; }
    QString localMediaError() const { return _localMediaError; }

    Q_INVOKABLE bool takePhoto();
    Q_INVOKABLE bool startRecording();
    Q_INVOKABLE bool stopRecording();
    Q_INVOKABLE bool toggleRecording();
    Q_INVOKABLE bool toggleVideoRecording() { return toggleRecording(); }
    Q_INVOKABLE bool setView(int viewType) { return _controller.setView(viewType); }
    Q_INVOKABLE bool autoFocus() { return _controller.autoFocus(); }
    Q_INVOKABLE bool centerGimbal() { return _gimbalController.centerGimbal(); }

    void updateUniRcGimbalChannels(qint16 channel11, qint16 channel12, qint16 channel14)
        { _gimbalController.updateChannels(channel11, channel12, channel14); }
    void cancelUniRcGimbal() { _gimbalController.cancel(); }

    void setVideoItem(QQuickItem *videoItem);
    void setVideoReceiver(VideoReceiver *receiver);
    void setNegotiatedPulledVideoResolution(VideoReceiver *sourceReceiver, const QSize &videoSize);
    void handleVideoRecordingStartResult(bool success, const QString &outputFile);
    void shutdownLocalMedia(bool waitForStop = false);
    void finalizeDetachedLocalMedia();

signals:
    void enabledChanged();
    void stateChanged();
    void localMediaStorageEnabledChanged();
    void localRecordingStateChanged();
    void recordingSessionStateChanged();
    void localPhotoCountChanged();
    void localMediaErrorChanged();

private:
    void _settingsChanged();
    void _gimbalSettingsChanged();
    void _handleReceiverStreamingChanged(bool active);
    void _handleReceiverDecodingChanged(bool active);
    void _handleReceiverRecordingChanged(bool active);
    void _handleReceiverStopRecordingComplete(int status);
    void _handleLocalPhotoTimeout();
    void _handleLocalRecordingStartTimeout();
    void _handleLocalRecordingStopTimeout();
    void _setLocalMediaError(const QString &message);
    void _notifyRecordingSessionStateChanged();
    bool _captureLocalVideoFrame();
    void _startLocalRecording();
    void _stopLocalRecording();
    void _resetLocalRecordingState();
    void _finishLocalRecording(const QString &outputFile);
    bool _receiverStreaming() const;
    bool _receiverDecoding() const;
    bool _receiverRecording() const;

    GimbalControlSettings *_settings = nullptr;
    RoukePayloadController _controller;
    RoukeGimbalController _gimbalController;
    QTimer _localPhotoTimer;
    QTimer _localRecordingStartTimer;
    QTimer _localRecordingStopTimer;
    QThreadPool _localPhotoSaveThreadPool;
    bool _lastEnabled = false;
    bool _recordingSessionRequested = false;
    int _localPhotoCount = 0;
    QSize _negotiatedPulledVideoSize;
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
    QString _localMediaError;
    bool _shuttingDown = false;
};
