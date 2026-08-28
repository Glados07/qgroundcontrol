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
#include "VideoManager/VideoReceiver/GStreamer/AndroidH265DecoderFallback.h"
#include "VideoManager/VideoReceiver/GStreamer/AndroidH265HardwareDecoderAdapter.h"
#include "VideoManager/VideoReceiver/VideoReceiver.h"
#include "VideoSettings.h"

#include <QtCore/QVariant>
#include <QtCore/QRunnable>
#include <QtCore/QUrl>

#include <algorithm>
#include <limits>

QGC_LOGGING_CATEGORY(DualVideoManagerLog, "gcs.custom.videomanager.dualvideo")

namespace {
constexpr const char *kSecondaryVideoObjectName = "secondaryVideoContent";
constexpr int kRestartDelayMs = 1000;
constexpr int kMaximumRestartDelayMs = 15000;
constexpr int kPrimaryActiveUriClearDelayMs = 1000;
#if defined(QGC_GST_STREAMING)
constexpr quint64 kCoreMinimumRtspTimeoutSeconds = 8;
constexpr quint64 kDecoderRecoveryMarginMs = 3000;
#else
constexpr int kMinimumDecodeStartupTimeoutMs = 10000;
constexpr int kMaximumDecodeStartupTimeoutMs = 30000;
#endif
constexpr uint32_t kFallbackRtspTimeoutSeconds = 5;

bool sameStreamUri(const QString &left, const QString &right)
{
    const QString trimmedLeft = left.trimmed();
    const QString trimmedRight = right.trimmed();
    if (trimmedLeft.isEmpty() || trimmedRight.isEmpty()) {
        return false;
    }
    if (trimmedLeft == trimmedRight) {
        return true;
    }

    const QUrl leftUrl(trimmedLeft, QUrl::StrictMode);
    const QUrl rightUrl(trimmedRight, QUrl::StrictMode);
    return leftUrl.isValid() && rightUrl.isValid() && leftUrl == rightUrl;
}

bool isAdapterFactoryForRoute(VideoReceiver *receiver,
                              const QString &uri,
                              const QString &factory)
{
    const QString adapterFactory =
        AndroidH265DecoderFallback::activeAdapterFactoryName(receiver, uri);
    if (adapterFactory.isEmpty()) {
        return false;
    }

    return AndroidH265HardwareDecoderAdapter::adapterRouteContainsFactory(
        adapterFactory, factory);
}

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

    _decodeStartupTimer.setSingleShot(true);
    connect(&_decodeStartupTimer,
            &QTimer::timeout,
            this,
            &DualVideoManager::_handleDecodeStartupTimeout);

    _primaryActiveUriClearTimer.setSingleShot(true);
    _primaryActiveUriClearTimer.setTimerType(Qt::PreciseTimer);
    _primaryActiveUriClearTimer.setInterval(kPrimaryActiveUriClearDelayMs);
    connect(&_primaryActiveUriClearTimer,
            &QTimer::timeout,
            this,
            [this]() {
                if (_cleaningUp || _primaryReleasingUris.isEmpty()) {
                    return;
                }

                _primaryReleasingUris.clear();
                _refreshSettings();
            });

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
    _primaryActiveUriClearTimer.stop();
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

void DualVideoManager::setPrimaryVideoReceiver(VideoReceiver *receiver)
{
    if (_cleaningUp) {
        return;
    }

    if (_primaryVideoReceiver == receiver) {
        if (receiver && receiver->started() && _primaryActiveUri.isEmpty()) {
            _recordPrimaryActiveUri(receiver->uri());
            return;
        }
        _refreshSettings();
        return;
    }

    if (_primaryVideoReceiver) {
        _schedulePrimaryActiveUriClear();
    }

    disconnect(_primaryVideoUriConnection);
    disconnect(_primaryVideoStartAttemptConnection);
    disconnect(_primaryVideoStartConnection);
    disconnect(_primaryVideoStopConnection);
    disconnect(_primaryVideoDestroyedConnection);
    _primaryVideoUriConnection = {};
    _primaryVideoStartAttemptConnection = {};
    _primaryVideoStartConnection = {};
    _primaryVideoStopConnection = {};
    _primaryVideoDestroyedConnection = {};
    _primaryVideoReceiver = receiver;
    _primaryStartAttemptUri.clear();

    if (receiver) {
        const QPointer<VideoReceiver> guardedReceiver(receiver);
        _primaryVideoUriConnection =
            connect(receiver,
                    &VideoReceiver::uriChanged,
                    this,
                    [this, guardedReceiver](const QString &) {
                        if (!guardedReceiver
                            || _primaryVideoReceiver != guardedReceiver) {
                            return;
                        }
                        _refreshSettings();
                    });
        _primaryVideoStartAttemptConnection =
            connect(receiver,
                    &VideoReceiver::onStartAttempt,
                    this,
                    [this, guardedReceiver](const QString &uri) {
                        if (_cleaningUp
                            || !guardedReceiver
                            || _primaryVideoReceiver != guardedReceiver) {
                            return;
                        }

                        _primaryStartAttemptUri = uri.trimmed();
                        _refreshSettings();
                    });
        _primaryVideoStartConnection =
            connect(receiver,
                    &VideoReceiver::onStartComplete,
                    this,
                    [this, guardedReceiver](VideoReceiver::STATUS status) {
                        if (_cleaningUp
                            || !guardedReceiver
                            || _primaryVideoReceiver != guardedReceiver) {
                            return;
                        }

                        const QString attemptedUri = _primaryStartAttemptUri;
                        _primaryStartAttemptUri.clear();
                        if (status == VideoReceiver::STATUS_OK) {
                            _recordPrimaryActiveUri(
                                attemptedUri.isEmpty()
                                    ? guardedReceiver->uri()
                                    : attemptedUri);
                        } else {
                            _refreshSettings();
                        }
                    });
        _primaryVideoStopConnection =
            connect(receiver,
                    &VideoReceiver::onStopComplete,
                    this,
                    [this, guardedReceiver](VideoReceiver::STATUS) {
                        if (_cleaningUp
                            || !guardedReceiver
                            || _primaryVideoReceiver != guardedReceiver) {
                            return;
                        }

                        _schedulePrimaryActiveUriClear();
                    });
        _primaryVideoDestroyedConnection =
            connect(receiver,
                    &QObject::destroyed,
                    this,
                    [this]() {
                        _primaryVideoReceiver = nullptr;
                        _primaryVideoUriConnection = {};
                        _primaryVideoStartAttemptConnection = {};
                        _primaryVideoStartConnection = {};
                        _primaryVideoStopConnection = {};
                        _primaryVideoDestroyedConnection = {};
                        _primaryStartAttemptUri.clear();
                        if (!_cleaningUp) {
                            _schedulePrimaryActiveUriClear();
                            _refreshSettings();
                        }
                    });

        if (receiver->started()) {
            _recordPrimaryActiveUri(receiver->uri());
            return;
        }
    }

    _refreshSettings();
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

void DualVideoManager::initVideoItem(QQuickWindow *window, QQuickItem *videoItem)
{
    if (!window || !videoItem) {
        qCWarning(DualVideoManagerLog)
            << "Cannot initialize the secondary video without a window and render item";
        return;
    }

    _window = window;
    _requestedVideoItem = videoItem;

    if (_receiver && (_videoItem != videoItem)) {
        _restartTimer.stop();
        _decodeStartupTimer.stop();
        _renderReady = false;
        _receiver->setWidget(nullptr);
        _releaseAfterStop = true;
        _requestStop();
        if (_receiver) {
            return;
        }
    }

    _ensureReceiver();
    if (_receiver && (_videoItem == videoItem)) {
        _scheduleRenderInitialization(videoItem->window()
                                          ? videoItem->window()
                                          : window);
    }
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
    _cleaningUp = true;
    _restartTimer.stop();
    _decodeStartupTimer.stop();
    _primaryActiveUriClearTimer.stop();
    _paused = true;
    _starting = false;
    _stopping = false;
    _restartRequested = false;
    _primaryStartAttemptUri.clear();
    _primaryActiveUri.clear();
    _primaryReleasingUris.clear();

    disconnect(_primaryVideoUriConnection);
    disconnect(_primaryVideoStartAttemptConnection);
    disconnect(_primaryVideoStartConnection);
    disconnect(_primaryVideoStopConnection);
    disconnect(_primaryVideoDestroyedConnection);
    _primaryVideoUriConnection = {};
    _primaryVideoStartAttemptConnection = {};
    _primaryVideoStartConnection = {};
    _primaryVideoStopConnection = {};
    _primaryVideoDestroyedConnection = {};
    _primaryVideoReceiver = nullptr;

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
    if (_cleaningUp) {
        return;
    }

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
    const QString configuredPrimaryUri = (videoSettings && primaryUsesRtsp)
        ? videoSettings->rtspUrl()->rawValue().toString().trimmed()
        : QString();
    const QString effectivePrimaryUri = _primaryVideoReceiver
        ? _primaryVideoReceiver->uri().trimmed()
        : QString();
    const bool releasingPrimarySource = std::any_of(
        _primaryReleasingUris.cbegin(),
        _primaryReleasingUris.cend(),
        [&uri](const QString &releasingUri) {
            return sameStreamUri(uri, releasingUri);
        });
    const bool duplicateSource = sameStreamUri(uri, configuredPrimaryUri)
        || sameStreamUri(uri, effectivePrimaryUri)
        || sameStreamUri(uri, _primaryStartAttemptUri)
        || sameStreamUri(uri, _primaryActiveUri)
        || releasingPrimarySource;
    const bool enabledChanged = (_enabled != enabled);
    const bool uriChanged = (_uri != uri);
    const bool duplicateChanged = (_duplicateSource != duplicateSource);
    _enabled = enabled;
    _uri = uri;
    _duplicateSource = duplicateSource;

    if (uriChanged || enabledChanged || duplicateChanged) {
        _consecutiveDecodeFailures = 0;
    }

    // A user-visible configuration change represents a new connection
    // attempt and must not inherit a retry delay from the previous
    // configuration. Passive primary-receiver state notifications also call
    // _refreshSettings(), but do not change any of these values; they must not
    // cancel or bypass an already scheduled retry for the secondary stream.
    if (uriChanged || enabledChanged || duplicateChanged) {
        _restartTimer.stop();
    }

    if (duplicateChanged && _duplicateSource) {
        qCWarning(DualVideoManagerLog)
            << "Secondary RTSP URL matches a configured, current, starting,"
               " active, or releasing primary URL;"
               " the duplicate receiver is disabled"
            << "secondary" << _uri
            << "configuredPrimary" << configuredPrimaryUri
            << "currentPrimary" << effectivePrimaryUri
            << "startingPrimary" << _primaryStartAttemptUri
            << "activePrimary" << _primaryActiveUri
            << "releasingPrimary" << _primaryReleasingUris.values();
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
    if (_cleaningUp || _receiver || !hasVideo() || !_window) {
        return;
    }

    QQuickItem *videoItem = _requestedVideoItem.data();
    if (!videoItem) {
        videoItem = _window->findChild<QQuickItem *>(
            QString::fromLatin1(kSecondaryVideoObjectName));
    }
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
#if defined(Q_OS_ANDROID) && defined(QGC_GST_STREAMING)
    AndroidH265DecoderFallback::install(receiver);
#endif

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

    // QGCVideoBackground is created by a Loader. On Android its qml6gl item
    // initializes the OpenGL context asynchronously on the render thread. Do
    // not start the receiver merely because our own render job ran: wait for
    // the item's actual itemInitialized state so qml6glsink can enter READY.
    if (videoItem->metaObject()->indexOfSignal("itemInitializedChanged()") >= 0) {
        _videoItemInitializedConnection =
            connect(videoItem,
                    SIGNAL(itemInitializedChanged()),
                    this,
                    SLOT(_finishRenderInitialization()),
                    Qt::QueuedConnection);
    }

    _videoItemWindowConnection =
        connect(videoItem,
                &QQuickItem::windowChanged,
                this,
                [guardedThis = QPointer<DualVideoManager>(this),
                 guardedReceiver = QPointer<VideoReceiver>(receiver),
                 guardedItem = QPointer<QQuickItem>(videoItem)](QQuickWindow *window) {
                    if (!guardedThis || !guardedReceiver
                        || guardedThis->_receiver != guardedReceiver
                        || guardedThis->_videoItem != guardedItem) {
                        return;
                    }

                    guardedThis->_renderReady = false;
                    guardedThis->_decodeStartupTimer.stop();
                    if (guardedReceiver->started() || guardedThis->_starting) {
                        guardedThis->_restartRequested = true;
                        guardedThis->_requestStop();
                    }
                    if (window) {
                        guardedThis->_scheduleRenderInitialization(window);
                    }
                });

    _videoItemDestroyedConnection =
        connect(videoItem,
                &QObject::destroyed,
                this,
                [guardedThis = QPointer<DualVideoManager>(this),
                 guardedReceiver = QPointer<VideoReceiver>(receiver)]() {
                    if (!guardedThis || !guardedReceiver
                        || guardedThis->_receiver != guardedReceiver) {
                        return;
                    }

                    guardedThis->_restartTimer.stop();
                    guardedThis->_renderReady = false;
                    guardedThis->_decodeStartupTimer.stop();
                    guardedReceiver->setWidget(nullptr);
                    guardedThis->_releaseAfterStop = true;
                    guardedThis->_requestStop();
                });

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
                if (status != VideoReceiver::STATUS_OK) {
                    qCWarning(DualVideoManagerLog)
                        << "Secondary video start failed"
                        << guardedReceiver->uri()
                        << "statusCode" << static_cast<int>(status);
                }
                if (status != VideoReceiver::STATUS_OK
                    && status != VideoReceiver::STATUS_INVALID_STATE) {
                    // A bus error raised synchronously by set_state() can be
                    // delivered after this failed start completion. The core
                    // has already discarded that pipeline and will not emit a
                    // stop completion, so retire its generation here to keep
                    // the stale error from latching _stopping forever.
                    guardedThis->_resetVideoPipelineGeneration();
                }
                if (status == VideoReceiver::STATUS_OK) {
                    guardedReceiver->setStarted(true);
                    if (guardedThis->_restartRequested
                        || !guardedThis->_renderReady
                        || !guardedThis->hasVideo()
                        || guardedThis->_paused) {
                        guardedThis->_requestStop();
                        return;
                    }
                    if (guardedReceiver->sink()) {
                        guardedReceiver->startDecoding(guardedReceiver->sink());
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
                                if (guardedThis->hasVideo()
                                    && !guardedThis->_paused) {
                                    guardedThis->_ensureReceiver();
                                    guardedThis->_applyDesiredState();
                                }
                            }
                        });
                } else if (guardedThis->_restartRequested
                           || status != VideoReceiver::STATUS_INVALID_URL) {
                    if (!guardedThis->_restartRequested
                        && status != VideoReceiver::STATUS_INVALID_URL) {
                        guardedThis->_consecutiveDecodeFailures = qMin(
                            guardedThis->_consecutiveDecodeFailures + 1,
                            4);
                    }
                    guardedThis->_restartRequested = false;
                    guardedThis->_scheduleRestart();
                }
            });

    connect(receiver,
            &VideoReceiver::onStartDecodingComplete,
            this,
            [guardedThis, guardedReceiver](VideoReceiver::STATUS status) {
                if (!guardedThis || !guardedReceiver
                    || guardedThis->_receiver != guardedReceiver) {
                    return;
                }

                if (status != VideoReceiver::STATUS_OK
                    && status != VideoReceiver::STATUS_INVALID_STATE) {
                    qCWarning(DualVideoManagerLog)
                        << "Secondary video decoding start failed"
                        << guardedReceiver->uri()
                        << "generation"
                        << guardedThis->_videoPipelineGeneration
                        << "statusCode" << static_cast<int>(status)
                        << "codec" << guardedThis->_videoCodec
                        << "decoderPlugin"
                        << guardedThis->_selectedDecoderPlugin
                        << "decoderFactory"
                        << guardedThis->_selectedDecoderFactory;
                }
                if (status == VideoReceiver::STATUS_OK
                    || status == VideoReceiver::STATUS_INVALID_STATE) {
                    guardedThis->_armDecodeStartupWatchdog();
                    return;
                }

                // A failed sink/decoder start otherwise leaves the receiver in
                // started=true, decoding=false forever. Serialize a complete
                // stop/restart so the next attempt gets a fresh Gst pipeline.
                guardedThis->_decodeStartupTimer.stop();
                const bool hardwareRouteAdvanced =
                    AndroidH265DecoderFallback::prepareHardwareRetry(
                        guardedReceiver.data(),
                        guardedThis->_videoPipelineUri,
                        guardedThis->_videoPipelineGeneration,
                        guardedThis->_videoCodec,
                        guardedThis->_adapterSelected,
                        guardedThis->_sourceFrameReceived,
                        true,
                        guardedThis->_decoderFrameReceived,
                        guardedThis->_sinkFrameReceived,
                        "secondary decoder or video sink failed to start");
                qCWarning(DualVideoManagerLog)
                    << "Secondary decoding branch will be rebuilt without changing decoder ranks"
                    << guardedReceiver->uri()
                    << "generation"
                    << guardedThis->_videoPipelineGeneration
                    << "codec" << guardedThis->_videoCodec
                    << "decoderPlugin"
                    << guardedThis->_selectedDecoderPlugin
                    << "decoderFactory"
                    << guardedThis->_selectedDecoderFactory
                    << "sourceFrame"
                    << guardedThis->_sourceFrameReceived
                    << "decoderFrame"
                    << guardedThis->_decoderFrameReceived
                    << "sinkFrame" << guardedThis->_sinkFrameReceived
                    << "hardwareRouteAdvanced" << hardwareRouteAdvanced;
                guardedThis->_consecutiveDecodeFailures = qMin(
                    guardedThis->_consecutiveDecodeFailures + 1,
                    4);
                guardedThis->_restartRequested = true;
                guardedThis->_requestStop();
            });

    connect(receiver,
            &VideoReceiver::onStopComplete,
            this,
            [guardedThis, guardedReceiver](VideoReceiver::STATUS status) {
                if (!guardedThis || !guardedReceiver) {
                    return;
                }

                guardedReceiver->setStarted(false);
                guardedThis->_decodeStartupTimer.stop();
                guardedThis->_starting = false;
                guardedThis->_stopping = false;
                guardedThis->_resetVideoPipelineGeneration();
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
                if (!restartRequested && guardedThis->hasVideo()
                    && !guardedThis->_paused
                    && status != VideoReceiver::STATUS_INVALID_URL) {
                    // Gst bus errors may stop the pipeline without first
                    // reporting a decoding-start failure. Count those
                    // unexpected stops so they use the same bounded backoff.
                    guardedThis->_consecutiveDecodeFailures = qMin(
                        guardedThis->_consecutiveDecodeFailures + 1,
                        4);
                }
                if (restartRequested
                    || status != VideoReceiver::STATUS_INVALID_URL) {
                    guardedThis->_scheduleRestart();
                }
            });

    connect(receiver,
            &VideoReceiver::videoPipelineGenerationStarted,
            this,
            [guardedThis, guardedReceiver](const QString &uri,
                                           quint64 generation) {
                if (!guardedThis || !guardedReceiver
                    || guardedThis->_receiver != guardedReceiver
                    || uri != guardedReceiver->uri()
                    || generation == 0
                    || generation <= guardedThis->_lastVideoPipelineGeneration) {
                    return;
                }

                guardedThis->_resetVideoPipelineGeneration();
                guardedThis->_videoPipelineUri = uri;
                guardedThis->_videoPipelineGeneration = generation;
                guardedThis->_lastVideoPipelineGeneration = generation;
                qCDebug(DualVideoManagerLog)
                    << "Secondary video pipeline generation started"
                    << uri << "generation" << generation;
            });

    connect(receiver,
            &VideoReceiver::sourceFrameReceived,
            this,
            [guardedThis, guardedReceiver](const QString &uri,
                                           quint64 generation,
                                           int codec) {
                if (!guardedThis || !guardedReceiver
                    || guardedThis->_receiver != guardedReceiver
                    || !guardedThis->_matchesVideoPipelineGeneration(
                        uri,
                        generation)) {
                    return;
                }

                guardedThis->_sourceFrameReceived = true;
                if (codec != VideoReceiver::VIDEO_CODEC_UNKNOWN) {
                    guardedThis->_videoCodec = codec;
                }
                guardedThis->_armDecodeStartupWatchdog();
            });

    connect(receiver,
            &VideoReceiver::videoDecoderSelected,
            this,
            [guardedThis, guardedReceiver](const QString &uri,
                                           quint64 generation,
                                           int codec,
                                           const QString &plugin,
                                           const QString &factory) {
                if (!guardedThis || !guardedReceiver
                    || guardedThis->_receiver != guardedReceiver
                    || !guardedThis->_matchesVideoPipelineGeneration(
                        uri,
                        generation)) {
                    return;
                }

                if (codec != VideoReceiver::VIDEO_CODEC_UNKNOWN) {
                    guardedThis->_videoCodec = codec;
                }
                guardedThis->_selectedDecoderPlugin = plugin;
                guardedThis->_selectedDecoderFactory = factory;
                const bool adapterSelected = isAdapterFactoryForRoute(
                    guardedReceiver.data(), uri, factory);
                guardedThis->_adapterSelected |= adapterSelected;
                if (adapterSelected
                    && guardedThis->_videoCodec
                        == VideoReceiver::VIDEO_CODEC_UNKNOWN) {
                    guardedThis->_videoCodec =
                        VideoReceiver::VIDEO_CODEC_H265;
                }
                guardedThis->_armDecodeStartupWatchdog();
            });

    connect(receiver,
            &VideoReceiver::decoderFrameReceived,
            this,
            [guardedThis, guardedReceiver](const QString &uri,
                                           quint64 generation,
                                           const QString &plugin,
                                           const QString &factory) {
                if (!guardedThis || !guardedReceiver
                    || guardedThis->_receiver != guardedReceiver
                    || !guardedThis->_matchesVideoPipelineGeneration(
                        uri,
                        generation)) {
                    return;
                }

                guardedThis->_decoderFrameReceived = true;
                if (!plugin.isEmpty()) {
                    guardedThis->_selectedDecoderPlugin = plugin;
                }
                if (!factory.isEmpty()) {
                    guardedThis->_selectedDecoderFactory = factory;
                }
            });

    connect(receiver,
            &VideoReceiver::sinkFrameReceived,
            this,
            [guardedThis, guardedReceiver](const QString &uri,
                                           quint64 generation) {
                if (!guardedThis || !guardedReceiver
                    || guardedThis->_receiver != guardedReceiver
                    || !guardedThis->_matchesVideoPipelineGeneration(
                        uri,
                        generation)) {
                    return;
                }

                guardedThis->_sinkFrameReceived = true;
                guardedThis->_decodeStartupTimer.stop();
                guardedThis->_consecutiveDecodeFailures = 0;
            });

    connect(receiver,
            &VideoReceiver::onVideoPipelineError,
            this,
            [guardedThis, guardedReceiver](const QString &uri,
                                           quint64 generation,
                                           const QString &errorPlugin,
                                           const QString &errorFactory,
                                           bool rtspSourceError,
                                           bool decoderBranchError,
                                           int codec,
                                           const QString &decoderPlugin,
                                           const QString &decoderFactory,
                                           bool sourceFrameReceived,
                                           bool decoderFrameReceived,
                                           bool sinkFrameReceived) {
                if (!guardedThis || !guardedReceiver
                    || guardedThis->_receiver != guardedReceiver
                    || !guardedThis->_matchesVideoPipelineGeneration(
                        uri,
                        generation)) {
                    return;
                }

                guardedThis->_sourceFrameReceived |= sourceFrameReceived;
                guardedThis->_decoderFrameReceived |= decoderFrameReceived;
                guardedThis->_sinkFrameReceived |= sinkFrameReceived;
                if (codec != VideoReceiver::VIDEO_CODEC_UNKNOWN) {
                    guardedThis->_videoCodec = codec;
                }
                if (!decoderPlugin.isEmpty()) {
                    guardedThis->_selectedDecoderPlugin = decoderPlugin;
                }
                if (!decoderFactory.isEmpty()) {
                    guardedThis->_selectedDecoderFactory = decoderFactory;
                }
                guardedThis->_adapterSelected |= isAdapterFactoryForRoute(
                                                     guardedReceiver.data(),
                                                     uri,
                                                     decoderFactory)
                    || isAdapterFactoryForRoute(guardedReceiver.data(),
                                                uri,
                                                errorFactory);
                if (guardedThis->_adapterSelected
                    && guardedThis->_videoCodec
                        == VideoReceiver::VIDEO_CODEC_UNKNOWN) {
                    guardedThis->_videoCodec =
                        VideoReceiver::VIDEO_CODEC_H265;
                }

                guardedThis->_decodeStartupTimer.stop();
                if (guardedThis->_stopping) {
                    return;
                }

                const bool hardwareRouteAdvanced =
                    !rtspSourceError && decoderBranchError
                    && AndroidH265DecoderFallback::prepareHardwareRetry(
                        guardedReceiver.data(),
                        uri,
                        generation,
                        guardedThis->_videoCodec,
                        guardedThis->_adapterSelected,
                        guardedThis->_sourceFrameReceived,
                        true,
                        guardedThis->_decoderFrameReceived,
                        guardedThis->_sinkFrameReceived,
                        "H.265 adapter decoder-branch bus error");

                // The core receiver stops every current-generation bus error.
                // Freeze any proven next hardware route before that stop,
                // then block watchdog/start paths until completion.
                guardedThis->_stopping = true;
                qCWarning(DualVideoManagerLog)
                    << "Secondary video pipeline stopped; configured decoder ranks remain unchanged"
                    << uri << "generation" << generation
                    << "codec" << guardedThis->_videoCodec
                    << "decoderPlugin"
                    << guardedThis->_selectedDecoderPlugin
                    << "decoderFactory"
                    << guardedThis->_selectedDecoderFactory
                    << "errorPlugin" << errorPlugin
                    << "errorFactory" << errorFactory
                    << "rtspSourceError" << rtspSourceError
                    << "sourceFrame"
                    << guardedThis->_sourceFrameReceived
                    << "decoderFrame"
                    << guardedThis->_decoderFrameReceived
                    << "sinkFrame" << guardedThis->_sinkFrameReceived
                    << "decoderBranchError" << decoderBranchError
                    << "hardwareRouteAdvanced" << hardwareRouteAdvanced;
                // GstVideoReceiver stops itself after this signal. The
                // existing onStopComplete path applies bounded restart.
            });

    connect(receiver,
            &VideoReceiver::timeout,
            this,
            [guardedThis]() {
                if (guardedThis) {
                    // GstVideoReceiver emits timeout before stopping its own
                    // pipeline. onStopComplete serializes the restart.
                    guardedThis->_consecutiveDecodeFailures = qMin(
                        guardedThis->_consecutiveDecodeFailures + 1,
                        4);
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
                emit guardedThis->streamingChanged();
                if (streaming) {
                    guardedThis->_armDecodeStartupWatchdog();
                } else {
                    guardedThis->_decodeStartupTimer.stop();
                }
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
#if !defined(QGC_GST_STREAMING)
                if (decoding) {
                    guardedThis->_consecutiveDecodeFailures = 0;
                    guardedThis->_decodeStartupTimer.stop();
                } else {
                    guardedThis->_armDecodeStartupWatchdog();
                }
#endif
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

    // Match native VideoManager: enter READY only after the scene graph has
    // synchronized the dynamically loaded QGCVideoBackground item.
    _scheduleRenderInitialization(videoItem->window()
                                      ? videoItem->window()
                                      : _window.data());
}

void DualVideoManager::_finishRenderInitialization()
{
    if (!_receiver || !_videoItem || _renderReady) {
        return;
    }

    const QVariant itemInitialized = _videoItem->property("itemInitialized");
    if (itemInitialized.isValid() && !itemInitialized.toBool()) {
        return;
    }

    _renderReady = true;
    _applyDesiredState();
}

void DualVideoManager::_scheduleRenderInitialization(QQuickWindow *window)
{
    if (!window || !_receiver || !_videoItem) {
        return;
    }

    window->scheduleRenderJob(
        new FinishSecondaryVideoInitialization(this),
        QQuickWindow::BeforeSynchronizingStage);
}

void DualVideoManager::_armDecodeStartupWatchdog()
{
#if defined(QGC_GST_STREAMING)
    if (!_receiver || !_receiver->started() || !_streaming
        || _videoPipelineGeneration == 0 || !_sourceFrameReceived
        || _sinkFrameReceived || _stopping || _releaseAfterStop) {
        _decodeStartupTimer.stop();
        return;
    }

    const quint64 effectiveSourceTimeoutSeconds = qMax(
        static_cast<quint64>(_rtspTimeout()),
        kCoreMinimumRtspTimeoutSeconds);
    const quint64 requestedMs = effectiveSourceTimeoutSeconds * 1000u
        + kDecoderRecoveryMarginMs;
    const int timeoutMs = static_cast<int>(qMin(
        requestedMs,
        static_cast<quint64>(std::numeric_limits<int>::max())));
#else
    if (!_receiver || !_receiver->started() || !_streaming || _decoding
        || _stopping || _releaseAfterStop) {
        _decodeStartupTimer.stop();
        return;
    }

    const quint64 requestedMs = static_cast<quint64>(_rtspTimeout()) * 1000u;
    const int timeoutMs = static_cast<int>(qBound<quint64>(
        kMinimumDecodeStartupTimeoutMs,
        requestedMs,
        kMaximumDecodeStartupTimeoutMs));
#endif
    _decodeStartupTimer.start(timeoutMs);
}

void DualVideoManager::_resetVideoPipelineGeneration()
{
    _decodeStartupTimer.stop();
    _videoPipelineUri.clear();
    _selectedDecoderPlugin.clear();
    _selectedDecoderFactory.clear();
    _videoPipelineGeneration = 0;
    _videoCodec = VideoReceiver::VIDEO_CODEC_UNKNOWN;
    _sourceFrameReceived = false;
    _adapterSelected = false;
    _decoderFrameReceived = false;
    _sinkFrameReceived = false;
}

bool DualVideoManager::_matchesVideoPipelineGeneration(
    const QString &uri,
    quint64 generation) const
{
    return generation != 0 && generation == _videoPipelineGeneration
        && uri == _videoPipelineUri;
}

void DualVideoManager::_handleDecodeStartupTimeout()
{
#if defined(QGC_GST_STREAMING)
    if (!_receiver || !_receiver->started() || !_streaming
        || _videoPipelineGeneration == 0 || !_sourceFrameReceived
        || _sinkFrameReceived || _stopping || _releaseAfterStop) {
        return;
    }

    const bool hardwareRouteAdvanced =
        AndroidH265DecoderFallback::prepareHardwareRetry(
            _receiver.data(),
            _videoPipelineUri,
            _videoPipelineGeneration,
            _videoCodec,
            _adapterSelected,
            _sourceFrameReceived,
            false,
            _decoderFrameReceived,
            _sinkFrameReceived,
            "first decoded frame timeout");
    qCWarning(DualVideoManagerLog)
        << "Secondary RTSP source produced media but no sink frame; rebuilding the receiver"
        << _videoPipelineUri
        << "generation" << _videoPipelineGeneration
        << "codec" << _videoCodec
        << "decoderPlugin" << _selectedDecoderPlugin
        << "decoderFactory" << _selectedDecoderFactory
        << "sourceFrame" << _sourceFrameReceived
        << "decoderFrame" << _decoderFrameReceived
        << "sinkFrame" << _sinkFrameReceived
        << "hardwareRouteAdvanced" << hardwareRouteAdvanced;
#else
    if (!_receiver || !_receiver->started() || !_streaming || _decoding
        || _stopping || _releaseAfterStop) {
        return;
    }

    qCWarning(DualVideoManagerLog)
        << "Secondary RTSP is streaming but produced no decoded frame; restarting"
        << _uri;
#endif
    _consecutiveDecodeFailures = qMin(_consecutiveDecodeFailures + 1, 4);
    _restartRequested = true;
    _requestStop();
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

    // Rendering readiness only gates starting. A disabled/changed stream must
    // always be allowed to stop and release even while the GL item is pending.
    if (!_renderReady) {
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

    // _refreshSettings() is also invoked while the primary receiver moves
    // through its active/releasing handoff states. Those unrelated updates
    // previously started the secondary receiver immediately and silently
    // bypassed its 2/4/8/15 second RTSP backoff. Keep the current deadline;
    // the timer callback will enter this function again when it expires.
    if (_restartTimer.isActive()) {
        return;
    }

    _resetVideoPipelineGeneration();
    _starting = true;
    const uint32_t requestedTimeout = _rtspTimeout();
    _receiver->start(requestedTimeout);
}

void DualVideoManager::_requestStop()
{
    if (!_receiver || _stopping) {
        return;
    }

    if (!_receiver->started() && !_starting) {
        _resetVideoPipelineGeneration();
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
    _resetVideoPipelineGeneration();
    _receiver->stop();
}

void DualVideoManager::_releaseReceiver()
{
    VideoReceiver *const receiver = _receiver.data();
    if (!receiver) {
        _resetVideoPipelineGeneration();
        _lastVideoPipelineGeneration = 0;
        return;
    }

    emit videoObjectsAboutToBeReleased();

    _restartTimer.stop();
    _decodeStartupTimer.stop();
    if (_videoItemInitializedConnection) {
        disconnect(_videoItemInitializedConnection);
        _videoItemInitializedConnection = {};
    }
    if (_videoItemWindowConnection) {
        disconnect(_videoItemWindowConnection);
        _videoItemWindowConnection = {};
    }
    if (_videoItemDestroyedConnection) {
        disconnect(_videoItemDestroyedConnection);
        _videoItemDestroyedConnection = {};
    }

    void *const sink = receiver->sink();
    receiver->disconnect(this);
    _receiver.clear();
    _videoItem.clear();
    _renderReady = false;
    _restartRequested = false;
    _consecutiveDecodeFailures = 0;
    _resetVideoPipelineGeneration();
    _lastVideoPipelineGeneration = 0;

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
        const int delayMs = qMin(
            kMaximumRestartDelayMs,
            kRestartDelayMs << qMin(_consecutiveDecodeFailures, 4));
        _restartTimer.setInterval(delayMs);
        _restartTimer.start();
    }
}

void DualVideoManager::_recordPrimaryActiveUri(const QString &uri)
{
    if (_cleaningUp) {
        return;
    }

    const QString activeUri = uri.trimmed();
    if (activeUri.isEmpty()) {
        return;
    }

    for (auto it = _primaryReleasingUris.begin();
         it != _primaryReleasingUris.end();) {
        if (sameStreamUri(activeUri, *it)) {
            it = _primaryReleasingUris.erase(it);
        } else {
            ++it;
        }
    }
    if (_primaryReleasingUris.isEmpty()) {
        _primaryActiveUriClearTimer.stop();
    }

    if (_primaryActiveUri == activeUri) {
        _refreshSettings();
        return;
    }

    _primaryActiveUri = activeUri;
    _refreshSettings();
}

void DualVideoManager::_schedulePrimaryActiveUriClear()
{
    if (_cleaningUp) {
        _primaryActiveUri.clear();
        _primaryReleasingUris.clear();
        return;
    }

    if (_primaryActiveUri.isEmpty()) {
        return;
    }

    const QString releasingUri = _primaryActiveUri;
    _primaryActiveUri.clear();
    _primaryReleasingUris.insert(releasingUri);
    _primaryActiveUriClearTimer.start();
}

uint32_t DualVideoManager::_rtspTimeout() const
{
    VideoSettings *const videoSettings = SettingsManager::instance()->videoSettings();
    if (!videoSettings) {
        return kFallbackRtspTimeoutSeconds;
    }

    return videoSettings->rtspTimeout()->rawValue().toUInt();
}
