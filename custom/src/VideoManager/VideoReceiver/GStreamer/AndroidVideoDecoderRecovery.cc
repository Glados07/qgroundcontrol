/****************************************************************************
 *
 * Android main-video first-frame recovery.
 *
 ****************************************************************************/

#include "AndroidVideoDecoderRecovery.h"

#include "AndroidH265DecoderFallback.h"
#include "AndroidH265HardwareDecoderAdapter.h"
#include "Fact.h"
#include "QGCLoggingCategory.h"
#include "SettingsManager.h"
#include "VideoManager/VideoReceiver/VideoReceiver.h"
#include "VideoSettings.h"

#include <QtCore/QUrl>

#include <limits>

QGC_LOGGING_CATEGORY(AndroidVideoDecoderRecoveryLog,
                     "gcs.custom.video.androidvideodecoderrecovery")

namespace {
constexpr const char *kRecoveryInstalledProperty =
    "customAndroidVideoDecoderRecoveryInstalled";
constexpr quint64 kCoreMinimumRtspTimeoutSeconds = 8;
constexpr quint64 kDecoderRecoveryMarginMs = 3000;

bool isRtspUri(const QString &uri)
{
    const QString scheme = QUrl(uri).scheme();
    return scheme.compare(QStringLiteral("rtsp"), Qt::CaseInsensitive) == 0
        || scheme.compare(QStringLiteral("rtsps"), Qt::CaseInsensitive) == 0;
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
}

void AndroidVideoDecoderRecovery::install(VideoReceiver *receiver)
{
#if defined(Q_OS_ANDROID) && defined(QGC_GST_STREAMING)
    if (!receiver
        || receiver->property(kRecoveryInstalledProperty).toBool()) {
        return;
    }

    receiver->setProperty(kRecoveryInstalledProperty, true);
    AndroidH265DecoderFallback::install(receiver);
    new AndroidVideoDecoderRecovery(receiver);
#else
    Q_UNUSED(receiver)
#endif
}

AndroidVideoDecoderRecovery::AndroidVideoDecoderRecovery(
    VideoReceiver *receiver)
    : QObject(receiver)
    , _receiver(receiver)
{
    _firstFrameTimer.setSingleShot(true);
    _firstFrameTimer.setTimerType(Qt::PreciseTimer);

    connect(&_firstFrameTimer,
            &QTimer::timeout,
            this,
            [this]() {
                if (!_streaming || !_sourceFrameReceived) {
                    return;
                }
                _restartAfterDecoderFailure(
                    "RTSP decoding produced no first frame",
                    false);
            });
    connect(receiver,
            &VideoReceiver::videoPipelineGenerationStarted,
            this,
            [this](const QString &uri, quint64 generation) {
                _handleGenerationStarted(uri, generation);
            });
    connect(receiver,
            &VideoReceiver::onStartComplete,
            this,
            [this](VideoReceiver::STATUS status) {
                _handleStartComplete(static_cast<int>(status));
            });
    connect(receiver,
            &VideoReceiver::onStartDecodingComplete,
            this,
            [this](VideoReceiver::STATUS status) {
                _handleStartDecodingComplete(static_cast<int>(status));
            });
    connect(receiver,
            &VideoReceiver::sourceFrameReceived,
            this,
            [this](const QString &uri,
                   quint64 generation,
                   int videoCodec) {
                _handleSourceFrameReceived(uri, generation, videoCodec);
            });
    connect(receiver,
            &VideoReceiver::videoDecoderSelected,
            this,
            [this](const QString &uri,
                   quint64 generation,
                   int videoCodec,
                   const QString &plugin,
                   const QString &factory) {
                _handleDecoderSelected(uri,
                                       generation,
                                       videoCodec,
                                       plugin,
                                       factory);
            });
    connect(receiver,
            &VideoReceiver::decoderFrameReceived,
            this,
            [this](const QString &uri,
                   quint64 generation,
                   const QString &plugin,
                   const QString &factory) {
                _handleDecoderFrameReceived(uri,
                                            generation,
                                            plugin,
                                            factory);
            });
    connect(receiver,
            &VideoReceiver::sinkFrameReceived,
            this,
            [this](const QString &uri, quint64 generation) {
                _handleSinkFrameReceived(uri, generation);
            });
    connect(receiver,
            &VideoReceiver::onVideoPipelineError,
            this,
            [this](const QString &uri,
                   quint64 generation,
                   const QString &errorPlugin,
                   const QString &errorFactory,
                   bool rtspSourceError,
                   bool decoderBranchError,
                   int videoCodec,
                   const QString &decoderPlugin,
                   const QString &decoderFactory,
                   bool sourceFrameReceived,
                   bool decoderFrameReceived,
                   bool sinkFrameReceived) {
                _handlePipelineError(uri,
                                     generation,
                                     errorPlugin,
                                     errorFactory,
                                     rtspSourceError,
                                     decoderBranchError,
                                     videoCodec,
                                     decoderPlugin,
                                     decoderFactory,
                                     sourceFrameReceived,
                                     decoderFrameReceived,
                                     sinkFrameReceived);
            });
    connect(receiver,
            &VideoReceiver::onStopComplete,
            this,
            [this](VideoReceiver::STATUS) { _handleStopComplete(); });
    connect(receiver,
            &VideoReceiver::streamingChanged,
            this,
            [this](bool streaming) {
                _streaming = streaming;
                if (streaming) {
                    _armFirstFrameWatchdog();
                } else {
                    _firstFrameTimer.stop();
                }
            });
}

void AndroidVideoDecoderRecovery::_handleGenerationStarted(
    const QString &uri,
    quint64 generation)
{
    _firstFrameTimer.stop();
    _activeUri = uri;
    _activeGeneration = generation;
    _videoCodec = static_cast<int>(VideoReceiver::VIDEO_CODEC_UNKNOWN);
    _startAccepted = false;
    _decodingRequested = false;
    _streaming = false;
    _sourceFrameReceived = false;
    _adapterSelected = false;
    _decoderFrameReceived = false;
    _sinkFrameReceived = false;
    _selectedDecoderPlugin.clear();
    _selectedDecoderFactory.clear();
    _stopping = false;
}

void AndroidVideoDecoderRecovery::_handleStartComplete(int status)
{
    _startAccepted = status == VideoReceiver::STATUS_OK
        || status == VideoReceiver::STATUS_INVALID_STATE;
    if (!_startAccepted) {
        _firstFrameTimer.stop();
        _activeUri.clear();
        _activeGeneration = 0;
        return;
    }

    _armFirstFrameWatchdog();
}

void AndroidVideoDecoderRecovery::_handleStartDecodingComplete(int status)
{
    if (status == VideoReceiver::STATUS_OK
        || status == VideoReceiver::STATUS_INVALID_STATE) {
        _decodingRequested = true;
        _armFirstFrameWatchdog();
        return;
    }

    _restartAfterDecoderFailure(
        "RTSP decoder or video sink failed to start",
        true);
}

void AndroidVideoDecoderRecovery::_handleSourceFrameReceived(
    const QString &uri,
    quint64 generation,
    int videoCodec)
{
    if (generation != _activeGeneration || uri != _activeUri) {
        return;
    }

    _sourceFrameReceived = true;
    if (videoCodec != static_cast<int>(VideoReceiver::VIDEO_CODEC_UNKNOWN)) {
        _videoCodec = videoCodec;
    }
    _armFirstFrameWatchdog();
}

void AndroidVideoDecoderRecovery::_handleDecoderSelected(
    const QString &uri,
    quint64 generation,
    int videoCodec,
    const QString &plugin,
    const QString &factory)
{
    if (generation != _activeGeneration || uri != _activeUri) {
        return;
    }

    if (videoCodec != static_cast<int>(VideoReceiver::VIDEO_CODEC_UNKNOWN)) {
        _videoCodec = videoCodec;
    }
    _selectedDecoderPlugin = plugin;
    _selectedDecoderFactory = factory;
    const bool adapterSelected = isAdapterFactoryForRoute(
        _receiver.data(), uri, factory);
    _adapterSelected |= adapterSelected;
    if (adapterSelected
        && _videoCodec
            == static_cast<int>(VideoReceiver::VIDEO_CODEC_UNKNOWN)) {
        _videoCodec = static_cast<int>(VideoReceiver::VIDEO_CODEC_H265);
    }
    _armFirstFrameWatchdog();
}

void AndroidVideoDecoderRecovery::_handleDecoderFrameReceived(
    const QString &uri,
    quint64 generation,
    const QString &plugin,
    const QString &factory)
{
    if (generation != _activeGeneration || uri != _activeUri) {
        return;
    }

    _decoderFrameReceived = true;
    _selectedDecoderPlugin = plugin;
    _selectedDecoderFactory = factory;
    _armFirstFrameWatchdog();
}

void AndroidVideoDecoderRecovery::_handleSinkFrameReceived(
    const QString &uri,
    quint64 generation)
{
    if (generation != _activeGeneration || uri != _activeUri) {
        return;
    }

    _sinkFrameReceived = true;
    _firstFrameTimer.stop();
}

void AndroidVideoDecoderRecovery::_handlePipelineError(
    const QString &uri,
    quint64 generation,
    const QString &errorPlugin,
    const QString &errorFactory,
    bool rtspSourceError,
    bool decoderBranchError,
    int videoCodec,
    const QString &decoderPlugin,
    const QString &decoderFactory,
    bool sourceFrameReceived,
    bool decoderFrameReceived,
    bool sinkFrameReceived)
{
    if (generation != _activeGeneration || uri != _activeUri || _stopping) {
        return;
    }

    // GstVideoReceiver stops every current-generation bus error. Freeze a
    // proven next hardware route first, then prevent a first-frame timeout
    // or late decoder-selection signal from starting a competing stop.
    _firstFrameTimer.stop();
    if (videoCodec != static_cast<int>(VideoReceiver::VIDEO_CODEC_UNKNOWN)) {
        _videoCodec = videoCodec;
    }
    if (!decoderPlugin.isEmpty()) {
        _selectedDecoderPlugin = decoderPlugin;
    }
    if (!decoderFactory.isEmpty()) {
        _selectedDecoderFactory = decoderFactory;
    }
    _sourceFrameReceived |= sourceFrameReceived;
    _decoderFrameReceived |= decoderFrameReceived;
    _sinkFrameReceived |= sinkFrameReceived;
    _adapterSelected |= isAdapterFactoryForRoute(
                            _receiver.data(), uri, decoderFactory)
        || isAdapterFactoryForRoute(
               _receiver.data(), uri, errorFactory);
    if (_adapterSelected
        && _videoCodec
            == static_cast<int>(VideoReceiver::VIDEO_CODEC_UNKNOWN)) {
        _videoCodec = static_cast<int>(VideoReceiver::VIDEO_CODEC_H265);
    }
    const bool hardwareRouteAdvanced = !rtspSourceError && decoderBranchError
        && AndroidH265DecoderFallback::prepareHardwareRetry(
            _receiver.data(),
            _activeUri,
            _activeGeneration,
            _videoCodec,
            _adapterSelected,
            _sourceFrameReceived,
            true,
            _decoderFrameReceived,
            _sinkFrameReceived,
            "H.265 adapter decoder-branch bus error");
    _stopping = true;
    qCWarning(AndroidVideoDecoderRecoveryLog)
        << "Android primary video pipeline stopped; configured decoder ranks remain unchanged"
        << uri
        << "generation" << generation
        << "codec" << _videoCodec
        << "decoder"
        << _selectedDecoderPlugin + QLatin1Char('/')
               + _selectedDecoderFactory
        << "decoderFrameReceived" << decoderFrameReceived
        << "errorSource"
        << errorPlugin + QLatin1Char('/') + errorFactory
        << "rtspSourceError" << rtspSourceError
        << "decoderBranchError" << decoderBranchError
        << "sinkFrameReceived" << _sinkFrameReceived
        << "hardwareRouteAdvanced" << hardwareRouteAdvanced;
    // GstVideoReceiver emitted this signal immediately before its own stop;
    // its owner rebuilds without ever promoting a software decoder.
}

void AndroidVideoDecoderRecovery::_handleStopComplete()
{
    _firstFrameTimer.stop();
    _activeUri.clear();
    _activeGeneration = 0;
    _videoCodec = static_cast<int>(VideoReceiver::VIDEO_CODEC_UNKNOWN);
    _startAccepted = false;
    _decodingRequested = false;
    _streaming = false;
    _sourceFrameReceived = false;
    _adapterSelected = false;
    _decoderFrameReceived = false;
    _sinkFrameReceived = false;
    _selectedDecoderPlugin.clear();
    _selectedDecoderFactory.clear();
    _stopping = false;
}

void AndroidVideoDecoderRecovery::_armFirstFrameWatchdog()
{
    if (!_receiver || _stopping || !_activeGeneration || !_startAccepted
        || !_decodingRequested || !_streaming || !_sourceFrameReceived
        || _sinkFrameReceived) {
        _firstFrameTimer.stop();
        return;
    }

    if (!isRtspUri(_activeUri)) {
        _firstFrameTimer.stop();
        return;
    }

    if (!_firstFrameTimer.isActive()) {
        _firstFrameTimer.start(_firstFrameTimeoutMs());
    }
}

void AndroidVideoDecoderRecovery::_restartAfterDecoderFailure(
    const char *reason,
    bool confirmedDecoderBranchFailure)
{
    if (!_receiver || _stopping || !_activeGeneration || !_startAccepted
        || _sinkFrameReceived) {
        return;
    }

    if (!isRtspUri(_activeUri)) {
        return;
    }

    _firstFrameTimer.stop();
    _stopping = true;
    const bool hardwareRouteAdvanced =
        AndroidH265DecoderFallback::prepareHardwareRetry(
            _receiver.data(),
            _activeUri,
            _activeGeneration,
            _videoCodec,
            _adapterSelected,
            _sourceFrameReceived,
            confirmedDecoderBranchFailure,
            _decoderFrameReceived,
            _sinkFrameReceived,
            reason);
    qCWarning(AndroidVideoDecoderRecoveryLog)
        << "Android primary RTSP decoding did not become healthy; rebuilding the receiver"
        << _activeUri
        << "generation" << _activeGeneration
        << "decoder"
        << _selectedDecoderPlugin + QLatin1Char('/')
               + _selectedDecoderFactory
        << "decoderFrameReceived" << _decoderFrameReceived
        << "hardwareRouteAdvanced" << hardwareRouteAdvanced
        << "reason" << reason;

    // VideoManager owns the desired-running state. A complete receiver stop
    // makes its existing onStopComplete path rebuild this URI. Decoder ranks
    // remain unchanged; a failed stream never promotes avdec_h265.
    _receiver->stop();
}

int AndroidVideoDecoderRecovery::_firstFrameTimeoutMs() const
{
    VideoSettings *const videoSettings =
        SettingsManager::instance()->videoSettings();
    const quint64 configuredSeconds = videoSettings
        ? videoSettings->rtspTimeout()->rawValue().toUInt()
        : kCoreMinimumRtspTimeoutSeconds;
    const quint64 effectiveSourceTimeoutSeconds = qMax(
        configuredSeconds,
        kCoreMinimumRtspTimeoutSeconds);
    const quint64 requestedMs = effectiveSourceTimeoutSeconds * 1000u
        + kDecoderRecoveryMarginMs;
    return static_cast<int>(qMin(
        requestedMs,
        static_cast<quint64>(std::numeric_limits<int>::max())));
}
