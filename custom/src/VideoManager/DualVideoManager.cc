/****************************************************************************
 *
 * Independent video manager for the UniPod MT11 stream.
 *
 ****************************************************************************/

#include "DualVideoManager.h"

#include "Fact.h"
#include "Gimbal/GimbalControlSettings.h"
#include "QGCCorePlugin.h"
#include "QGCLoggingCategory.h"
#include "SettingsManager.h"
#include "VideoManager/VideoReceiver/VideoReceiver.h"
#include "VideoSettings.h"

#include <QtCore/QVariant>

QGC_LOGGING_CATEGORY(DualVideoManagerLog, "gcs.custom.videomanager.dualvideo")

namespace {
constexpr const char *kMt11VideoObjectName = "mt11VideoContent";
constexpr int kRestartDelayMs = 1000;
constexpr uint32_t kFallbackRtspTimeoutSeconds = 5;
}

DualVideoManager::DualVideoManager(GimbalControlSettings *settings, QObject *parent)
    : QObject(parent)
    , _settings(settings)
{
    _restartTimer.setSingleShot(true);
    _restartTimer.setInterval(kRestartDelayMs);
    connect(&_restartTimer, &QTimer::timeout, this, &DualVideoManager::_applyDesiredState);

    if (_settings) {
        connect(_settings->mt11Enabled(),
                &Fact::rawValueChanged,
                this,
                [this](const QVariant &) { _refreshSettings(); });
        connect(_settings->mt11RtspUrl(),
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
        qCWarning(DualVideoManagerLog) << "Cannot initialize MT11 video without a QQuickWindow";
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
    const bool enabled = _settings && _settings->mt11Enabled()->rawValue().toBool();
    const QString uri = _settings
        ? _settings->mt11RtspUrl()->rawValue().toString().trimmed()
        : QString();

    const bool enabledChanged = (_enabled != enabled);
    _enabled = enabled;
    _uri = uri;

    if (!hasVideo()) {
        setFullScreen(false);
    }

    if (enabledChanged) {
        emit DualVideoManager::enabledChanged();
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
        QString::fromLatin1(kMt11VideoObjectName));
    if (!videoItem) {
        // MT11Video.qml deliberately does not instantiate an OpenGL video item
        // while MT11 is disabled. Its Loader calls init again once the item is
        // available, so absence here is an expected ordering condition.
        return;
    }

    VideoReceiver *const receiver = QGCCorePlugin::instance()->createVideoReceiver(this);
    if (!receiver) {
        qCCritical(DualVideoManagerLog) << "Failed to create the MT11 VideoReceiver";
        return;
    }

    // The QtMultimedia factory currently ignores its parent argument, while
    // the GStreamer factory does not. Normalize ownership so CustomPlugin can
    // classify this as the MT11 receiver on either backend and cleanup remains
    // deterministic.
    if (receiver->parent() != this) {
        receiver->setParent(this);
    }

    receiver->setName(QString::fromLatin1(kMt11VideoObjectName));
    receiver->setWidget(videoItem);
    receiver->setUri(_uri);

    if (VideoSettings *const videoSettings = SettingsManager::instance()->videoSettings()) {
        receiver->setLowLatency(videoSettings->lowLatencyMode()->rawValue().toBool());
    }

    void *const sink = QGCCorePlugin::instance()->createVideoSink(videoItem, receiver);
    if (!sink) {
        qCCritical(DualVideoManagerLog) << "Failed to create the MT11 video sink";
        delete receiver;
        return;
    }
    receiver->setSink(sink);

    _receiver = receiver;
    _videoItem = videoItem;

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
                    // A running receiver ignores another start request. Stop
                    // the timed-out pipeline first; onStopComplete schedules
                    // the bounded restart when the stream is still enabled.
                    if (guardedThis->_receiver
                        && (guardedThis->_receiver->started()
                            || guardedThis->_starting)) {
                        guardedThis->_restartRequested = true;
                        guardedThis->_requestStop();
                    } else {
                        guardedThis->_scheduleRestart();
                    }
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
                emit guardedThis->videoSizeChanged();
            });

    emit videoReceiverChanged();
    emit videoItemChanged();
    emit initializedChanged();
}

void DualVideoManager::_applyDesiredState()
{
    if (!_receiver) {
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
