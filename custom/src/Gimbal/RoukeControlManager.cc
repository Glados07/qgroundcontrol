/****************************************************************************
 * Rouke camera controls and independent Video 2 local-media lifecycle.
 ****************************************************************************/
#include "RoukeControlManager.h"
#include "Android/AndroidMediaLibrary.h"
#include "AppSettings.h"
#include "Fact.h"
#include "GimbalControlSettings.h"
#include "GimbalPhotoCapturePolicy.h"
#include "SettingsManager.h"
#include "VideoManager/VideoReceiver/VideoReceiver.h"
#include "VideoSettings.h"
#include "QGCLoggingCategory.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QEventLoop>
#include <QtCore/QFileInfo>
#include <QtCore/QMetaObject>
#include <QtCore/QSaveFile>
#include <QtCore/QSharedPointer>
#include <QtCore/QtMath>
#include <QtGui/QImage>
#include <QtGui/QImageWriter>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickItemGrabResult>
#include <QtQuick/QQuickWindow>
#include <utility>

namespace {
QGC_LOGGING_CATEGORY(RoukeControlLog, "gcs.custom.gimbal.rouke.control")
constexpr int kLocalPhotoTimeoutMs = 5000;
constexpr int kLocalPhotoJpegQuality = 100;
constexpr int kMaximumLocalPhotoLongEdge = 4096;
constexpr int kMaximumLocalPhotoShortEdge = 2160;
constexpr qint64 kMaximumLocalPhotoPixelCount =
    static_cast<qint64>(kMaximumLocalPhotoLongEdge)
        * kMaximumLocalPhotoShortEdge;
constexpr int kLocalRecordingStartTimeoutMs = 3000;
constexpr int kLocalRecordingStopTimeoutMs = 5000;
constexpr int kShutdownRecordingWaitMs = 3000;
constexpr const char* kRecordingExtensions[VideoReceiver::FILE_FORMAT_MAX + 1] = {
    "mkv", "mov", "mp4",
};

class RoukePhotoGrabLifetime final : public QObject
{
public:
    RoukePhotoGrabLifetime(
        const QSharedPointer<QQuickItemGrabResult>& result,
        QQuickWindow* window)
        : QObject(window)
        , grabResult(result)
    {
    }

    QSharedPointer<QQuickItemGrabResult> grabResult;
};

QString localDirectory(AppSettings* settings, bool photo)
{
    if (!settings) {
        return {};
    }
#ifdef Q_OS_ANDROID
    const QString mediaDirectory = photo
        ? QStringLiteral("Photo") : QStringLiteral("Video");
    const QString staging = AndroidMediaLibrary::mediaStagingDirectory(
        settings->savePath()->rawValue().toString(),
        QCoreApplication::applicationName(),
        mediaDirectory);
    if (!staging.isEmpty()) {
        return staging;
    }
#endif
    return photo ? settings->photoSavePath() : settings->videoSavePath();
}

void publishLocalMedia(const QString& path, bool photo)
{
#ifdef Q_OS_ANDROID
    const QString mimeType = photo
        ? QStringLiteral("image/jpeg")
        : (QFileInfo(path).suffix().compare(QStringLiteral("mp4"),
                                            Qt::CaseInsensitive) == 0
               ? QStringLiteral("video/mp4")
               : (QFileInfo(path).suffix().compare(QStringLiteral("mov"),
                                                   Qt::CaseInsensitive) == 0
                      ? QStringLiteral("video/quicktime")
                      : QStringLiteral("video/x-matroska")));
    (void) AndroidMediaLibrary::publishMediaFile(
        path,
        mimeType,
        QCoreApplication::applicationName(),
        photo ? QStringLiteral("Photo") : QStringLiteral("Video"));
#else
    Q_UNUSED(path);
    Q_UNUSED(photo);
#endif
}

struct PhotoSaveOutcome {
    bool success = false;
    QString error;
    QSize outputPixelSize;
    qint64 fileSize = -1;
};

PhotoSaveOutcome savePhoto(QImage image,
                           const GimbalPhotoCapturePolicy::CaptureGeometry& geometry,
                           const QString& filename)
{
    PhotoSaveOutcome outcome;
    image = GimbalPhotoCapturePolicy::prepareImageForSaving(image, geometry);
    if (image.isNull()) {
        outcome.error = QStringLiteral("Image preparation failed.");
        return outcome;
    }

    outcome.outputPixelSize = image.size();

    QSaveFile file(filename);
    if (!file.open(QIODevice::WriteOnly)) {
        outcome.error = file.errorString();
        return outcome;
    }
    QImageWriter writer(&file, "jpg");
    writer.setQuality(kLocalPhotoJpegQuality);
    if (!writer.write(image)) {
        outcome.error = writer.errorString();
        file.cancelWriting();
        return outcome;
    }
    if (!file.commit()) {
        outcome.error = file.errorString();
        return outcome;
    }
    outcome.success = true;
    outcome.fileSize = QFileInfo(filename).size();
    return outcome;
}

} // namespace


RoukeControlManager::RoukeControlManager(GimbalControlSettings *settings, QObject *parent)
    : QObject(parent), _settings(settings), _gimbalController(&_controller, this)
{
    Q_CHECK_PTR(_settings);
    _localPhotoTimer.setSingleShot(true);
    _localPhotoTimer.setInterval(kLocalPhotoTimeoutMs);
    _localRecordingStartTimer.setSingleShot(true);
    _localRecordingStartTimer.setInterval(kLocalRecordingStartTimeoutMs);
    _localRecordingStopTimer.setSingleShot(true);
    _localRecordingStopTimer.setInterval(kLocalRecordingStopTimeoutMs);
    _localPhotoSaveThreadPool.setMaxThreadCount(1);
    connect(&_localPhotoTimer, &QTimer::timeout, this, &RoukeControlManager::_handleLocalPhotoTimeout);
    connect(&_localRecordingStartTimer, &QTimer::timeout, this, &RoukeControlManager::_handleLocalRecordingStartTimeout);
    connect(&_localRecordingStopTimer, &QTimer::timeout, this, &RoukeControlManager::_handleLocalRecordingStopTimeout);
    connect(&_controller, &RoukePayloadController::stateChanged, this, [this]() {
        emit stateChanged();
        _notifyRecordingSessionStateChanged();
    });
    connect(&_controller, &RoukePayloadController::commandFinished, this,
            [this](const QString &command, bool success) {
                if (command == QStringLiteral("lr1_record") && !success
                    && !_localRecordingIntent && !_localRecordingActive) {
                    _recordingSessionRequested = false;
                }
                _notifyRecordingSessionStateChanged();
            });
    for (Fact *fact : {_settings->roukeEnabled(), _settings->roukeUdpHost(),
                       _settings->roukeUdpPort(), _settings->roukeLocalPort(),
                       _settings->roukePayloadAddress()}) {
        connect(fact, &Fact::rawValueChanged, this, &RoukeControlManager::_settingsChanged);
    }
    for (Fact *fact : {_settings->roukeYawSpeedLimit(), _settings->roukePitchSpeedLimit(),
                       _settings->roukeYawReversed(), _settings->roukePitchReversed()}) {
        connect(fact, &Fact::rawValueChanged, this, &RoukeControlManager::_gimbalSettingsChanged);
    }
    _gimbalSettingsChanged();
    connect(_settings->localMediaStorageEnabled(), &Fact::rawValueChanged, this, [this]() {
        emit localMediaStorageEnabledChanged();
        if (!localMediaStorageEnabled()) {
            _localRecordingIntent = false;
            _stopLocalRecording();
        }
        _notifyRecordingSessionStateChanged();
    });
    _settingsChanged();
}

RoukeControlManager::~RoukeControlManager()
{
    _gimbalController.cancel();
    // Retire incoming camera ACKs before the bounded media-finalization loop.
    _controller.setConfiguration(false, {}, 0, 0, -1);
    _localPhotoTimer.stop();
    ++_localPhotoSequence;
    _localPhotoGrabLifetime.clear();
    _localPhotoPending = false;
    _localPhotoSaveThreadPool.waitForDone();
    shutdownLocalMedia(true);
}

bool RoukeControlManager::enabled() const
{
    return _settings && _settings->roukeEnabled()->rawValue().toBool();
}

bool RoukeControlManager::localMediaStorageEnabled() const
{
    return _settings && _settings->localMediaStorageEnabled()->rawValue().toBool();
}

void RoukeControlManager::_settingsChanged()
{
    _gimbalController.cancel();
    const bool active = enabled();
    _recordingSessionRequested = false;
    const int port = _settings->roukeUdpPort()->rawValue().toInt();
    const int localPort = _settings->roukeLocalPort()->rawValue().toInt();
    _controller.setConfiguration(active,
        _settings->roukeUdpHost()->rawValue().toString(),
        port > 0 && port <= 65535 ? static_cast<quint16>(port) : 0,
        localPort >= 0 && localPort <= 65535 ? static_cast<quint16>(localPort) : 0,
        _settings->roukePayloadAddress()->rawValue().toInt());
    if (_lastEnabled != active) {
        _lastEnabled = active;
        if (!active) {
            _recordingSessionRequested = false;
            _localRecordingIntent = false;
            _stopLocalRecording();
        }
        emit enabledChanged();
    }
    _notifyRecordingSessionStateChanged();
}

void RoukeControlManager::_gimbalSettingsChanged()
{
    _gimbalController.setConfiguration(_settings->roukeYawSpeedLimit()->rawValue().toInt(),
        _settings->roukePitchSpeedLimit()->rawValue().toInt(),
        _settings->roukeYawReversed()->rawValue().toBool(),
        _settings->roukePitchReversed()->rawValue().toBool());
}

bool RoukeControlManager::recordingSessionActive() const
{
    return _recordingSessionRequested || cameraRecordingPending() || (recordingKnown() && recording())
        || _localRecordingActive || localRecordingPending();
}

bool RoukeControlManager::recordingSessionCapturing() const
{
    return (recordingKnown() && recording()) || _localRecordingActive;
}

bool RoukeControlManager::videoRecordingAvailable() const
{
    return enabled() && (controlAvailable() || recordingSessionActive()
        || (localMediaStorageEnabled() && _receiverStreaming()));
}

bool RoukeControlManager::takePhoto()
{
    if (!enabled()) return false;
    const bool local = localMediaStorageEnabled() && _captureLocalVideoFrame();
    const bool camera = _controller.takePhoto();
    return local || camera;
}

bool RoukeControlManager::startRecording()
{
    if (!enabled()) return false;
    if (localMediaStorageEnabled() && _receiverStreaming()) {
        _localRecordingIntent = true;
        _startLocalRecording();
    }
    const bool camera = recordingKnown() && recording()
        ? true : _controller.setRecording(true);
    const bool local = _localRecordingActive || _localRecordingStartPending;
    _recordingSessionRequested = camera || local;
    _notifyRecordingSessionStateChanged();
    return camera || local;
}

bool RoukeControlManager::stopRecording()
{
    // An explicit stop is valid when camera state is unknown (e.g. lost ACK).
    // Local cleanup never depends on UDP reachability or the SD command result.
    const bool local = _localRecordingActive || localRecordingPending() || _localRecordingOwned;
    _recordingSessionRequested = false;
    _localRecordingIntent = false;
    _stopLocalRecording();
    const bool camera = enabled() && _controller.setRecording(false);
    _notifyRecordingSessionStateChanged();
    return camera || local;
}

bool RoukeControlManager::toggleRecording()
{
    return recordingSessionActive() ? stopRecording() : startRecording();
}

void RoukeControlManager::setVideoItem(QQuickItem* videoItem)
{
    if (_videoItem == videoItem) {
        return;
    }
    if (_localPhotoPending) {
        _handleLocalPhotoTimeout();
    }
    _videoItem = videoItem;
}

void RoukeControlManager::setVideoReceiver(VideoReceiver* receiver)
{
    if (_videoReceiver == receiver) {
        return;
    }

    if (_videoReceiver) {
        disconnect(_videoReceiver, nullptr, this, nullptr);
        if (_localRecordingActive || _localRecordingStartPending
            || _localRecordingStopPending || _localRecordingOwned) {
            _localRecordingIntent = false;
            if (!_localRecordingOutputFile.isEmpty()
                && !_detachedRecordingOutputs.contains(
                    _localRecordingOutputFile)) {
                _detachedRecordingOutputs.append(
                    _localRecordingOutputFile);
            }
            _resetLocalRecordingState();
            _setLocalMediaError(tr("The Rouke video receiver was detached during local recording."));
        }
    }
    _videoReceiver = receiver;
    _negotiatedPulledVideoSize = QSize();
    _receiverStreamingActive = false;
    _receiverDecodingActive = false;
    _receiverRecordingActive = false;
    if (!_videoReceiver) {
        _videoItem.clear();
        _notifyRecordingSessionStateChanged();
        return;
    }

    connect(_videoReceiver, &VideoReceiver::streamingChanged,
            this, &RoukeControlManager::_handleReceiverStreamingChanged);
    connect(_videoReceiver, &VideoReceiver::decodingChanged,
            this, &RoukeControlManager::_handleReceiverDecodingChanged);
    connect(_videoReceiver, &VideoReceiver::recordingChanged,
            this, &RoukeControlManager::_handleReceiverRecordingChanged);
    const QPointer<VideoReceiver> guardedReceiver(_videoReceiver);
    connect(_videoReceiver,
            &VideoReceiver::videoSizeChanged,
            this,
            [this, guardedReceiver](const QSize& videoSize) {
                setNegotiatedPulledVideoResolution(
                    guardedReceiver.data(),
                    videoSize);
            });
    connect(_videoReceiver,
            &VideoReceiver::videoPipelineGenerationStarted,
            this,
            [this, guardedReceiver](const QString&, quint64) {
                if (guardedReceiver
                    && guardedReceiver.data() == _videoReceiver.data()) {
                    _negotiatedPulledVideoSize = QSize();
                }
            });
    connect(_videoReceiver, &VideoReceiver::onStopRecordingComplete,
            this, [this](VideoReceiver::STATUS status) {
                _handleReceiverStopRecordingComplete(static_cast<int>(status));
            });
    connect(_videoReceiver, &QObject::destroyed, this, [this]() {
        _videoReceiver.clear();
        _videoItem.clear();
        _receiverStreamingActive = false;
        _receiverDecodingActive = false;
        _receiverRecordingActive = false;
        _negotiatedPulledVideoSize = QSize();
        _localRecordingIntent = false;
        _resetLocalRecordingState();
    });
    if (_videoReceiver->started()) {
        // createVideoSink may be called after the receiver has already begun
        // its source session. Until the first explicit streaming signal, the
        // started flag is the only safe initial local-media availability hint.
        _receiverStreamingActive = true;
    }
    _notifyRecordingSessionStateChanged();
}

void RoukeControlManager::setNegotiatedPulledVideoResolution(
    VideoReceiver* sourceReceiver,
    const QSize& videoSize)
{
    if (!sourceReceiver || sourceReceiver != _videoReceiver.data()) {
        return;
    }

    if (!GimbalPhotoCapturePolicy::isPixelSizeWithinBounds(
            videoSize,
            kMaximumLocalPhotoLongEdge,
            kMaximumLocalPhotoShortEdge,
            kMaximumLocalPhotoPixelCount)) {
        if (videoSize.isValid() && !videoSize.isEmpty()) {
            qCWarning(RoukeControlLog)
                << "Rejected unsafe decoded Rouke Video 2 resolution"
                << videoSize;
        }
        _negotiatedPulledVideoSize = QSize();
        return;
    }

    if (_negotiatedPulledVideoSize == videoSize) {
        return;
    }

    _negotiatedPulledVideoSize = videoSize;
    qCInfo(RoukeControlLog)
        << "Updated decoded Rouke Video 2 resolution"
        << _negotiatedPulledVideoSize;
}

void RoukeControlManager::handleVideoRecordingStartResult(
    bool success,
    const QString& outputFile)
{
    if (_issuedLocalRecordingFileBases.isEmpty()) {
        return;
    }
    if (!success) {
        // VideoReceiver may not expose a filename on failure. Consume only
        // the one outstanding generation (starts are deliberately serialized).
        if (!_issuedLocalRecordingFileBases.isEmpty()) {
            _issuedLocalRecordingFileBases.takeFirst();
        }
        _localRecordingStartTimer.stop();
        _localRecordingStartPending = false;
        _localRecordingOwned = false;
        _localRecordingIntent = false;
        _setLocalMediaError(tr("Failed to start Rouke local video recording."));
        emit localRecordingStateChanged();
        _notifyRecordingSessionStateChanged();
        return;
    }
    const QString resultBase = outputFile.isEmpty()
        ? QString() : QFileInfo(outputFile).completeBaseName();
    if (!_issuedLocalRecordingFileBases.removeOne(resultBase)) {
        return;
    }

    _localRecordingOutputFile = outputFile;
    _localRecordingOwned = true;
    if (_shuttingDown || !_localRecordingIntent
        || !localMediaStorageEnabled()) {
        _localRecordingStartPending = false;
        _stopLocalRecording();
        return;
    }
    _localRecordingStartTimer.stop();
    _localRecordingStartPending = false;
    _localRecordingActive = true;
    emit localRecordingStateChanged();
    _notifyRecordingSessionStateChanged();
}

void RoukeControlManager::shutdownLocalMedia(bool waitForStop)
{
    _shuttingDown = true;
    _localRecordingIntent = false;
    if (_localRecordingStartPending && !_localRecordingOwned) {
        // A late start callback still carries the unique output basename and
        // will issue its compensating stop while the manager remains alive.
        _localRecordingStartTimer.stop();
    } else {
        _stopLocalRecording();
    }

    const auto finalized = [this]() {
        return !_localRecordingStartPending
            && !_localRecordingStopPending
            && !_localRecordingOwned
            && _issuedLocalRecordingFileBases.isEmpty();
    };
    if (waitForStop && !finalized()) {
        QEventLoop loop;
        QTimer deadline;
        deadline.setSingleShot(true);
        connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
        connect(this, &RoukeControlManager::localRecordingStateChanged,
                &loop, [&loop, finalized]() {
                    if (finalized()) {
                        loop.quit();
                    }
                });
        deadline.start(kShutdownRecordingWaitMs);
        loop.exec(QEventLoop::ExcludeUserInputEvents);
    }
    if (waitForStop) {
        _localPhotoSaveThreadPool.waitForDone();
#ifdef Q_OS_ANDROID
        if (!AndroidMediaLibrary::waitForPendingPublications(120000)) {
            qCWarning(RoukeControlLog)
                << "Timed out publishing Rouke media during shutdown";
        }
#endif
    }
    _shuttingDown = false;
}

void RoukeControlManager::_handleReceiverStreamingChanged(bool active)
{
    _receiverStreamingActive = active;
    if (!active) {
        _negotiatedPulledVideoSize = QSize();
    }
    if (!active && (_localRecordingActive || _localRecordingStartPending)) {
        _localRecordingIntent = false;
        if (_localRecordingOwned && !_localRecordingStopPending) {
            _stopLocalRecording();
        } else if (!_localRecordingOwned) {
            // Preserve an unresolved start generation. A late successful
            // callback will immediately issue the compensating stop and the
            // finalized file can still be published safely.
            _localRecordingStartTimer.stop();
        }
        _setLocalMediaError(
            tr("Rouke local recording stopped because its video stream ended."));
    }
    _notifyRecordingSessionStateChanged();
}

void RoukeControlManager::_handleReceiverDecodingChanged(bool active)
{
    _receiverDecodingActive = active;
    if (!active) {
        _negotiatedPulledVideoSize = QSize();
    }
}

void RoukeControlManager::_handleReceiverRecordingChanged(bool active)
{
    _receiverRecordingActive = active;
    if (active && _localRecordingOwned && !_localRecordingStopPending) {
        _localRecordingStartTimer.stop();
        _localRecordingStartPending = false;
        _localRecordingActive = true;
        emit localRecordingStateChanged();
    } else if (!active && (_localRecordingActive || _localRecordingStopPending)) {
        _finishLocalRecording(_localRecordingOutputFile);
    }
    _notifyRecordingSessionStateChanged();
}

void RoukeControlManager::_handleReceiverStopRecordingComplete(int status)
{
    if (!_localRecordingStopPending) {
        return;
    }
    if (status == static_cast<int>(VideoReceiver::STATUS_OK)
        || !_receiverRecording()) {
        _finishLocalRecording(_localRecordingOutputFile);
    } else {
        _handleLocalRecordingStopTimeout();
    }
}

void RoukeControlManager::_handleLocalPhotoTimeout()
{
    if (!_localPhotoPending) {
        return;
    }
    ++_localPhotoSequence;
    _localPhotoGrabLifetime.clear();
    _localPhotoPending = false;
    _setLocalMediaError(tr("Failed to capture the Rouke local video frame."));
}

void RoukeControlManager::_handleLocalRecordingStartTimeout()
{
    if (!_localRecordingStartPending) {
        return;
    }
    // Keep the issued generation: a late success callback must still be
    // recognized and stopped rather than leaking a recorder session.
    _localRecordingStartPending = false;
    _localRecordingIntent = false;
    _setLocalMediaError(tr("Timed out starting Rouke local video recording."));
    emit localRecordingStateChanged();
    _notifyRecordingSessionStateChanged();
}

void RoukeControlManager::_handleLocalRecordingStopTimeout()
{
    if (!_localRecordingStopPending) {
        return;
    }
    if (_videoReceiver && _localRecordingOwned && _receiverRecording()) {
        _videoReceiver->stopRecording();
        _localRecordingStopTimer.start();
        _setLocalMediaError(
            tr("Timed out stopping Rouke local video recording."));
        return;
    }
    _finishLocalRecording(_localRecordingOutputFile);
}

void RoukeControlManager::_setLocalMediaError(const QString& message)
{
    if (_localMediaError == message) return;
    _localMediaError = message;
    emit localMediaErrorChanged();
}

void RoukeControlManager::_notifyRecordingSessionStateChanged()
{
    emit recordingSessionStateChanged();
}

bool RoukeControlManager::_captureLocalVideoFrame()
{
    if (_localPhotoPending || _localPhotoGrabLifetimeCount > 0) {
        _setLocalMediaError(tr("A Rouke local photo is still being processed."));
        return false;
    }
    if (!_videoItem || !_receiverDecoding()
        || _videoItem->width() <= 0 || _videoItem->height() <= 0) {
        _setLocalMediaError(tr("No decoded Rouke video frame is available for a local photo."));
        return false;
    }

    AppSettings* appSettings = SettingsManager::instance()->appSettings();
    const QString directory = localDirectory(appSettings, true);
    if (directory.isEmpty()
        || (!QDir(directory).exists() && !QDir().mkpath(directory))
        || !QFileInfo(directory).isWritable()) {
        _setLocalMediaError(tr("The local photo save path is unavailable or not writable."));
        return false;
    }

    const quint64 sequence = ++_localPhotoSequence;
    const QString filename = QDir(directory).filePath(
        QStringLiteral("Rouke_%1_local_%2.jpg")
            .arg(QDateTime::currentDateTime().toString(
                     QStringLiteral("yyyy-MM-dd_hh.mm.ss.zzz")))
            .arg(sequence, 3, 10, QLatin1Char('0')));
    QQuickWindow* window = _videoItem->window();
    if (!window) {
        _setLocalMediaError(tr("Failed to capture the Rouke local video frame."));
        return false;
    }

    const qreal reportedDevicePixelRatio =
        window->effectiveDevicePixelRatio();
    const qreal effectiveDevicePixelRatio =
        qIsFinite(reportedDevicePixelRatio)
            && reportedDevicePixelRatio > 0.0
        ? reportedDevicePixelRatio
        : 1.0;
    const QSize itemPixelSize(
        qMax(1,
             qRound(_videoItem->width()
                    * effectiveDevicePixelRatio)),
        qMax(1,
             qRound(_videoItem->height()
                    * effectiveDevicePixelRatio)));
    const QSize implicitVideoSize(
        qRound(_videoItem->implicitWidth()),
        qRound(_videoItem->implicitHeight()));
    const bool negotiatedSizeSafe =
        GimbalPhotoCapturePolicy::isPixelSizeWithinBounds(
            _negotiatedPulledVideoSize,
            kMaximumLocalPhotoLongEdge,
            kMaximumLocalPhotoShortEdge,
            kMaximumLocalPhotoPixelCount);
    const bool implicitSizeSafe =
        GimbalPhotoCapturePolicy::isPixelSizeWithinBounds(
            implicitVideoSize,
            kMaximumLocalPhotoLongEdge,
            kMaximumLocalPhotoShortEdge,
            kMaximumLocalPhotoPixelCount);
    const bool itemSizeSafe =
        GimbalPhotoCapturePolicy::isPixelSizeWithinBounds(
            itemPixelSize,
            kMaximumLocalPhotoLongEdge,
            kMaximumLocalPhotoShortEdge,
            kMaximumLocalPhotoPixelCount);
    const QSize sourcePixelSize =
        GimbalPhotoCapturePolicy::resolveSourcePixelSize(
            negotiatedSizeSafe ? _negotiatedPulledVideoSize : QSize(),
            implicitSizeSafe ? implicitVideoSize : QSize(),
            itemSizeSafe ? itemPixelSize : QSize());
    // The vendor protocol supplies no card-recording resolution query.
    // Preserve real decoded Video 2 pixels; never infer sensor/photo resolution.
    const QSize outputPixelSize = sourcePixelSize;
    if (!GimbalPhotoCapturePolicy::isPixelSizeWithinBounds(
            sourcePixelSize,
            kMaximumLocalPhotoLongEdge,
            kMaximumLocalPhotoShortEdge,
            kMaximumLocalPhotoPixelCount)
        || !GimbalPhotoCapturePolicy::isPixelSizeWithinBounds(
            outputPixelSize,
            kMaximumLocalPhotoLongEdge,
            kMaximumLocalPhotoShortEdge,
            kMaximumLocalPhotoPixelCount)) {
        _setLocalMediaError(tr("Failed to capture the Rouke local video frame."));
        qCWarning(RoukeControlLog)
            << "Rejected unsafe Rouke local photo dimensions"
            << "negotiated" << _negotiatedPulledVideoSize
            << "implicit" << implicitVideoSize
            << "rendered" << itemPixelSize;
        return false;
    }
    const auto geometry = GimbalPhotoCapturePolicy::captureGeometry(
        outputPixelSize,
        sourcePixelSize,
        effectiveDevicePixelRatio);
    if (!geometry.isValid()) {
        _setLocalMediaError(tr("Failed to capture the Rouke local video frame."));
        qCWarning(RoukeControlLog)
            << "Rejected invalid Rouke local photo geometry"
            << "decoded source" << sourcePixelSize
            << "DPR" << effectiveDevicePixelRatio;
        return false;
    }

    const char* sourceSizeDescription = negotiatedSizeSafe
        ? "negotiated Video 2 stream"
        : (implicitSizeSafe
               ? "video item implicit-size fallback"
               : "video item physical-size fallback");
    qCInfo(RoukeControlLog)
        << "Starting Rouke local camera-frame capture"
        << "output" << geometry.outputPixelSize
        << "output source"
        << sourceSizeDescription
        << "decoded source" << sourcePixelSize
        << "negotiated" << _negotiatedPulledVideoSize
        << "item DIP"
        << QSize(qRound(_videoItem->width()),
                 qRound(_videoItem->height()))
        << "item implicit" << implicitVideoSize
        << "DPR" << effectiveDevicePixelRatio
        << "content" << geometry.contentPixelSize
        << "grab logical target" << geometry.grabLogicalSize
        << "JPEG quality" << kLocalPhotoJpegQuality;

    const QSharedPointer<QQuickItemGrabResult> result =
        _videoItem->grabToImage(geometry.grabLogicalSize);
    if (!result) {
        _setLocalMediaError(tr("Failed to capture the Rouke local video frame."));
        return false;
    }
    auto* lifetime = new RoukePhotoGrabLifetime(result, window);
    ++_localPhotoGrabLifetimeCount;
    connect(lifetime, &QObject::destroyed, this, [this]() {
        Q_ASSERT(_localPhotoGrabLifetimeCount > 0);
        --_localPhotoGrabLifetimeCount;
    });
    _localPhotoGrabLifetime = lifetime;
    _localPhotoPending = true;
    _setLocalMediaError({});
    const QPointer<QObject> guardedLifetime(lifetime);
    const QWeakPointer<QQuickItemGrabResult> weakResult = result.toWeakRef();
    connect(result.data(), &QQuickItemGrabResult::ready, this,
            [this, guardedLifetime, weakResult, filename, sequence, geometry]() {
                const auto strongResult = weakResult.toStrongRef();
                if (!strongResult || !guardedLifetime
                    || guardedLifetime != _localPhotoGrabLifetime
                    || sequence != _localPhotoSequence) {
                    return;
                }
                _localPhotoTimer.stop();
                _localPhotoGrabLifetime.clear();
                QImage image = strongResult->image();
                const QSize grabbedPixelSize = image.size();
                if (image.isNull()) {
                    _localPhotoPending = false;
                    _setLocalMediaError(tr("Failed to capture the Rouke local video frame."));
                    qCWarning(RoukeControlLog)
                        << "Rouke camera-frame grab produced no usable image"
                        << "raw" << grabbedPixelSize
                        << "expected content" << geometry.contentPixelSize
                        << "expected output" << geometry.outputPixelSize;
                    return;
                }
                _localPhotoSaveThreadPool.start(
                    [this,
                     image = std::move(image),
                     geometry,
                     filename,
                     sequence,
                     grabbedPixelSize]() mutable {
                        const PhotoSaveOutcome outcome =
                            savePhoto(std::move(image), geometry, filename);
                        const bool completionQueued = QMetaObject::invokeMethod(
                            this,
                            [this,
                             outcome,
                             filename,
                             sequence,
                             grabbedPixelSize,
                             geometry]() {
                                if (sequence != _localPhotoSequence) return;
                                _localPhotoPending = false;
                                if (!outcome.success) {
                                    _setLocalMediaError(tr("Failed to save the Rouke local video frame."));
                                    qCWarning(RoukeControlLog)
                                        << "Failed to save Rouke local camera frame"
                                        << filename << outcome.error
                                        << "raw" << grabbedPixelSize
                                        << "expected output"
                                        << geometry.outputPixelSize;
                                    return;
                                }
                                ++_localPhotoCount;
                                emit localPhotoCountChanged();
                                _setLocalMediaError({});
                                qCInfo(RoukeControlLog)
                                    << "Saved Rouke local camera frame"
                                    << filename
                                    << "raw grab" << grabbedPixelSize
                                    << "output" << outcome.outputPixelSize
                                    << "bytes" << outcome.fileSize;
                                publishLocalMedia(filename, true);
                            },
                            Qt::QueuedConnection);
                        if (!completionQueued) {
                            qCWarning(RoukeControlLog)
                                << "Failed to queue Rouke local photo completion"
                                << filename;
                        }
                    });
            }, Qt::SingleShotConnection);
    connect(result.data(), &QQuickItemGrabResult::ready, lifetime,
            [lifetime]() { lifetime->deleteLater(); },
            Qt::SingleShotConnection);
    _localPhotoTimer.start();
    return true;
}

void RoukeControlManager::_startLocalRecording()
{
    if (!_videoReceiver || !_videoReceiver->started() || !_receiverStreaming()
        || _localRecordingStartPending || _localRecordingActive
        || _localRecordingStopPending || _localRecordingOwned
        || !_issuedLocalRecordingFileBases.isEmpty()) {
        return;
    }
    AppSettings* appSettings = SettingsManager::instance()->appSettings();
    VideoSettings* videoSettings = SettingsManager::instance()->videoSettings();
    const QString directory = localDirectory(appSettings, false);
    if (directory.isEmpty()
        || (!QDir(directory).exists() && !QDir().mkpath(directory))
        || !QFileInfo(directory).isWritable()) {
        _localRecordingIntent = false;
        _setLocalMediaError(tr("The local video save path is unavailable or not writable."));
        return;
    }
    const auto format = videoSettings
        ? static_cast<VideoReceiver::FILE_FORMAT>(
              videoSettings->recordingFormat()->rawValue().toInt())
        : VideoReceiver::FILE_FORMAT_MAX;
    if (!VideoReceiver::isValidFileFormat(format)) {
        _localRecordingIntent = false;
        _setLocalMediaError(tr("The configured local video format is invalid."));
        return;
    }

    _localRecordingFileBase =
        QStringLiteral("Rouke_%1_local_%2")
            .arg(QDateTime::currentDateTime().toString(
                     QStringLiteral("yyyy-MM-dd_hh.mm.ss.zzz")))
            .arg(++_localRecordingSegmentCounter, 3, 10, QLatin1Char('0'));
    _localRecordingOutputFile = QDir(directory).filePath(
        _localRecordingFileBase + QLatin1Char('.')
        + QLatin1String(kRecordingExtensions[format]));
    _issuedLocalRecordingFileBases.append(_localRecordingFileBase);
    _localRecordingOwned = false;
    _localRecordingStartPending = true;
    _localRecordingStartTimer.start();
    emit localRecordingStateChanged();
    _notifyRecordingSessionStateChanged();
    _videoReceiver->startRecording(_localRecordingOutputFile, format);
}

void RoukeControlManager::_stopLocalRecording()
{
    if (!_localRecordingActive && !_localRecordingOwned
        && !_localRecordingStartPending) {
        return;
    }
    if (_localRecordingStartPending && !_localRecordingOwned) {
        return;
    }
    if (!_videoReceiver || !_localRecordingOwned) {
        _resetLocalRecordingState();
        return;
    }
    if (_localRecordingStopPending) return;
    _localRecordingStartTimer.stop();
    _localRecordingStartPending = false;
    _localRecordingStopPending = true;
    _localRecordingStopTimer.start();
    emit localRecordingStateChanged();
    _notifyRecordingSessionStateChanged();
    _videoReceiver->stopRecording();
}

void RoukeControlManager::_resetLocalRecordingState()
{
    _localRecordingStartTimer.stop();
    _localRecordingStopTimer.stop();
    _localRecordingActive = false;
    _localRecordingStartPending = false;
    _localRecordingStopPending = false;
    _localRecordingOwned = false;
    _localRecordingFileBase.clear();
    _localRecordingOutputFile.clear();
    _issuedLocalRecordingFileBases.clear();
    emit localRecordingStateChanged();
    _notifyRecordingSessionStateChanged();
}

void RoukeControlManager::_finishLocalRecording(const QString& outputFile)
{
    const bool shouldPublish = !outputFile.isEmpty()
        && QFileInfo(outputFile).isFile()
        && QFileInfo(outputFile).size() > 0;
    _resetLocalRecordingState();
    if (shouldPublish) {
        publishLocalMedia(outputFile, false);
    }
}

void RoukeControlManager::finalizeDetachedLocalMedia()
{
    for (const QString& path : std::as_const(_detachedRecordingOutputs)) {
        const QFileInfo output(path);
        if (output.isFile() && output.size() > 0) {
            publishLocalMedia(output.absoluteFilePath(), false);
        }
    }
    _detachedRecordingOutputs.clear();
}

bool RoukeControlManager::_receiverStreaming() const
{
    return _videoReceiver && _receiverStreamingActive;
}

bool RoukeControlManager::_receiverDecoding() const
{
    return _videoReceiver && _receiverDecodingActive;
}

bool RoukeControlManager::_receiverRecording() const
{
    return _videoReceiver && _receiverRecordingActive;
}

