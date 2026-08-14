/****************************************************************************
 *
 * Independent manager for the second configured RTSP stream.
 *
 ****************************************************************************/

#include "DualVideoManager.h"

#include "Fact.h"
#include "QGCCorePlugin.h"
#include "QGCLoggingCategory.h"
#include "Settings/VideoCustomSettings.h"
#include "SettingsManager.h"
#include "VideoManager/VideoReceiver/VideoReceiver.h"
#include "VideoSettings.h"

#include <QtCore/QVariant>
#include <QtCore/QRunnable>

QGC_LOGGING_CATEGORY(DualVideoManagerLog, "gcs.custom.videomanager.dualvideo")

namespace {
constexpr const char *kSecondaryVideoObjectName = "secondaryVideoContent";
constexpr int kRestartDelayMs = 1000;
constexpr uint32_t kFallbackRtspTimeoutSeconds = 5;

class FinishSecondaryVideoInitialization final : public QRunnable
{
public:
    explicit FinishSecondaryVideoInitialization(DualVideoManager *manager)
        : _manager(manager)
    {
    }

    void run() final
    {
        if (_manager) {
            QMetaObject::invokeMethod(
                _manager.data(),
                "_finishRenderInitialization",
                Qt::QueuedConnection);
        }
    }

private:
    QPointer<DualVideoManager> _manager;
};
}

DualVideoManager::DualVideoManager(VideoCustomSettings *settings, QObject *parent)
    : QObject(parent)
    , _settings(settings)
{
    _restartTimer.setSingleShot(true);
    _restartTimer.setInterval(kRestartDelayMs);
    connect(&_restartTimer, &QTimer::timeout, this, &DualVideoManager::_applyDesiredState);

    if (_settings) {
        connect(_settings->secondaryRtspUrl(),
                &Fact::rawValueChanged,
                this,
                [this](const QVariant &) { _refreshSettings(); });
    }

    if (VideoSettings *const videoSettings = SettingsManager::instance()->videoSettings()) {
        connect(videoSettings->videoSource(),
                &Fact::rawValueChanged,
                this,
                [this](const QVariant &) { _refreshSettings(); });
        connect(videoSettings->rtspUrl(),
                &Fact::rawValueChanged,
                this,
                [this](const QVariant &) { _refreshSettings(); });
        connect(videoSettings->streamEnabled(),
                &Fact::rawValueChanged,
                this,
                [this](const QVariant &) { _refreshSettings(); });
    }

    _refreshSettings();
}

DualVideoManager::~DualVideoManager()
{
    cleanup();
}

QObject *DualVideoManager::videoReceiverObject() const
{
    return _receiver.data();
}

VideoReceiver *DualVideoManager::videoReceiver() const
{
    return _receiver.data();
}

double DualVideoManager::aspectRatio() const
{
    if (_videoSize.isValid() && (_videoSize.height() > 0)) {
        return static_cast<double>(_videoSize.width()) / static_cast<double>(_videoSize.height());
    }

    VideoSettings *const videoSettings = SettingsManager::instance()->videoSettings();
    return videoSettings ? videoSettings->aspectRatio()->rawValue().toDouble() : (16.0 / 9.0);
}

void DualVideoManager::init(QQuickWindow *window)
{
    if (!window) {
        qCWarning(DualVideoManagerLog) << "Cannot initialize the secondary video without a QQuickWindow";
        return;
    }

    _window = window;
    _ensureReceiver();
    _applyDesiredState();
}

void DualVideoManager::startVideo()
{
    _paused = false;
    _ensureReceiver();
    _applyDesiredState();
}

void DualVideoManager::stopVideo()
{
    _paused = true;
    _restartTimer.stop();
    _applyDesiredState();
}

void DualVideoManager::cleanup()
{
    _restartTimer.stop();
    _paused = true;
    _starting = false;
    _stopping = false;
    _restartRequested = false;

    if (_fullScreen) {
        _fullScreen = false;
        emit fullScreenChanged();
    }

    _releaseAfterStop = false;
    _releaseReceiver();
}

void DualVideoManager::setFullScreen(bool fullScreen)
{
    if (!hasVideo()) {
        fullScreen = false;
    }

    if (_fullScreen == fullScreen) {
        return;
    }

    _fullScreen = fullScreen;
    emit fullScreenChanged();
}

void DualVideoManager::_refreshSettings()
{
    const bool oldHasVideo = hasVideo();
    VideoSettings *const videoSettings = SettingsManager::instance()->videoSettings();
    const bool primaryUsesRtsp = videoSettings
        && videoSettings->videoSource()->rawValue().toString()
               == QString::fromLatin1(VideoSettings::videoSourceRTSP);
    const bool enabled = _settings && videoSettings && primaryUsesRtsp
        && videoSettings->streamEnabled()->rawValue().toBool();
    const QString uri = _settings
        ? _settings->secondaryRtspUrl()->rawValue().toString().trimmed()
        : QString();
    const QString primaryUri = (videoSettings && primaryUsesRtsp)
        ? videoSettings->rtspUrl()->rawValue().toString().trimmed()
        : QString();
    const bool duplicateSource = !uri.isEmpty() && !primaryUri.isEmpty()
        && (uri == primaryUri);

    const bool enabledChanged = (_enabled != enabled);
    const bool duplicateChanged = (_duplicateSource != duplicateSource);
    _enabled = enabled;
    _uri = uri;
    _duplicateSource = duplicateSource;

    if (duplicateChanged && _duplicateSource) {
        qCWarning(DualVideoManagerLog)
            << "Secondary RTSP URL matches the primary URL; the duplicate receiver is disabled"
            << _uri;
    }

    if (!hasVideo()) {
        setFullScreen(false);
    }

    if (enabledChanged) {
        emit DualVideoManager::enabledChanged();
    }
    if (duplicateChanged) {
        emit duplicateSourceChanged();
    }
    if (oldHasVideo != hasVideo()) {
        emit hasVideoChanged();
    }
    if (hasVideo()) {
        _ensureReceiver();
    } else if (_receiver) {
        _releaseAfterStop = true;
    }
    _applyDesiredState();
}

void DualVideoManager::_ensureReceiver()
{
    if (_receiver || !hasVideo() || !_window) {
        return;
    }

    QQuickItem *const videoItem = _window->findChild<QQuickItem *>(
        QString::fromLatin1(kSecondaryVideoObjectName));
    if (!videoItem) {
        // The secondary video surface does not instantiate an OpenGL video item
        // while the URL is empty. Its Loader calls init again once the item is
        // available, so absence here is an expected ordering condition.
        return;
    }

    VideoReceiver *const receiver = QGCCorePlugin::instance()->createVideoReceiver(this);
    if (!receiver) {
        qCCritical(DualVideoManagerLog) << "Failed to create the secondary VideoReceiver";
        return;
    }

    // The QtMultimedia factory currently ignores its parent argument, while
    // the GStreamer factory does not. Normalize ownership so CustomPlugin can
    // classify this as the secondary receiver on either backend and cleanup remains
    // deterministic.
    if (receiver->parent() != this) {
        receiver->setParent(this);
    }

    receiver->setName(QString::fromLatin1(kSecondaryVideoObjectName));
    receiver->setWidget(videoItem);
    receiver->setUri(_uri);

    if (VideoSettings *const videoSettings = SettingsManager::instance()->videoSettings()) {
        receiver->setLowLatency(videoSettings->lowLatencyMode()->rawValue().toBool());
    }

    void *const sink = QGCCorePlugin::instance()->createVideoSink(videoItem, receiver);
    if (!sink) {
        qCCritical(DualVideoManagerLog) << "Failed to create the secondary video sink";
        delete receiver;
        return;
    }
    receiver->setSink(sink);

    _receiver = receiver;
    _videoItem = videoItem;
    _renderReady = false;

    const QPointer<DualVideoManager> guardedThis(this);
    const QPointer<VideoReceiver> guardedReceiver(receiver);

    connect(receiver,
            &VideoReceiver::onStartComplete,
            this,
            [guardedThis, guardedReceiver](VideoReceiver::STATUS status) {
                if (!guardedThis || !guardedReceiver) {
                    return;
                }

                guardedThis->_starting = false;
                qCInfo(DualVideoManagerLog)
                    << "Secondary video start completed" << guardedReceiver->uri()
                    << "status" << status;
                if (status == VideoReceiver::STATUS_OK) {
                    guardedReceiver->setStarted(true);
                    if (guardedReceiver->sink()) {
                        guardedReceiver->startDecoding(guardedReceiver->sink());
                    }
                    if (guardedThis->_restartRequested) {
                        guardedThis->_requestStop();
                        return;
                    }
                    guardedThis->_applyDesiredState();
                } else if (guardedThis->_releaseAfterStop) {
                    guardedThis->_restartRequested = false;
                    guardedThis->_releaseAfterStop = false;
                    QTimer::singleShot(
                        0,
                        guardedThis.data(),
                        [guardedThis]() {
                            if (guardedThis) {
                                guardedThis->_releaseReceiver();
                            }
                        });
                } else if (guardedThis->_restartRequested
                           || status != VideoReceiver::STATUS_INVALID_URL) {
                    guardedThis->_restartRequested = false;
                    guardedThis->_scheduleRestart();
                }
            });

    connect(receiver,
            &VideoReceiver::onStopComplete,
            this,
            [guardedThis, guardedReceiver](VideoReceiver::STATUS status) {
                if (!guardedThis || !guardedReceiver) {
                    return;
                }

                guardedReceiver->setStarted(false);
                guardedThis->_starting = false;
                guardedThis->_stopping = false;
                const bool restartRequested = guardedThis->_restartRequested;
                guardedThis->_restartRequested = false;
                if (guardedThis->_releaseAfterStop) {
                    guardedThis->_releaseAfterStop = false;
                    QTimer::singleShot(
                        0,
                        guardedThis.data(),
                        [guardedThis]() {
                            if (guardedThis) {
                                guardedThis->_releaseReceiver();
                                if (guardedThis->hasVideo()
                                        && !guardedThis->_paused) {
                                    guardedThis->_ensureReceiver();
                                    guardedThis->_applyDesiredState();
                                }
                            }
                        });
                    return;
                }
                if (restartRequested
                    || status != VideoReceiver::STATUS_INVALID_URL) {
                    guardedThis->_scheduleRestart();
                }
            });

    connect(receiver,
            &VideoReceiver::timeout,
            this,
            [guardedThis]() {
                if (guardedThis) {
                    // GstVideoReceiver emits timeout before stopping its own
                    // pipeline. onStopComplete serializes the restart.
                    guardedThis->_restartRequested = true;
                }
            });

    connect(receiver,
            &VideoReceiver::streamingChanged,
            this,
            [guardedThis](bool streaming) {
                if (!guardedThis || (guardedThis->_streaming == streaming)) {
                    return;
                }
                guardedThis->_streaming = streaming;
                qCInfo(DualVideoManagerLog)
                    << "Secondary video streaming" << guardedThis->_uri << streaming;
                emit guardedThis->streamingChanged();
            });

    connect(receiver,
            &VideoReceiver::decodingChanged,
            this,
            [guardedThis](bool decoding) {
                if (!guardedThis || (guardedThis->_decoding == decoding)) {
                    return;
                }
                guardedThis->_decoding = decoding;
                qCInfo(DualVideoManagerLog)
                    << "Secondary video decoding" << guardedThis->_uri << decoding;
                emit guardedThis->decodingChanged();
            });

    connect(receiver,
            &VideoReceiver::videoSizeChanged,
            this,
            [guardedThis](const QSize &videoSize) {
                if (!guardedThis || (guardedThis->_videoSize == videoSize)) {
                    return;
                }
                guardedThis->_videoSize = videoSize;
                qCInfo(DualVideoManagerLog)
                    << "Secondary video size" << guardedThis->_uri << videoSize;
                emit guardedThis->videoSizeChanged();
            });

    emit videoReceiverChanged();
    emit videoItemChanged();
    emit initializedChanged();

    // Match native VideoManager: enter READY only after the scene graph has
    // synchronized the dynamically loaded QGCVideoBackground item.
    _window->scheduleRenderJob(
        new FinishSecondaryVideoInitialization(this),
        QQuickWindow::BeforeSynchronizingStage);
}

void DualVideoManager::_finishRenderInitialization()
{
    if (!_receiver || !_videoItem) {
        return;
    }

    _renderReady = true;
    qCInfo(DualVideoManagerLog)
        << "Secondary video render item is ready"
        << _videoItem->property("itemInitialized").toBool();
    _applyDesiredState();
}

void DualVideoManager::_applyDesiredState()
{
    if (!_receiver || !_renderReady) {
        return;
    }

    const bool shouldRun = hasVideo() && !_paused;
    if (!shouldRun) {
        _restartTimer.stop();
        _requestStop();
        return;
    }

    if (_receiver->uri() != _uri) {
        if (_receiver->started() || _starting || _stopping) {
            _restartRequested = true;
            _requestStop();
            return;
        }
        _receiver->setUri(_uri);
    }

    if (_stopping || _starting || _receiver->started()) {
        return;
    }

    _restartTimer.stop();
    _starting = true;
    qCInfo(DualVideoManagerLog) << "Starting secondary video" << _uri;
    _receiver->start(_rtspTimeout());
}

void DualVideoManager::_requestStop()
{
    if (!_receiver || _stopping) {
        return;
    }

    if (!_receiver->started() && !_starting) {
        if (_releaseAfterStop) {
            _releaseAfterStop = false;
            _releaseReceiver();
        }
        return;
    }

    if (!_receiver->started() && _starting) {
        // start() is asynchronous. Requesting stop here may race ahead of the
        // worker-side pipeline creation and report completion too early; let
        // onStartComplete observe the new desired state and stop immediately.
        _releaseAfterStop = _releaseAfterStop || !hasVideo();
        return;
    }

    _stopping = true;
    _receiver->stop();
}

void DualVideoManager::_releaseReceiver()
{
    VideoReceiver *const receiver = _receiver.data();
    if (!receiver) {
        return;
    }

    emit videoObjectsAboutToBeReleased();

    void *const sink = receiver->sink();
    receiver->disconnect(this);
    _receiver.clear();
    _videoItem.clear();
    _renderReady = false;
    _restartRequested = false;

    // GstVideoReceiver's destructor drains its worker before the final sink
    // reference is released. The QML Loader remains active until the
    // initializedChanged signal emitted below, so the sink never outlives its
    // rendering item.
    delete receiver;
    if (sink) {
        QGCCorePlugin::instance()->releaseVideoSink(sink);
    }
    emit videoObjectsReleased();

    const bool wasStreaming = _streaming;
    const bool wasDecoding = _decoding;
    _streaming = false;
    _decoding = false;
    _videoSize = QSize();

    emit videoReceiverChanged();
    emit videoItemChanged();
    emit initializedChanged();
    if (wasStreaming) {
        emit streamingChanged();
    }
    if (wasDecoding) {
        emit decodingChanged();
    }
    emit videoSizeChanged();
}

void DualVideoManager::_scheduleRestart()
{
    if (hasVideo() && !_paused && !_stopping) {
        _restartTimer.start();
    }
}

uint32_t DualVideoManager::_rtspTimeout() const
{
    VideoSettings *const videoSettings = SettingsManager::instance()->videoSettings();
    if (!videoSettings) {
        return kFallbackRtspTimeoutSeconds;
    }

    return videoSettings->rtspTimeout()->rawValue().toUInt();
}
