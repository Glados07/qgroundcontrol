/****************************************************************************
 *
 * Independent video manager for the UniPod MT11 stream.
 *
 ****************************************************************************/

#pragma once

#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QSize>
#include <QtCore/QString>
#include <QtCore/QTimer>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>

#include <cstdint>

class GimbalControlSettings;
class VideoReceiver;

class DualVideoManager : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool enabled READ enabled NOTIFY enabledChanged)
    Q_PROPERTY(bool hasVideo READ hasVideo NOTIFY hasVideoChanged)
    Q_PROPERTY(bool initialized READ initialized NOTIFY initializedChanged)
    Q_PROPERTY(bool streaming READ streaming NOTIFY streamingChanged)
    Q_PROPERTY(bool decoding READ decoding NOTIFY decodingChanged)
    Q_PROPERTY(bool fullScreen READ fullScreen WRITE setFullScreen NOTIFY fullScreenChanged)
    Q_PROPERTY(double aspectRatio READ aspectRatio NOTIFY videoSizeChanged)
    Q_PROPERTY(QSize videoSize READ videoSize NOTIFY videoSizeChanged)
    Q_PROPERTY(QObject *videoReceiver READ videoReceiverObject NOTIFY videoReceiverChanged)
    Q_PROPERTY(QQuickItem *videoItem READ videoItem NOTIFY videoItemChanged)

public:
    explicit DualVideoManager(GimbalControlSettings *settings, QObject *parent = nullptr);
    ~DualVideoManager() override;

    bool enabled() const { return _enabled; }
    bool hasVideo() const { return _enabled && !_uri.isEmpty(); }
    bool initialized() const { return !_receiver.isNull(); }
    bool streaming() const { return _streaming; }
    bool decoding() const { return _decoding; }
    bool fullScreen() const { return _fullScreen; }
    double aspectRatio() const;
    QSize videoSize() const { return _videoSize; }

    VideoReceiver *videoReceiver() const;
    QObject *videoReceiverObject() const;
    QQuickItem *videoItem() const { return _videoItem.data(); }

    Q_INVOKABLE void init(QQuickWindow *window);
    Q_INVOKABLE void startVideo();
    Q_INVOKABLE void stopVideo();
    Q_INVOKABLE void cleanup();

    void setFullScreen(bool fullScreen);

signals:
    void enabledChanged();
    void hasVideoChanged();
    void initializedChanged();
    void videoObjectsAboutToBeReleased();
    void videoObjectsReleased();
    void streamingChanged();
    void decodingChanged();
    void fullScreenChanged();
    void videoSizeChanged();
    void videoReceiverChanged();
    void videoItemChanged();

private:
    void _refreshSettings();
    void _ensureReceiver();
    void _applyDesiredState();
    void _requestStop();
    void _scheduleRestart();
    void _releaseReceiver();
    uint32_t _rtspTimeout() const;

    GimbalControlSettings *_settings = nullptr;
    QPointer<QQuickWindow> _window;
    QPointer<VideoReceiver> _receiver;
    QPointer<QQuickItem> _videoItem;
    QTimer _restartTimer;
    QString _uri;
    QSize _videoSize;
    bool _enabled = false;
    bool _streaming = false;
    bool _decoding = false;
    bool _fullScreen = false;
    bool _paused = false;
    bool _starting = false;
    bool _stopping = false;
    bool _restartRequested = false;
    bool _releaseAfterStop = false;
};
