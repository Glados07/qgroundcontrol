/****************************************************************************
 *
 * 思翼云台相机控制管理器。
 *
 ****************************************************************************/

#include "GimbalControlManager.h"

#include "A8MiniZoomPolicy.h"
#include "Android/AndroidMediaLibrary.h"
#include "AppSettings.h"
#include "GimbalControlSettings.h"
#include "GimbalMediaSessionPolicy.h"
#include "GimbalPhotoCapturePolicy.h"
#include "QGCLoggingCategory.h"
#include "SettingsManager.h"
#include "SiyiSdk.h"
#include "Fact.h"
#include "VideoManager.h"
#include "VideoManager/VideoReceiver/VideoReceiver.h"
#include "VideoSettings.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QEventLoop>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QMetaObject>
#include <QtCore/QRegularExpression>
#include <QtCore/QSaveFile>
#include <QtCore/QSharedPointer>
#include <QtCore/QtMath>
#include <QtGui/QImage>
#include <QtGui/QImageWriter>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickItemGrabResult>
#include <QtQuick/QQuickWindow>

#include <limits>
#include <utility>

namespace {

QGC_LOGGING_CATEGORY(GimbalControlLog, "gcs.custom.gimbal.control")

static constexpr double kZoomComparisonTolerance = 0.051;
static constexpr int kLocalRecordingStartTimeoutMs = 3000;
static constexpr int kLocalRecordingStopTimeoutMs = 5000;
static constexpr int kApplicationShutdownRecordingWaitMs = 3000;
static constexpr int kLocalPhotoGrabTimeoutMs = 5000;
static constexpr int kLocalPhotoJpegQuality = 100;
static constexpr const char* kLocalRecordingFileExtensions[
    VideoReceiver::FILE_FORMAT_MAX + 1] = {
    "mkv",
    "mov",
    "mp4",
};

QString localMediaMimeType(const QString& filePath)
{
    const QString suffix = QFileInfo(filePath).suffix().toLower();
    if (suffix == QStringLiteral("jpg")
        || suffix == QStringLiteral("jpeg")) {
        return QStringLiteral("image/jpeg");
    }
    if (suffix == QStringLiteral("mp4")) {
        return QStringLiteral("video/mp4");
    }
    if (suffix == QStringLiteral("mov")) {
        return QStringLiteral("video/quicktime");
    }
    if (suffix == QStringLiteral("mkv")) {
        return QStringLiteral("video/x-matroska");
    }
    return QString();
}

enum class LocalMediaKind {
    Photo,
    Video,
};

struct LocalPhotoSaveOutcome {
    bool success = false;
    QString error;
    QSize outputPixelSize;
    qint64 fileSize = -1;
};

// QQuickItemGrabResult::setup()/render() run through direct scene-graph
// connections and can therefore still be executing on the render thread
// while the GUI thread handles a timeout. Keep the last strong reference in
// an object parented to the QQuickWindow. A business cancellation merely
// retires the holder; ready() or final window destruction releases it at a
// render-safe lifecycle boundary.
class LocalPhotoGrabLifetime final : public QObject
{
public:
    LocalPhotoGrabLifetime(
        const QSharedPointer<QQuickItemGrabResult>& result,
        QQuickWindow* window)
        : QObject(window)
        , grabResult(result)
    {
    }

    QSharedPointer<QQuickItemGrabResult> grabResult;
};

LocalPhotoSaveOutcome saveLocalPhotoImage(
    QImage grabbedImage,
    const GimbalPhotoCapturePolicy::CaptureGeometry& captureGeometry,
    const QString& filename)
{
    LocalPhotoSaveOutcome outcome;
    const QImage image =
        GimbalPhotoCapturePolicy::prepareImageForSaving(
            grabbedImage,
            captureGeometry);
    grabbedImage = QImage();
    if (image.isNull()) {
        outcome.error = QStringLiteral("Image preparation failed.");
        return outcome;
    }

    outcome.outputPixelSize = image.size();
    QSaveFile outputFile(filename);
    if (!outputFile.open(QIODevice::WriteOnly)) {
        outcome.error = outputFile.errorString();
        return outcome;
    }

    bool encoded = false;
    {
        QImageWriter writer(&outputFile, "jpg");
        writer.setQuality(kLocalPhotoJpegQuality);
        encoded = writer.write(image);
        if (!encoded) {
            outcome.error = writer.errorString();
        }
    }
    if (!encoded) {
        outputFile.cancelWriting();
        return outcome;
    }
    if (!outputFile.commit()) {
        outcome.error = outputFile.errorString();
        return outcome;
    }

    outcome.success = true;
    outcome.fileSize = QFileInfo(filename).size();
    return outcome;
}

QString localMediaSaveDirectory(AppSettings* appSettings,
                                LocalMediaKind mediaKind)
{
    if (!appSettings) {
        return QString();
    }

#ifdef Q_OS_ANDROID
    const QString mediaDirectoryName =
        mediaKind == LocalMediaKind::Photo
            ? QStringLiteral("Photo")
            : QStringLiteral("Video");
    const QString sharedMediaDirectory =
        AndroidMediaLibrary::mediaDirectory(
            appSettings->savePath()->rawValue().toString(),
            QCoreApplication::applicationName(),
            mediaDirectoryName);
    if (!sharedMediaDirectory.isEmpty()) {
        return sharedMediaDirectory;
    }

    qCWarning(GimbalControlLog)
        << "Android shared-media directory is unavailable; using the"
        << "application data fallback for" << mediaDirectoryName;
#endif

    return mediaKind == LocalMediaKind::Photo
        ? appSettings->photoSavePath()
        : appSettings->videoSavePath();
}

void registerLocalMediaFile(const QString& filePath)
{
#ifdef Q_OS_ANDROID
    const QFileInfo mediaFile(filePath);
    const QString mimeType = localMediaMimeType(filePath);
    if (!mediaFile.isFile()
        || mediaFile.size() <= 0
        || mimeType.isEmpty()) {
        qCWarning(GimbalControlLog)
            << "Skipping Android media scan for invalid local media"
            << filePath;
        return;
    }

    (void) AndroidMediaLibrary::registerMediaFile(
        mediaFile.absoluteFilePath(),
        mimeType);
#else
    Q_UNUSED(filePath);
#endif
}

void registerExistingLocalMediaFiles()
{
#ifdef Q_OS_ANDROID
    AppSettings* appSettings = SettingsManager::instance()->appSettings();
    if (!appSettings) {
        return;
    }

    struct MediaDirectory {
        QString targetPath;
        QString legacyPath;
        QStringList nameFilters;
        QRegularExpression customFilePattern;
    };

    const QList<MediaDirectory> mediaDirectories = {
        {
            localMediaSaveDirectory(appSettings, LocalMediaKind::Photo),
            appSettings->photoSavePath(),
            {QStringLiteral("*.jpg"), QStringLiteral("*.jpeg")},
            QRegularExpression(
                QStringLiteral(R"(_local_\d{3,}\.(?:jpg|jpeg)$)"),
                QRegularExpression::CaseInsensitiveOption),
        },
        {
            localMediaSaveDirectory(appSettings, LocalMediaKind::Video),
            appSettings->videoSavePath(),
            {
                QStringLiteral("*.mkv"),
                QStringLiteral("*.mov"),
                QStringLiteral("*.mp4"),
            },
            QRegularExpression(
                QStringLiteral(R"(_local_\d{3,}\.(?:mkv|mov|mp4)$)"),
                QRegularExpression::CaseInsensitiveOption),
        },
    };

    for (const MediaDirectory& mediaDirectory : mediaDirectories) {
        if (mediaDirectory.targetPath.isEmpty()) {
            continue;
        }

        const auto matchingMediaFiles = [&mediaDirectory](
                                            const QString& path) {
            QDir directory(path);
            if (!directory.exists()) {
                return QFileInfoList();
            }

            directory.setFilter(QDir::Files
                                | QDir::Readable
                                | QDir::NoSymLinks);
            directory.setNameFilters(mediaDirectory.nameFilters);
            QFileInfoList matchingFiles;
            const QFileInfoList mediaFiles = directory.entryInfoList();
            for (const QFileInfo& mediaFile : mediaFiles) {
                if (mediaDirectory.customFilePattern
                        .match(mediaFile.fileName())
                        .hasMatch()) {
                    matchingFiles.append(mediaFile);
                }
            }
            return matchingFiles;
        };

        const QFileInfoList targetFiles =
            matchingMediaFiles(mediaDirectory.targetPath);
        for (const QFileInfo& targetFile : targetFiles) {
            registerLocalMediaFile(targetFile.absoluteFilePath());
        }

        if (mediaDirectory.legacyPath.isEmpty()) {
            continue;
        }

        const QString cleanTargetPath =
            QDir::cleanPath(mediaDirectory.targetPath);
        const QString cleanLegacyPath =
            QDir::cleanPath(mediaDirectory.legacyPath);
        if (cleanLegacyPath == cleanTargetPath) {
            continue;
        }

        const QFileInfoList legacyFiles =
            matchingMediaFiles(mediaDirectory.legacyPath);
        for (const QFileInfo& legacyFile : legacyFiles) {
            (void) AndroidMediaLibrary::migrateMediaFile(
                legacyFile.absoluteFilePath(),
                mediaDirectory.targetPath,
                localMediaMimeType(legacyFile.absoluteFilePath()));
        }
    }
#endif
}

bool removeLocalMediaFile(const QString& filePath)
{
#ifdef Q_OS_ANDROID
    return AndroidMediaLibrary::removeMediaFile(filePath);
#else
    return QFile::remove(filePath);
#endif
}

void cleanupOldLocalVideos(VideoSettings* videoSettings,
                           const QString& savePath)
{
    if (!videoSettings
        || !videoSettings->enableStorageLimit()->rawValue().toBool()) {
        return;
    }

    QDir videoDirectory(savePath);
    videoDirectory.setFilter(QDir::Files
                             | QDir::Readable
                             | QDir::NoSymLinks
                             | QDir::Writable);
    videoDirectory.setSorting(QDir::Time);
    videoDirectory.setNameFilters({
        QStringLiteral("*.mkv"),
        QStringLiteral("*.mov"),
        QStringLiteral("*.mp4"),
    });

    const QFileInfoList videos = videoDirectory.entryInfoList();
    QFileInfoList removableVideos;
    static const QRegularExpression localSegmentPattern(
        QStringLiteral(R"(_local_\d{3,}\.(?:mkv|mov|mp4)$)"),
        QRegularExpression::CaseInsensitiveOption);
    quint64 totalBytes = 0;
    for (const QFileInfo& video : std::as_const(videos)) {
        totalBytes += static_cast<quint64>(video.size());
        if (localSegmentPattern.match(video.fileName()).hasMatch()) {
            removableVideos.append(video);
        }
    }

    const quint64 maximumBytes =
        videoSettings->maxVideoSize()->rawValue().toULongLong()
        * 1024ULL
        * 1024ULL;
    while (totalBytes >= maximumBytes && !removableVideos.isEmpty()) {
        const QFileInfo oldestVideo = removableVideos.takeLast();
        qCInfo(GimbalControlLog)
            << "Removing old local video" << oldestVideo.filePath();
        if (removeLocalMediaFile(oldestVideo.filePath())) {
            totalBytes -= static_cast<quint64>(oldestVideo.size());
        } else {
            qCWarning(GimbalControlLog)
                << "Failed to remove old local video"
                << oldestVideo.filePath();
            // The total did not decrease. Stop here instead of deleting more
            // segments than the configured limit requires.
            break;
        }
    }

    if (totalBytes >= maximumBytes) {
        // Other QGC entry points (including thermal recording) can share the
        // directory. Never unlink their files merely to satisfy this custom
        // branch's storage limit; an active file may still be in use.
        qCWarning(GimbalControlLog)
            << "Local video storage remains above its configured limit;"
            << "no more custom local segments are safe to remove";
    }
}

} // namespace

GimbalControlManager::GimbalControlManager(GimbalControlSettings* settings, QObject* parent)
    : QObject(parent)
    , _settings(settings)
    , _sdk(new SiyiSdk(this))
    , _videoManager(VideoManager::instance())
{
    Q_CHECK_PTR(_settings);
    _sdk->setZoomRange(kMinZoom, kProtocolMaxZoom);

    _sdkResponseTimer.setSingleShot(true);
    // 倍率和相机状态每 2 秒轮询一次；响应超时必须短于轮询周期。
    _sdkResponseTimer.setInterval(1500);
    _zoomOperationTimer.setSingleShot(true);
    // 这是完整缩放操作的截止时间，不是单个UDP查询的超时。绝对缩放可能
    // 需要数秒才能到达目标，不能在中途清除pending并把1.8x等过程值当结果。
    _zoomOperationTimer.setInterval(10000);
    _zoomQueryTimeoutTimer.setSingleShot(true);
    _zoomQueryTimeoutTimer.setInterval(kDefaultZoomQueryTimeoutMs);
    _sdkPollTimer.setInterval(2000);
    _recordingCapabilityTimeoutTimer.setSingleShot(true);
    _recordingCapabilityTimeoutTimer.setInterval(
        kRecordingCapabilityTimeoutMs);
    _continuousZoomWatchdog.setSingleShot(true);
    // Held zoom uses one native 0x05 run. Sixty seconds is only a guard for a
    // lost release/cancel event and always sends a real stop when it expires.
    _continuousZoomWatchdog.setInterval(60000);
    _continuousZoomStepTimer.setSingleShot(true);
    _continuousZoomStepTimer.setInterval(kManualZoomPollIntervalMs);
    _manualZoomStopRetryTimer.setSingleShot(true);
    _manualZoomStopRetryTimer.setInterval(kManualZoomStopRetryMs);
    _manualZoomFinalizeTimer.setSingleShot(true);
    _manualZoomFinalizeTimer.setInterval(kManualZoomFinalizeTimeoutMs);
    _zoomSyncTimer.setSingleShot(true);
    _zoomSyncTimer.setInterval(350);
    _pulledVideoFallbackTimer.setSingleShot(true);
    // 直接sink观察器优先；仅当VideoManager尺寸持续稳定1秒时才允许兜底。
    _pulledVideoFallbackTimer.setInterval(1000);
    _photoFeedbackTimer.setSingleShot(true);
    _photoFeedbackTimer.setInterval(2000);
    _recordingStatusDelayTimer.setSingleShot(true);
    _recordingStatusDelayTimer.setInterval(400);
    _recordingCommandTimeoutTimer.setSingleShot(true);
    _recordingCommandTimeoutTimer.setInterval(2500);
    _localPhotoCaptureTimer.setSingleShot(true);
    _localPhotoCaptureTimer.setInterval(kLocalPhotoGrabTimeoutMs);
    _localPhotoSaveThreadPool.setMaxThreadCount(1);
    _localPhotoSaveThreadPool.setExpiryTimeout(10000);
    _localRecordingStartTimer.setSingleShot(true);
    _localRecordingStartTimer.setInterval(kLocalRecordingStartTimeoutMs);
    _localRecordingStopTimer.setSingleShot(true);
    _localRecordingStopTimer.setInterval(kLocalRecordingStopTimeoutMs);

    connect(_sdk,
            &SiyiSdk::absoluteZoomFeedbackReceived,
            this,
            &GimbalControlManager::_handleAbsoluteZoomFeedback);
    connect(_sdk,
            &SiyiSdk::manualZoomReceived,
            this,
            &GimbalControlManager::_handleManualZoomFeedback);
    connect(_sdk, &SiyiSdk::maximumZoomReceived, this, &GimbalControlManager::_handleMaximumZoom);
    connect(_sdk,
            &SiyiSdk::recordingStreamParametersReceived,
            this,
            &GimbalControlManager::_handleRecordingStreamParameters);
    connect(_sdk, &SiyiSdk::currentZoomReceived, this, &GimbalControlManager::_handleCurrentZoom);
    connect(_sdk, &SiyiSdk::cameraSystemStatusReceived, this, &GimbalControlManager::_handleCameraSystemStatus);
    connect(_sdk, &SiyiSdk::functionFeedbackReceived, this, &GimbalControlManager::_handleFunctionFeedback);
    connect(_sdk, &SiyiSdk::packetReceived, this, [this]() {
        // 设置关闭后仍可能收到关闭前请求的迟到UDP回包，不能让它重新点亮在线状态。
        if (!enabled()) {
            return;
        }
        const bool wasResponding = _sdkResponding;
        _sdkResponseTimer.stop();
        _setSdkResponding(true);
        if (!wasResponding) {
            _setLastError(QString());
        }
    });
    connect(_sdk, &SiyiSdk::communicationError, this, &GimbalControlManager::_handleCommunicationError);
    connect(&_sdkResponseTimer, &QTimer::timeout, this, &GimbalControlManager::_markSdkNotResponding);
    connect(&_zoomOperationTimer, &QTimer::timeout, this, &GimbalControlManager::_markZoomStatusUnknown);
    connect(&_zoomQueryTimeoutTimer,
            &QTimer::timeout,
            this,
            &GimbalControlManager::_handleZoomQueryTimeout);
    connect(&_sdkPollTimer, &QTimer::timeout, this, &GimbalControlManager::_pollSdk);
    connect(&_recordingCapabilityTimeoutTimer,
            &QTimer::timeout,
            this,
            &GimbalControlManager::_expireRecordingResolutionCapability);
    connect(&_zoomSyncTimer, &QTimer::timeout, this, &GimbalControlManager::_requestZoomAfterSettle);
    connect(&_pulledVideoFallbackTimer,
            &QTimer::timeout,
            this,
            &GimbalControlManager::_tryConfirmPulledVideoResolutionFallback);
    connect(&_continuousZoomStepTimer,
            &QTimer::timeout,
            this,
            &GimbalControlManager::_pollContinuousZoom);
    connect(&_manualZoomStopRetryTimer,
            &QTimer::timeout,
            this,
            &GimbalControlManager::_retryManualZoomStop);
    connect(&_manualZoomFinalizeTimer,
            &QTimer::timeout,
            this,
            &GimbalControlManager::_expireManualZoomFinalize);
    connect(&_continuousZoomWatchdog, &QTimer::timeout, this, &GimbalControlManager::_stopContinuousZoomForSafety);
    connect(&_photoFeedbackTimer, &QTimer::timeout, this, [this]() { _photoCommandPending = false; });
    connect(&_recordingStatusDelayTimer, &QTimer::timeout, this, &GimbalControlManager::_requestRecordingStatusAfterDelay);
    connect(&_recordingCommandTimeoutTimer, &QTimer::timeout, this, &GimbalControlManager::_handleRecordingCommandTimeout);
    connect(&_localPhotoCaptureTimer,
            &QTimer::timeout,
            this,
            [this]() {
                if (!_localPhotoCapturePending) {
                    return;
                }

                ++_localPhotoRequestSequence;
                _localPhotoGrabLifetime.clear();
                _localPhotoCapturePending = false;
                _setLocalMediaError(
                    tr("Failed to capture the local video frame."));
                qCWarning(GimbalControlLog)
                    << "Timed out waiting for the local camera-frame grab";
            });
    connect(&_localRecordingStartTimer,
            &QTimer::timeout,
            this,
            &GimbalControlManager::_handleLocalRecordingStartTimeout);
    connect(&_localRecordingStopTimer,
            &QTimer::timeout,
            this,
            &GimbalControlManager::_handleLocalRecordingStopTimeout);

    if (_videoManager) {
        connect(_videoManager,
                &VideoManager::videoSizeChanged,
                this,
                &GimbalControlManager::_handlePulledVideoSize);
        connect(_videoManager,
                &VideoManager::decodingChanged,
                this,
                &GimbalControlManager::_handleVideoDecodingChanged);
        connect(_videoManager,
                &VideoManager::streamingChanged,
                this,
                &GimbalControlManager::_handleVideoStreamingChanged);
        connect(_videoManager,
                &VideoManager::recordingChanged,
                this,
                &GimbalControlManager::_handleVideoRecordingChanged);
        _tryConfirmPulledVideoResolution();
        _schedulePulledVideoResolutionFallback();
    }

    connect(_settings->enabled(), &Fact::rawValueChanged, this, &GimbalControlManager::_settingsChanged);
    connect(_settings->sdkHost(), &Fact::rawValueChanged, this, &GimbalControlManager::_settingsChanged);
    connect(_settings->sdkPort(), &Fact::rawValueChanged, this, &GimbalControlManager::_settingsChanged);
    connect(_settings->zoomStep(),
            &Fact::rawValueChanged,
            this,
            &GimbalControlManager::_handleZoomStepChanged);
    connect(_settings->localMediaStorageEnabled(),
            &Fact::rawValueChanged,
            this,
            &GimbalControlManager::_handleLocalMediaStorageEnabledChanged);

    _lastEnabled = enabled();
    _configureSdkEndpoint();
    if (_lastEnabled) {
        _sdkPollTimer.start();
        _pollSdk();
    }

    // Queue this after plugin construction so QGCActivity and all application
    // save paths are fully initialized. This also upgrades media written by a
    // build which did not notify Android's gallery index.
    QTimer::singleShot(0, this, []() {
        registerExistingLocalMediaFiles();
    });
}

GimbalControlManager::~GimbalControlManager()
{
    _localPhotoCaptureTimer.stop();
    ++_localPhotoRequestSequence;
    // Do not destroy a QQuickItemGrabResult here: its render-thread callback
    // may still be running. The QQuickWindow-owned lifetime holder retires it.
    _localPhotoGrabLifetime.clear();
    _localPhotoCapturePending = false;
    _localPhotoSaveThreadPool.waitForDone();

    shutdownLocalMedia();
    if (_continuousZoomActive && _sdk) {
        _stopContinuousZoom(false);
    }
    // Exhaust the small, stop-only retry budget synchronously while the SDK
    // transport is still alive. A retry timer cannot fire after destruction.
    for (int attempt = 0;
         attempt < kManualZoomStopMaximumRetryAttempts;
         ++attempt) {
        if (_flushPendingManualZoomStop()) {
            break;
        }
    }
}

bool GimbalControlManager::enabled() const
{
    return _settings && _settings->enabled()->rawValue().toBool();
}

bool GimbalControlManager::localMediaStorageEnabled() const
{
    return _settings
        && _settings->localMediaStorageEnabled()->rawValue().toBool();
}

bool GimbalControlManager::cameraRecordingPending() const
{
    if (_recordingCommandPending) {
        return true;
    }
    if (!_cameraRecordingIntentValid || _cameraStatusKnown) {
        return false;
    }

    return _cameraRecordingIntentTarget
        ? !_cameraRecordingStartBlocked
        : !_cameraRecordingStopBlocked;
}

bool GimbalControlManager::recordingSessionActive() const
{
    return _recordingSessionRequested
        || _recording
        || (_cameraRecordingIntentValid
            && !_cameraRecordingIntentTarget)
        || _localRecordingActive
        || _localRecordingStartPending
        || _localRecordingStopPending;
}

bool GimbalControlManager::recordingSessionCapturing() const
{
    // SIYI 0x0c is a toggle without an ACK. _recording is updated
    // optimistically while its command is pending, so only expose that branch
    // as actually capturing after 0x0a confirms it.
    return GimbalMediaSessionPolicy::recordingSessionCapturing(
        _recording,
        _recordingCommandPending,
        _localRecordingActive);
}

bool GimbalControlManager::videoRecordingAvailable() const
{
    return GimbalMediaSessionPolicy::recordingAvailable(
        recordingSessionActive(),
        _recordingCommandPending,
        _localRecordingStartPending || _localRecordingStopPending,
        localMediaStorageEnabled(),
        _videoManager && _videoManager->streaming(),
        enabled(),
        _cameraStatusKnown);
}

double GimbalControlManager::zoomStep() const
{
    if (!_settings) {
        return 1.0;
    }
    // 0x0f和0x18都只有一位小数精度。设置值先量化到0.1x，使放大和缩小
    // 共用的最小值锚定目标表以及精确分辨率端点都可表示。
    return qRound(qBound(0.1,
                         _settings->zoomStep()->rawValue().toDouble(),
                         kProtocolMaxZoom - kMinZoom)
                  * 10.0)
        / 10.0;
}

bool GimbalControlManager::zoomInAvailable() const
{
    return _zoomDirectionAvailable(1);
}

bool GimbalControlManager::zoomOutAvailable() const
{
    return _zoomDirectionAvailable(-1);
}

bool GimbalControlManager::zoomIn()
{
    return _sendZoomStep(1);
}

bool GimbalControlManager::zoomOut()
{
    return _sendZoomStep(-1);
}

bool GimbalControlManager::_zoomPlanningReference(double* zoomLevel) const
{
    if (!zoomLevel) {
        return false;
    }

    if (_absoluteZoomPending) {
        *zoomLevel = _requestedZoom;
        return true;
    }
    if (_zoomValueUncertain && _latestActualZoomKnown) {
        *zoomLevel = _latestActualZoom;
        return true;
    }
    if (_zoomStatusKnown) {
        *zoomLevel = _currentZoom;
        return true;
    }
    if (_latestActualZoomKnown) {
        *zoomLevel = _latestActualZoom;
        return true;
    }
    return false;
}

bool GimbalControlManager::_zoomDirectionAvailable(int direction) const
{
    if (!zoomControlsUnlocked() || (direction != -1 && direction != 1)) {
        return false;
    }

    double plannedZoom = kMinZoom;
    if (!_zoomPlanningReference(&plannedZoom)) {
        // The first 0x18 reply may arrive shortly after the decoded stream.
        // Keep the controls visibly usable. The command handler still requires
        // an actual planning reference and never stores the gesture for replay.
        return _maximumZoom > kMinZoom + kZoomComparisonTolerance;
    }

    double targetZoom = 0.0;
    return A8MiniZoomPolicy::stepTarget(plannedZoom,
                                        zoomStep(),
                                        kMinZoom,
                                        _maximumZoom,
                                        direction,
                                        &targetZoom);
}

bool GimbalControlManager::_sendZoomStep(int direction)
{
    if (!_cameraCommandAvailable()) {
        return false;
    }
    if (!zoomControlsUnlocked()) {
        _setLastError(
            tr("Waiting for a supported pulled-video session and a valid "
               "SIYI recording-stream resolution before controlling zoom."));
        return false;
    }
    if (_continuousZoomActive) {
        // A tap is a newer explicit gesture. Stop the elapsed-time target
        // sequence immediately; never let it advance behind the tap.
        qCWarning(GimbalControlLog)
            << "Stopping residual continuous SIYI zoom before accepting tap direction"
            << direction;
        if (!_stopContinuousZoom(false)) {
            return false;
        }
    }
    // If release scheduled a delayed safety-stop copy, send it now before the
    // newer absolute command. A stop packet must never arrive after that command.
    if (!_flushPendingManualZoomStop()) {
        return false;
    }
    _cancelManualZoomFinalize();
    if (!_zoomDirectionAvailable(direction)) {
        _setLastError(direction > 0
                          ? tr("The SIYI camera is already at its valid zoom-in boundary.")
                          : tr("The SIYI camera is already at its valid zoom-out boundary."));
        return false;
    }

    double referenceZoom = kMinZoom;
    if (!_zoomPlanningReference(&referenceZoom)) {
        _setLastError(tr("Waiting for the current SIYI camera zoom value."));
        return false;
    }
    double targetZoom = 0.0;
    if (!A8MiniZoomPolicy::stepTarget(referenceZoom,
                                      zoomStep(),
                                      kMinZoom,
                                      _maximumZoom,
                                      direction,
                                      &targetZoom)) {
        _setLastError(direction > 0
                          ? tr("The SIYI camera is already at its valid zoom-in boundary.")
                          : tr("The SIYI camera is already at its valid zoom-out boundary."));
        return false;
    }

    // A tap is never retained for later replay. If another absolute target is
    // still pending, this command replaces it now and is planned from the last
    // legal requested stop. Rapid taps therefore update the latest desired
    // target immediately instead of building a seconds-long FIFO.
    const bool replacePendingTarget = _absoluteZoomPending;
    if (!_sendAbsoluteZoomTarget(targetZoom,
                                 false,
                                 replacePendingTarget)) {
        return false;
    }
    // Only an accepted newer explicit absolute gesture releases a target
    // retained by an earlier native hold. A failed send must leave the safety
    // latch intact.
    _nativeHoldTargetLatched = false;
    qCInfo(GimbalControlLog)
        << "Applied immediate SIYI tap direction" << direction
        << "reference" << referenceZoom
        << "target" << targetZoom
        << "replaced pending target" << replacePendingTarget;
    _setLastError(QString());
    return true;
}

bool GimbalControlManager::_advanceHeldZoomDisplayTarget()
{
    if (!_continuousZoomActive || !_heldZoomElapsed.isValid()) {
        return true;
    }

    const qint64 totalPressMs64 =
        static_cast<qint64>(_heldZoomInitialPressDurationMs)
        + _heldZoomElapsed.elapsed();
    const int totalPressMs = static_cast<int>(
        qMin<qint64>(totalPressMs64, std::numeric_limits<int>::max()));

    double timedTarget = _heldZoomLastTarget;
    if (!A8MiniZoomPolicy::heldTarget(_heldZoomStartTarget,
                                      _continuousZoomDirection,
                                      totalPressMs,
                                      zoomStep(),
                                      kMinZoom,
                                      _maximumZoom,
                                      kHeldZoomStepPeriodMs,
                                      &timedTarget)) {
        return true;
    }

    const bool movesFarther =
        (_continuousZoomDirection > 0
         && timedTarget
             > _heldZoomLastTarget + kZoomComparisonTolerance)
        || (_continuousZoomDirection < 0
            && timedTarget
                < _heldZoomLastTarget - kZoomComparisonTolerance);
    if (!movesFarther) {
        return true;
    }

    // Native 0x05 owns the physical motion for the whole hold. Advancing the
    // legal target shown to the operator must not restart the camera's zoom
    // controller with another 0x0f packet.
    _heldZoomLastTarget = timedTarget;
    _requestedZoom = timedTarget;
    _setCurrentZoom(timedTarget);
    _setZoomStatusKnown(true);
    _setZoomValueUncertain(false);
    _suppressIdleAlignmentUntilExplicitZoom = true;
    emit zoomAvailabilityChanged();
    return true;
}

bool GimbalControlManager::_heldZoomDisplayAtTerminal() const
{
    if (!_continuousZoomActive) {
        return false;
    }
    if (_continuousZoomDirection > 0) {
        return _heldZoomLastTarget
            >= _maximumZoom - kZoomComparisonTolerance;
    }
    if (_continuousZoomDirection < 0) {
        return _heldZoomLastTarget
            <= kMinZoom + kZoomComparisonTolerance;
    }
    return false;
}

bool GimbalControlManager::setZoom(double zoomLevel)
{
    if (!_cameraCommandAvailable()) {
        return false;
    }
    if (!zoomControlsUnlocked()) {
        _setLastError(
            tr("Waiting for a supported pulled-video session and a valid "
               "SIYI recording-stream resolution before controlling zoom."));
        return false;
    }
    if (!_zoomStatusKnown || _stableZoomConfirmationPending) {
        _setLastError(tr("Waiting for a stable SIYI camera zoom value."));
        return false;
    }
    if (!qIsFinite(zoomLevel)) {
        _setLastError(tr("Invalid SIYI camera zoom value."));
        return false;
    }

    // 协议只传一位小数；先完成无副作用的范围校验。公开Q_INVOKABLE即使
    // 被直接传入越界值，也不能先停止长按重复或破坏正在进行的确认状态。
    const double targetZoom = qRound(zoomLevel * 10.0) / 10.0;
    if (targetZoom < kMinZoom - kZoomComparisonTolerance
        || targetZoom > _maximumZoom + kZoomComparisonTolerance) {
        _setLastError(tr("The requested SIYI camera zoom is outside the effective recording-mode limit."));
        return false;
    }
    const double boundedTargetZoom = qBound(kMinZoom, targetZoom, _maximumZoom);
    if (!A8MiniZoomPolicy::isAlignedZoom(boundedTargetZoom,
                                         zoomStep(),
                                         kMinZoom,
                                         _maximumZoom)) {
        _setLastError(tr("The requested SIYI camera zoom is not aligned to the configured zoom step."));
        return false;
    }

    // An explicit absolute value replaces any earlier in-flight target now.
    // No previous gesture is retained for delayed replay.
    if (_continuousZoomActive) {
        if (!_stopContinuousZoom(false)) {
            return false;
        }
    }
    if (!_flushPendingManualZoomStop()) {
        return false;
    }
    _cancelManualZoomFinalize();
    if (!_sendAbsoluteZoomTarget(boundedTargetZoom,
                                 false,
                                 _absoluteZoomPending)) {
        return false;
    }
    _nativeHoldTargetLatched = false;

    _setLastError(QString());
    return true;
}

bool GimbalControlManager::startZoom(int direction)
{
    return startZoomWithPressDuration(direction,
                                      kHeldZoomPressThresholdMs);
}

bool GimbalControlManager::startZoomWithPressDuration(
    int direction,
    int pressDurationMs)
{
    if (!_cameraCommandAvailable()) {
        return false;
    }

    const int normalizedDirection = direction > 0 ? 1 : (direction < 0 ? -1 : 0);
    if (normalizedDirection == 0) {
        _setLastError(tr("Held zoom direction must be -1 or 1."));
        return false;
    }

    if (_continuousZoomActive) {
        if (_continuousZoomDirection == normalizedDirection) {
            _continuousZoomWatchdog.start();
            return true;
        }
        _setLastError(tr("Release the current zoom direction before reversing it."));
        return false;
    }

    if (!zoomControlsUnlocked()) {
        _setLastError(
            tr("Waiting for a supported pulled-video session and a valid "
               "SIYI recording-stream resolution before controlling zoom."));
        return false;
    }

    // A held gesture supersedes every older release transaction. Complete a
    // retained safety-stop copy before starting a new native 0x05 run so that
    // no delayed stop can terminate the new gesture.
    if (!_flushPendingManualZoomStop()) {
        return false;
    }
    _cancelManualZoomFinalize();
    if (!_zoomDirectionAvailable(normalizedDirection)) {
        _setLastError(tr("No further SIYI zoom target remains in this direction."));
        return false;
    }

    double referenceZoom = kMinZoom;
    if (!_zoomPlanningReference(&referenceZoom)) {
        _setLastError(tr("Waiting for a valid SIYI zoom planning reference."));
        return false;
    }

    double firstTarget = kMinZoom;
    const int boundedPressDurationMs =
        qMax(kHeldZoomPressThresholdMs, pressDurationMs);
    if (!A8MiniZoomPolicy::heldTarget(referenceZoom,
                                      normalizedDirection,
                                      boundedPressDurationMs,
                                      zoomStep(),
                                      kMinZoom,
                                      _maximumZoom,
                                      kHeldZoomStepPeriodMs,
                                      &firstTarget)) {
        _setLastError(tr("No further SIYI zoom target remains in this direction."));
        return false;
    }

    const bool firstTargetIsTerminal =
        (normalizedDirection > 0
         && firstTarget >= _maximumZoom - kZoomComparisonTolerance)
        || (normalizedDirection < 0
            && firstTarget <= kMinZoom + kZoomComparisonTolerance);
    if (firstTargetIsTerminal) {
        // When only the final legal interval remains, starting free-running
        // 0x05 and stopping it one timer tick later cannot reliably reach the
        // endpoint. A single absolute endpoint command is monotonic from the
        // current legal reference and cannot overshoot then reverse.
        if (!_sendAbsoluteZoomTarget(firstTarget,
                                     false,
                                     _absoluteZoomPending)) {
            return false;
        }
        _nativeHoldTargetLatched = true;
        _setLastError(QString());
        qCInfo(GimbalControlLog)
            << "Applied terminal SIYI held target with one absolute command"
            << "direction" << normalizedDirection
            << "origin" << referenceZoom << "target" << firstTarget;
        return true;
    }

    // Long press uses the camera's native continuous command exactly once.
    // Repeated 0x0f targets make the A8 Mini stop/restart at every legal step
    // and are visibly jerky. Latch the endpoint so release can always send its
    // stop to the same camera even if settings change during the gesture.
    const QString sessionHost =
        _settings->sdkHost()->rawValue().toString().trimmed();
    const quint16 sessionPort = _sdkPort();

    _continuousZoomStepTimer.stop();
    _zoomSyncTimer.stop();
    _zoomOperationTimer.stop();
    _cancelOutstandingZoomQuery();
    _zoomResponseBlocked = false;
    _absoluteZoomPending = false;
    _absoluteZoomTracker.clear();
    _alignmentAttemptCount = 0;
    _clearAutomaticAlignmentSuppression();
    _clearStableZoomConfirmation();

    _manualZoomStopRetryTimer.stop();
    _manualZoomStopRetryAttemptsRemaining = 0;
    _manualZoomSessionHost = sessionHost;
    _manualZoomSessionPort = sessionPort;
    if (!_sdk->sendManualZoomTo(
            static_cast<qint8>(normalizedDirection),
            sessionHost,
            sessionPort)) {
        _manualZoomSessionHost.clear();
        _manualZoomSessionPort = 0;
        return false;
    }
    _nativeHoldTargetLatched = true;
    _nativeHoldFeedbackLogPending = false;

    // The gesture origin never changes. QML supplies the duration already
    // elapsed since the physical press, and every timer tick adds subsequent
    // elapsed time. Delayed callbacks therefore keep the correct grid phase,
    // while the physical camera motion remains one uninterrupted 0x05 run.
    _heldZoomStartTarget = referenceZoom;
    _heldZoomLastTarget = firstTarget;
    _heldZoomInitialPressDurationMs = boundedPressDurationMs;
    _heldZoomElapsed.start();
    _requestedZoom = firstTarget;
    _setCurrentZoom(firstTarget);
    _setZoomStatusKnown(true);
    _setZoomValueUncertain(false);
    _suppressIdleAlignmentUntilExplicitZoom = true;
    _setContinuousZoomState(true, normalizedDirection);
    _continuousZoomWatchdog.start();
    _continuousZoomStepTimer.start(kManualZoomPollIntervalMs);
    _sdkResponseTimer.start();
    emit zoomAvailabilityChanged();
    _setLastError(QString());
    qCInfo(GimbalControlLog)
        << "Started native continuous SIYI zoom direction"
        << normalizedDirection
        << "origin" << referenceZoom << "first target" << firstTarget;
    return true;
}

bool GimbalControlManager::stopZoom()
{
    // A normal release performs one final duration calculation so an event
    // arriving on a 600 ms boundary is not lost between timer ticks.
    return _stopContinuousZoom(true);
}

bool GimbalControlManager::cancelZoom()
{
    // Cancellation (pointer leave, application background, hidden control)
    // does not advance the timed target, but it must stop native 0x05 motion.
    return _stopContinuousZoom(false);
}

bool GimbalControlManager::takePhoto()
{
    // The two destinations are deliberately independent: a missing gimbal SD
    // card or an SDK send failure must never suppress the local frame capture.
    const bool localCaptureStarted = localMediaStorageEnabled()
        ? _captureLocalVideoFrame()
        : false;

    bool cameraCommandSent = false;
    if (_cameraCommandAvailable() && !_photoCommandPending) {
        _configureSdkEndpoint();
        cameraCommandSent = _sdk->takePhoto();
        if (cameraCommandSent) {
            _photoCommandPending = true;
            _photoFeedbackTimer.start();
            _setLastError(QString());
        }
    }

    return localCaptureStarted || cameraCommandSent;
}

bool GimbalControlManager::toggleVideoRecording()
{
    if (_recordingCommandPending
        || _localRecordingStartPending
        || _localRecordingStopPending) {
        return false;
    }

    return recordingSessionActive()
        ? _stopRecordingSession()
        : _startRecordingSession();
}

bool GimbalControlManager::requestCurrentZoom()
{
    if (!enabled()
        || _continuousZoomActive
        || _manualZoomFinalizePending
        || _absoluteZoomPending
        || _stableZoomConfirmationPending
        || _zoomResponseBlocked
        || _zoomQueryOutstanding) {
        return false;
    }

    return _sendCurrentZoomQuery(true);
}

bool GimbalControlManager::requestCameraStatus()
{
    if (!enabled()) {
        return false;
    }

    _configureSdkEndpoint();
    _sdkResponseTimer.start();
    return _sdk->requestCameraSystemStatus();
}

void GimbalControlManager::_settingsChanged()
{
    const bool nowEnabled = enabled();

    // Stop native held motion at its latched endpoint before applying endpoint
    // or enabled-state changes.
    if (_continuousZoomActive) {
        _stopContinuousZoom(false);
    }
    _cancelManualZoomFinalize();

    _continuousZoomWatchdog.stop();
    _zoomSyncTimer.stop();
    _zoomOperationTimer.stop();
    _cancelOutstandingZoomQuery();
    _zoomResponseBlocked = false;
    _absoluteZoomPending = false;
    _absoluteZoomTracker.clear();
    _latestActualZoomKnown = false;
    _alignmentAttemptCount = 0;
    _clearAutomaticAlignmentSuppression();
    _suppressIdleAlignmentUntilExplicitZoom = false;
    _clearStableZoomConfirmation();
    _requestedZoom = kMinZoom;
    _resetMaximumZoomCapability();
    _photoFeedbackTimer.stop();
    _photoCommandPending = false;
    _finishRecordingCommand();
    _sdkResponseTimer.stop();
    _sdkPollTimer.stop();
    _setSdkResponding(false);
    _setZoomStatusKnown(false);
    _setCameraStatusKnown(false);
    _setRecording(false);
    _setLastError(QString());
    _configureSdkEndpoint();

    if (_lastEnabled != nowEnabled) {
        _lastEnabled = nowEnabled;
        emit enabledChanged();
        emit zoomAvailabilityChanged();
    }

    if (!nowEnabled) {
        _recordingSessionRequested = false;
        _cameraRecordingIntentValid = false;
        _cameraRecordingStartBlocked = false;
        _cameraRecordingStopBlocked = false;
        _localRecordingIntent = false;
        _localRecordingResumeOnStream = false;
        _localRecordingRetryAfterGenerationResolved = false;
        _reconcileLocalRecording();
    } else if (_recordingSessionRequested) {
        _cameraRecordingIntentValid = true;
        _cameraRecordingIntentTarget = true;
        _cameraRecordingStartBlocked = false;
        _cameraRecordingStopBlocked = false;
    }

    if (nowEnabled) {
        _sdkPollTimer.start();
        _pollSdk();
        _reconcileCameraRecordingIntent();
    }
    _notifyRecordingSessionStateChanged();
}

void GimbalControlManager::_handleZoomStepChanged()
{
    emit zoomStepChanged();
    _clearAutomaticAlignmentSuppression();
    _suppressIdleAlignmentUntilExplicitZoom = false;

    if (_continuousZoomActive) {
        _stopContinuousZoom(false);
    }
    _cancelManualZoomFinalize();
    if (_maximumZoomKnown) {
        _refreshMaximumZoomCapability();
    } else {
        emit zoomAvailabilityChanged();
    }

    // If the setting changes while an absolute command is still in flight,
    // the previously confirmed display may no longer belong to the new grid.
    // Hide it immediately; pending remains independent from stream unlock.
    if (_zoomStatusKnown
        && _maximumZoomKnown
        && !A8MiniZoomPolicy::isAlignedZoom(_currentZoom,
                                             zoomStep(),
                                             kMinZoom,
                                             _maximumZoom)) {
        _setZoomStatusKnown(false);
    }
    if (!enabled()
        || !_maximumZoomKnown
        || _absoluteZoomPending
        || _stableZoomConfirmationPending) {
        return;
    }

    // 步长改变后，旧倍率可能不在新网格上。重新读取两次稳定值并在必要时
    // 用0x0f归整到完整分度，确认完成前不公开旧倍率。
    _setZoomStatusKnown(false);
    _beginStableZoomConfirmation(true, 0);
    _cancelOutstandingZoomQuery();
    _zoomOperationTimer.start();
    _scheduleZoomSync();
}

void GimbalControlManager::_handleManualZoomFeedback(double zoomLevel)
{
    if (!enabled()
        || !qIsFinite(zoomLevel)
        || zoomLevel < kMinZoom
        || zoomLevel > kProtocolMaxZoom) {
        return;
    }

    _sdkResponseTimer.stop();
    _setSdkResponding(true);

    // 0x05 has a fixed sequence and its ACK contains only a raw zoom value.
    // A late start/stop ACK cannot be assigned to the current gesture, so it is
    // heartbeat evidence only. Only a solicited 0x18 response may drive state.
}

void GimbalControlManager::_handleAbsoluteZoomFeedback(bool accepted)
{
    if (!enabled() || !_absoluteZoomPending || accepted) {
        return;
    }

    // sequence固定为0，迟到ACK无法归属旧目标还是新目标。负ACK只作
    // 诊断，不能缩短当前目标的证据窗口；最终状态始终由0x18决定。
    qCWarning(GimbalControlLog)
        << "SIYI camera returned a negative absolute-zoom ACK;"
        << "continuing 0x18 confirmation for target" << _requestedZoom;
    _setLastError(tr("The SIYI camera reported a negative zoom acknowledgement; verifying the actual zoom value."));
    if (!_zoomQueryOutstanding && !_zoomResponseBlocked) {
        _scheduleZoomSync();
    }
}

void GimbalControlManager::_handleMaximumZoom(double maximumZoom)
{
    if (!enabled()
        || !qIsFinite(maximumZoom)
        || maximumZoom < kMinZoom
        || maximumZoom > kProtocolMaxZoom) {
        return;
    }

    const double normalizedMaximum = qRound(
        qBound(kMinZoom, maximumZoom, kProtocolMaxZoom) * 10.0) / 10.0;
    const bool deviceMaximumChanged = !_deviceMaximumZoomKnown
        || qAbs(_deviceMaximumZoom - normalizedMaximum)
            > kZoomComparisonTolerance;
    _deviceMaximumZoom = normalizedMaximum;
    _deviceMaximumZoomKnown = true;
    if (_recordingResolutionConfirmed) {
        // The recording resolution defines the model capability. 0x16 is an
        // independent device-side guard: a lower value is respected so that a
        // stale or transitioning camera mode cannot receive an unreachable
        // target, while a larger value can never expand the mapped mode.
        if (deviceMaximumChanged
            && qAbs(_deviceMaximumZoom - _recordingResolutionMaximumZoom)
                > kZoomComparisonTolerance) {
            qCWarning(GimbalControlLog)
                << "SIYI recording-resolution maximum"
                << _recordingResolutionMaximumZoom
                << "differs from device 0x16 maximum"
                << _deviceMaximumZoom;
        }
        _refreshMaximumZoomCapability();
    }
}

void GimbalControlManager::_handleRecordingStreamParameters(
    quint8 videoEncodingType,
    quint16 width,
    quint16 height,
    quint16 bitrateKbps,
    quint8 frameRate)
{
    if (!enabled()) {
        return;
    }

    const QSize recordingSize(width, height);
    double maximumZoom = 0.0;
    if (!A8MiniZoomPolicy::maximumZoomForRecordingResolution(
            width, height, &maximumZoom)) {
        if (_lastRejectedRecordingVideoSize != recordingSize) {
            _lastRejectedRecordingVideoSize = recordingSize;
            qCWarning(GimbalControlLog)
                << "Unsupported SIYI recording resolution"
                << width << "x" << height
                << "- zoom remains locked until a known recording mode is reported";
        }
        if (_continuousZoomActive) {
            _stopContinuousZoom(false);
        }
        _resetMaximumZoomCapability();
        return;
    }

    const bool capabilityChanged =
        !_recordingResolutionConfirmed
        || _recordingVideoSize != recordingSize
        || qAbs(_recordingResolutionMaximumZoom - maximumZoom)
            > kZoomComparisonTolerance;
    _recordingResolutionConfirmed = true;
    _recordingVideoSize = recordingSize;
    _recordingResolutionMaximumZoom = maximumZoom;
    _lastRejectedRecordingVideoSize = QSize();
    _recordingCapabilityTimeoutTimer.start();
    if (capabilityChanged
        && _deviceMaximumZoomKnown
        && qAbs(_deviceMaximumZoom - maximumZoom)
            > kZoomComparisonTolerance) {
        qCWarning(GimbalControlLog)
            << "SIYI recording-resolution maximum" << maximumZoom
            << "differs from device 0x16 maximum"
            << _deviceMaximumZoom;
    }
    _refreshMaximumZoomCapability();

    if (capabilityChanged) {
        qCInfo(GimbalControlLog)
            << "Updated SIYI recording-stream capability:"
            << width << "x" << height
            << "codec" << videoEncodingType
            << "bitrate kbps" << bitrateKbps
            << "fps" << frameRate
            << "mapped maximum" << maximumZoom
            << "device maximum"
            << (_deviceMaximumZoomKnown ? _deviceMaximumZoom : -1.0)
            << "effective terminal maximum" << _maximumZoom;
    }
}

void GimbalControlManager::_expireRecordingResolutionCapability()
{
    if (!enabled() || !_recordingResolutionConfirmed) {
        return;
    }

    qCWarning(GimbalControlLog)
        << "SIYI recording-stream parameters timed out; invalidating zoom capability";
    if (_continuousZoomActive) {
        _stopContinuousZoom(false);
    }
    _cancelManualZoomFinalize();
    _zoomSyncTimer.stop();
    _zoomOperationTimer.stop();
    _cancelOutstandingZoomQuery();
    _zoomResponseBlocked = false;
    _absoluteZoomPending = false;
    _absoluteZoomTracker.clear();
    _latestActualZoomKnown = false;
    _alignmentAttemptCount = 0;
    _clearAutomaticAlignmentSuppression();
    _suppressIdleAlignmentUntilExplicitZoom = false;
    _requestedZoom = _currentZoom;
    _resetMaximumZoomCapability();
    _setZoomStatusKnown(false);
    _setLastError(
        tr("Timed out waiting for the SIYI recording-stream resolution."));
}

void GimbalControlManager::_handlePulledVideoSize()
{
    _tryConfirmPulledVideoResolution();
    _schedulePulledVideoResolutionFallback();
}

void GimbalControlManager::_handleVideoDecodingChanged()
{
    if (_videoManager && !_videoManager->decoding()) {
        // The decoded stream is the UI-session lock. Command/query/heartbeat
        // transients never lock the buttons, but a real stream disconnect
        // cancels every active gesture and requires a fresh stream check.
        _pulledVideoFallbackTimer.stop();
        _videoManagerFallbackCandidate = QSize();
        _negotiatedPulledVideoSize = QSize();
        _setVideoStreamAvailable(false);
        _stopContinuousZoom(false);
        _nativeHoldFeedbackLogPending = false;
        _cancelManualZoomFinalize();
        _zoomSyncTimer.stop();
        _zoomOperationTimer.stop();
        _cancelOutstandingZoomQuery();
        _zoomResponseBlocked = false;
        _absoluteZoomPending = false;
        _absoluteZoomTracker.clear();
        _clearStableZoomConfirmation();
        _latestActualZoomKnown = false;
        _suppressIdleAlignmentUntilExplicitZoom = false;
        _setZoomStatusKnown(false);
        return;
    }

    _tryConfirmPulledVideoResolution();
    _schedulePulledVideoResolutionFallback();
}

void GimbalControlManager::_handleVideoStreamingChanged()
{
    if (!_videoManager) {
        return;
    }

    if (_videoManager->streaming()) {
        // A new receiver session is the automatic retry boundary after a
        // local start failure. GStreamer stops its recorder on source loss and
        // later reports recordingChanged(false); whichever signal arrives last
        // reconciles one new, uniquely named segment.
        if (_localRecordingIntent
            && _localRecordingResumeOnStream
            && !_videoManager->recording()) {
            if (!_localRecordingIssuedFileBases.isEmpty()
                || _localRecordingStartPending
                || _localRecordingStopPending) {
                _localRecordingRetryAfterGenerationResolved = true;
            } else {
                _localRecordingResumeOnStream = false;
                _localRecordingRetryAfterGenerationResolved = false;
                _localRecordingStartBlocked = false;
                _reconcileLocalRecording();
            }
        }
    } else if (_localRecordingIntent) {
        _localRecordingResumeOnStream = true;
    }

    _notifyRecordingSessionStateChanged();
}

void GimbalControlManager::_handleVideoRecordingChanged()
{
    if (!_videoManager) {
        return;
    }

    const bool actualRecording = _videoManager->recording();
    bool activeStateSignalEmitted = false;
    if (actualRecording) {
        const bool confirmedOwned = _localRecordingOwned
            && _localRecordingOwnershipConfirmed;
        _localRecordingStartTimer.stop();
        _localRecordingStartPending = false;
        _localRecordingResumeOnStream = false;
        _localRecordingRetryAfterGenerationResolved = false;

        if (_localRecordingStopPending) {
            // A confirmed recorder may briefly publish active after a timeout
            // or cancellation already queued its compensating stop. Preserve
            // ownership and the diagnostic until the matching false state.
            if (confirmedOwned) {
                activeStateSignalEmitted =
                    _setLocalRecordingActive(true);
            }
        } else if (_localRecordingIntent && localMediaStorageEnabled()) {
            // recordingChanged alone is global and cannot prove ownership.
            // Only the matching main-receiver start result may confirm it.
            _localRecordingOwned = confirmedOwned;
            _localRecordingOwnershipConfirmed = confirmedOwned;
            _localRecordingUsingExternalSession = !confirmedOwned;
            activeStateSignalEmitted = _setLocalRecordingActive(true);
            _setLocalMediaError(QString());
        } else if (confirmedOwned && !_localRecordingStopPending) {
            // A start confirmation may race with a user stop, a settings
            // change, or shutdown. Never leave a recording we own orphaned.
            activeStateSignalEmitted = _setLocalRecordingActive(true);
            _stopLocalRecording();
        } else if (!confirmedOwned && !_localRecordingStopPending) {
            // The global VideoManager may also be controlled elsewhere. With
            // no bar-session intent, observe but never adopt or stop it.
            _localRecordingOwned = false;
            _localRecordingOwnershipConfirmed = false;
            _localRecordingUsingExternalSession = false;
            activeStateSignalEmitted = _setLocalRecordingActive(false);
        }
    } else {
        const bool finalizedOwnedRecording =
            _localRecordingOwned
            && _localRecordingOwnershipConfirmed;
        const QString finalizedOutputFile =
            _localRecordingOutputFile;
        const bool wasExternalSession =
            _localRecordingUsingExternalSession && !_localRecordingOwned;
        const bool wasExpectedStop = _localRecordingStopPending;
        const bool mayRestartAfterActualEnd =
            _localRecordingResumeOnStream
            || wasExternalSession;

        _localRecordingStartTimer.stop();
        _localRecordingStopTimer.stop();
        _localRecordingStartPending = false;
        _localRecordingStopPending = false;
        _localRecordingOwned = false;
        _localRecordingOwnershipConfirmed = false;
        _localRecordingUsingExternalSession = false;
        _localRecordingStopRetryCount = 0;
        activeStateSignalEmitted = _setLocalRecordingActive(false);

        if (finalizedOwnedRecording) {
            registerLocalMediaFile(finalizedOutputFile);
        }

        if (mayRestartAfterActualEnd
            && _localRecordingIntent
            && localMediaStorageEnabled()
            && _videoManager->streaming()) {
            _localRecordingResumeOnStream = false;
            _localRecordingStartBlocked = false;
            _reconcileLocalRecording();
        } else if (_localRecordingIntent
                   && localMediaStorageEnabled()
                   && _videoManager->streaming()
                   && !mayRestartAfterActualEnd
                   && !wasExpectedStop) {
            // A recorder failure while the source remains healthy is not a
            // reconnect boundary. Block automatic loops (for example on a
            // full disk) until the user or stream starts a new attempt.
            _localRecordingStartBlocked = true;
            _setLocalMediaError(
                tr("Local video recording stopped unexpectedly."));
        }
    }

    if (!activeStateSignalEmitted) {
        emit localRecordingStateChanged();
        _notifyRecordingSessionStateChanged();
    }
}

void GimbalControlManager::_handleLocalMediaStorageEnabledChanged()
{
    emit localMediaStorageEnabledChanged();

    if (localMediaStorageEnabled()) {
        // Only a session started from this control bar gains a local branch.
        // An SD recording started externally must not be silently adopted just
        // because the setting is switched on.
        _localRecordingIntent = _recordingSessionRequested;
        _localRecordingStartBlocked = false;
        if (_localRecordingIntent
            && !_localRecordingIssuedFileBases.isEmpty()) {
            _localRecordingRetryAfterGenerationResolved = true;
        }
    } else {
        _localRecordingIntent = false;
        _localRecordingResumeOnStream = false;
        _localRecordingRetryAfterGenerationResolved = false;
        _setLocalMediaError(QString());
    }

    _reconcileLocalRecording();
    _notifyRecordingSessionStateChanged();
}

void GimbalControlManager::_handleLocalRecordingStartTimeout()
{
    if (!_localRecordingStartPending || !_videoManager) {
        return;
    }

    if (_videoManager->recording()) {
        _handleVideoRecordingChanged();
        return;
    }

    if (_localRecordingOwned && _localRecordingOwnershipConfirmed) {
        // The receiver accepted this exact output, but its public active state
        // did not arrive in time. Keep ownership and issue a compensating stop
        // so a delayed transition can never become an orphaned recording.
        _localRecordingStartBlocked = true;
        _localRecordingResumeOnStream = false;
        _localRecordingRetryAfterGenerationResolved = false;
        _setLocalMediaError(
            tr("Timed out starting local video recording."));
        _stopLocalRecording();
        return;
    }

    _localRecordingStartPending = false;
    _localRecordingOwned = false;
    _localRecordingOwnershipConfirmed = false;
    _localRecordingUsingExternalSession = false;
    _localRecordingStartBlocked = true;
    const bool activeStateSignalEmitted =
        _setLocalRecordingActive(false);
    _setLocalMediaError(
        _localRecordingIntent && localMediaStorageEnabled()
            ? tr("Timed out starting local video recording.")
            : QString());
    if (!activeStateSignalEmitted) {
        emit localRecordingStateChanged();
        _notifyRecordingSessionStateChanged();
    }
}

void GimbalControlManager::_handleLocalRecordingStopTimeout()
{
    if (!_localRecordingStopPending || !_videoManager) {
        return;
    }

    if (!_videoManager->recording()) {
        _handleVideoRecordingChanged();
        return;
    }

    if (_localRecordingStopRetryCount == 0) {
        ++_localRecordingStopRetryCount;
        if (_mainVideoReceiver) {
            _mainVideoReceiver->stopRecording();
        }
        _localRecordingStopTimer.start();
        return;
    }

    _localRecordingStopPending = false;
    const bool activeStateSignalEmitted =
        _setLocalRecordingActive(true);
    _setLocalMediaError(tr("Timed out stopping local video recording."));
    if (!activeStateSignalEmitted) {
        emit localRecordingStateChanged();
        _notifyRecordingSessionStateChanged();
    }
}

void GimbalControlManager::setNegotiatedPulledVideoResolution(const QSize& videoSize)
{
    if (!videoSize.isValid()
        || videoSize.isEmpty()
        || (_videoManager && !_videoManager->decoding())) {
        return;
    }

    // 直接观察结果优先；若它是瞬态/不受支持尺寸，失效处理会允许稳定的
    // VideoManager尺寸继续兜底，并且不会撤销本次已解锁视频会话。
    _pulledVideoFallbackTimer.stop();
    _videoManagerFallbackCandidate = QSize();

    if (_negotiatedPulledVideoSize == videoSize) {
        return;
    }

    _negotiatedPulledVideoSize = videoSize;

    if (!_confirmPulledVideoResolution(_negotiatedPulledVideoSize,
                                       "negotiated main video sink")) {
        _invalidatePulledVideoResolutionCapability(
            _negotiatedPulledVideoSize,
            "unsupported negotiated main video sink");
    }
}

void GimbalControlManager::setMainVideoItem(QQuickItem* videoItem)
{
    if (_mainVideoItem == videoItem) {
        return;
    }
    if (_localPhotoCaptureTimer.isActive()) {
        ++_localPhotoRequestSequence;
        _localPhotoCaptureTimer.stop();
        _localPhotoGrabLifetime.clear();
        _localPhotoCapturePending = false;
        _setLocalMediaError(
            tr("Failed to capture the local video frame."));
        qCWarning(GimbalControlLog)
            << "Cancelled local camera-frame grab because the video item changed";
    }
    _mainVideoItem = videoItem;
}

void GimbalControlManager::setMainVideoReceiver(VideoReceiver* receiver)
{
    if (receiver && receiver->isThermal()) {
        return;
    }
    _mainVideoReceiver = receiver;
}

void GimbalControlManager::handleMainVideoRecordingStartResult(
    bool success,
    const QString& outputFile)
{
    if (success) {
        const QString resultFileBase = outputFile.isEmpty()
            ? QString()
            : QFileInfo(outputFile).completeBaseName();
        if (!_localRecordingIssuedFileBases.removeOne(resultFileBase)) {
            // Another component won a concurrent global start. Its recording
            // may be adopted for display, but this manager must never claim
            // ownership merely because the global recording flag changed.
            return;
        }

        // The unique basename is also the request generation. It remains in
        // the issued list after the UI timeout, so a late matching success is
        // still ours rather than an external recorder session.
        _localRecordingFileBase = resultFileBase;
        _localRecordingOutputFile = outputFile;
        _localRecordingOwned = true;
        _localRecordingOwnershipConfirmed = true;
        _localRecordingUsingExternalSession = false;
        _localRecordingStartBlocked = false;
        _localRecordingResumeOnStream = false;
        _localRecordingRetryAfterGenerationResolved = false;

        if (_videoManager && _videoManager->recording()) {
            _handleVideoRecordingChanged();
        } else if (!_localRecordingIntent
                   || !localMediaStorageEnabled()) {
            // The user, setting, or application shutdown may have cancelled
            // while the receiver was starting. Queue the compensating stop;
            // do not clear ownership until the receiver actually finalizes.
            _localRecordingStartPending = true;
            _stopLocalRecording();
        } else {
            const bool pendingChanged = !_localRecordingStartPending;
            _localRecordingStartPending = true;
            _localRecordingStartTimer.start();
            if (pendingChanged) {
                emit localRecordingStateChanged();
                _notifyRecordingSessionStateChanged();
            }
        }
        return;
    }

    // Only one receiver start generation may be unresolved at a time. The
    // failure signal carries no filename, so FIFO uniqueness is the only
    // reliable correlation available without changing the native receiver.
    if (_localRecordingIssuedFileBases.isEmpty()) {
        return;
    }

    const QString failedFileBase =
        _localRecordingIssuedFileBases.takeFirst();
    if (failedFileBase != _localRecordingFileBase) {
        qCWarning(GimbalControlLog)
            << "Unexpected local recording generation order"
            << failedFileBase << _localRecordingFileBase;
    }

    if (_localRecordingStopPending) {
        if (!_videoManager || !_videoManager->recording()) {
            _handleVideoRecordingChanged();
        }
        return;
    }

    _localRecordingStartTimer.stop();
    _localRecordingStartPending = false;
    if (!_localRecordingOwnershipConfirmed) {
        _localRecordingOwned = false;
    }
    const bool retryResolvedGeneration =
        (_localRecordingRetryAfterGenerationResolved
         || _localRecordingResumeOnStream)
        && _localRecordingIntent
        && localMediaStorageEnabled()
        && _videoManager
        && _videoManager->streaming();
    _localRecordingStartBlocked = !retryResolvedGeneration;
    if (retryResolvedGeneration) {
        _localRecordingResumeOnStream = false;
        _localRecordingRetryAfterGenerationResolved = false;
    }
    if (!_localRecordingUsingExternalSession) {
        _localRecordingOwnershipConfirmed = false;
        _setLocalRecordingActive(false);
        _setLocalMediaError(
            _localRecordingIntent
                    && localMediaStorageEnabled()
                    && !retryResolvedGeneration
                ? tr("Failed to start local video recording.")
                : QString());
    }
    emit localRecordingStateChanged();
    _notifyRecordingSessionStateChanged();
    _reconcileLocalRecording();
}

void GimbalControlManager::shutdownLocalMedia(bool waitForStop)
{
    _recordingSessionRequested = false;
    _localRecordingIntent = false;
    _localRecordingResumeOnStream = false;
    _localRecordingRetryAfterGenerationResolved = false;
    const bool ownedOrPending = _localRecordingOwned
        || _localRecordingStartPending
        || _localRecordingStopPending;
    const bool unresolvedStart =
        !_localRecordingIssuedFileBases.isEmpty();

    if (_localRecordingOwned) {
        _stopLocalRecording();
    } else if (_localRecordingUsingExternalSession) {
        _localRecordingUsingExternalSession = false;
        _setLocalRecordingActive(false);
    }

    const auto localMediaFinalized = [this]() {
        return !_localRecordingOwned
            && !_localRecordingStartPending
            && !_localRecordingStopPending
            && _localRecordingIssuedFileBases.isEmpty();
    };

    bool waitTimedOut = false;
    if (waitForStop
        && (ownedOrPending || unresolvedStart)
        && !localMediaFinalized()) {
        QEventLoop waitLoop;
        QTimer waitTimer;
        waitTimer.setSingleShot(true);
        connect(this,
                &GimbalControlManager::localRecordingStateChanged,
                &waitLoop,
                [&waitLoop, localMediaFinalized]() {
                    if (localMediaFinalized()) {
                        waitLoop.quit();
                    }
                });
        if (_videoManager) {
            connect(_videoManager,
                    &VideoManager::recordingChanged,
                    &waitLoop,
                    [&waitLoop, localMediaFinalized]() {
                        if (localMediaFinalized()) {
                            waitLoop.quit();
                        }
                    });
        }
        connect(&waitTimer, &QTimer::timeout, &waitLoop, &QEventLoop::quit);
        waitTimer.start(kApplicationShutdownRecordingWaitMs);
        waitLoop.exec(QEventLoop::ExcludeUserInputEvents);
        waitTimedOut = !localMediaFinalized();
        waitTimer.stop();
    }

    const bool stopped = !_videoManager || !_videoManager->recording();
    if (stopped || waitForStop) {
        if (waitTimedOut) {
            qCWarning(GimbalControlLog)
                << "Timed out resolving or finalizing the owned local recording during shutdown";
        }
        _localRecordingStartTimer.stop();
        _localRecordingStopTimer.stop();
        _localRecordingOwned = false;
        _localRecordingOwnershipConfirmed = false;
        _localRecordingUsingExternalSession = false;
        _localRecordingStartPending = false;
        _localRecordingStopPending = false;
        _localRecordingStopRetryCount = 0;
        _localRecordingIssuedFileBases.clear();
        const bool wasActive = _localRecordingActive;
        _setLocalRecordingActive(false);
        if (!wasActive) {
            emit localRecordingStateChanged();
        }
    }

    _mainVideoItem.clear();
    _notifyRecordingSessionStateChanged();
}

void GimbalControlManager::_tryConfirmPulledVideoResolution()
{
#ifdef QGC_GST_STREAMING
    // GstVideoReceiver启动阶段的query_caps不是最终协商值。GStreamer构建
    // 此入口优先处理显示sink依据最终GstVideoInfo报告的可信尺寸；
    // VideoManager低优先级稳定兜底由独立Timer处理。
    if (!_negotiatedPulledVideoSize.isValid()
        || _negotiatedPulledVideoSize.isEmpty()) {
        return;
    }
    (void) _confirmPulledVideoResolution(
        _negotiatedPulledVideoSize,
        "negotiated main video sink");
#else
    if (!_videoManager || !_videoManager->decoding()) {
        return;
    }
    const QSize videoSize = _videoManager->videoSize();
    if (videoSize.isValid()
        && !videoSize.isEmpty()
        && !_confirmPulledVideoResolution(videoSize, "VideoManager")) {
        _invalidatePulledVideoResolutionCapability(
            videoSize,
            "unsupported VideoManager resolution");
    }
#endif
}

void GimbalControlManager::_schedulePulledVideoResolutionFallback()
{
#ifdef QGC_GST_STREAMING
    // 某些平台能正常显示视频并更新VideoManager，但显示sink的两个custom观察器
    // 没有回调。只有尚无任何直接观察结果时，才把稳定的VideoManager尺寸作为兜底。
    if (_negotiatedPulledVideoSize.isValid()
        || !_videoManager
        || !_videoManager->decoding()) {
        _pulledVideoFallbackTimer.stop();
        _videoManagerFallbackCandidate = QSize();
        return;
    }

    const QSize videoSize = _videoManager->videoSize();
    const bool supported = videoSize.isValid()
        && !videoSize.isEmpty()
        && videoSize.width() <= std::numeric_limits<quint16>::max()
        && videoSize.height() <= std::numeric_limits<quint16>::max()
        && A8MiniZoomPolicy::isSupportedPulledVideoResolution(
            static_cast<quint16>(videoSize.width()),
            static_cast<quint16>(videoSize.height()));
    if (!supported) {
        _pulledVideoFallbackTimer.stop();
        _videoManagerFallbackCandidate = QSize();
        return;
    }
    if (_videoStreamAvailable
        && _pulledVideoResolutionConfirmed) {
        _pulledVideoFallbackTimer.stop();
        _videoManagerFallbackCandidate = QSize();
        return;
    }

    if (_videoManagerFallbackCandidate == videoSize
        && _pulledVideoFallbackTimer.isActive()) {
        return;
    }

    _videoManagerFallbackCandidate = videoSize;
    _pulledVideoFallbackTimer.start();
#endif
}

void GimbalControlManager::_tryConfirmPulledVideoResolutionFallback()
{
#ifdef QGC_GST_STREAMING
    if (_negotiatedPulledVideoSize.isValid()
        || !_videoManager
        || !_videoManager->decoding()) {
        _videoManagerFallbackCandidate = QSize();
        return;
    }

    const QSize videoSize = _videoManager->videoSize();
    if (videoSize != _videoManagerFallbackCandidate) {
        _videoManagerFallbackCandidate = QSize();
        _schedulePulledVideoResolutionFallback();
        return;
    }

    _videoManagerFallbackCandidate = QSize();
    (void) _confirmPulledVideoResolution(
        videoSize,
        "stable VideoManager fallback");
#endif
}

void GimbalControlManager::_invalidatePulledVideoResolutionCapability(
    const QSize& videoSize,
    const char* sourceDescription)
{
    // A sink can briefly expose an odd coded/intermediate size while the same
    // decoded stream is being reconfigured. Once a supported resolution has
    // unlocked this decoding session, such a sample must not re-lock both
    // buttons. Forget the unusable direct sample so the stable VideoManager
    // fallback can still refresh the capability.
    _negotiatedPulledVideoSize = QSize();
    if (_videoStreamAvailable && _pulledVideoResolutionConfirmed) {
        _schedulePulledVideoResolutionFallback();
        return;
    }

    _setVideoStreamAvailable(false);
    if (!_pulledVideoResolutionConfirmed) {
        _schedulePulledVideoResolutionFallback();
        return;
    }

    // Pull resolution is only the decoded-video session gate. An unsupported
    // size locks the controls, but it must not replace or erase the independent
    // recording-resolution zoom capability.
    _pulledVideoResolutionConfirmed = false;
    _stopContinuousZoom(false);
    _nativeHoldFeedbackLogPending = false;
    _cancelManualZoomFinalize();
    _sdkResponseTimer.stop();
    _zoomSyncTimer.stop();
    _zoomOperationTimer.stop();
    _cancelOutstandingZoomQuery();
    _zoomResponseBlocked = false;
    _absoluteZoomPending = false;
    _absoluteZoomTracker.clear();
    _latestActualZoomKnown = false;
    _alignmentAttemptCount = 0;
    _clearAutomaticAlignmentSuppression();
    _suppressIdleAlignmentUntilExplicitZoom = false;
    _requestedZoom = _currentZoom;
    _setZoomStatusKnown(false);
    qCWarning(GimbalControlLog)
        << "Released stale pulled-video session after" << sourceDescription
        << videoSize.width() << "x" << videoSize.height()
        << "- waiting for a supported decoded stream";
    _schedulePulledVideoResolutionFallback();
}

void GimbalControlManager::_applyMaximumZoomCapability(double maximumZoom)
{
    if (!qIsFinite(maximumZoom)
        || maximumZoom < kMinZoom
        || maximumZoom > kProtocolMaxZoom) {
        return;
    }

    const double normalizedCapabilityMaximum =
        qRound(qBound(kMinZoom, maximumZoom, kProtocolMaxZoom) * 10.0) / 10.0;
    double alignedMaximum = kMinZoom;
    if (!A8MiniZoomPolicy::alignedMaximumZoom(normalizedCapabilityMaximum,
                                               zoomStep(),
                                               kMinZoom,
                                               &alignedMaximum)) {
        return;
    }
    const bool effectiveCapabilityChanged = !_maximumZoomKnown
        || qAbs(_maximumZoom - alignedMaximum) > kZoomComparisonTolerance;

    // Preserve the raw capability for settings changes. The exact physical
    // ceiling remains a legal terminal stop even when the last interval is
    // shorter than zoomStep.
    _capabilityMaximumZoom = normalizedCapabilityMaximum;
    _setMaximumZoom(alignedMaximum);
    _setMaximumZoomKnown(true);
    if (!effectiveCapabilityChanged) {
        return;
    }

    // 能力来源恢复或上限改变后，重新取得稳定的0x18实际倍率作为后续
    // 绝对目标规划参考；显示仍只发布合法目标，不发布中间原始采样。
    _stopContinuousZoom(false);
    _cancelManualZoomFinalize();
    _sdkResponseTimer.stop();
    _zoomSyncTimer.stop();
    _zoomOperationTimer.stop();
    _cancelOutstandingZoomQuery();
    _zoomResponseBlocked = false;
    _absoluteZoomPending = false;
    _absoluteZoomTracker.clear();
    _alignmentAttemptCount = 0;
    _clearAutomaticAlignmentSuppression();
    _suppressIdleAlignmentUntilExplicitZoom = false;
    _requestedZoom = _currentZoom;
    _beginStableZoomConfirmation(true, 0);
    _setZoomStatusKnown(false);
    if (enabled()) {
        _cancelOutstandingZoomQuery();
        _zoomOperationTimer.start();
        _scheduleZoomSync();
    }
}

void GimbalControlManager::_refreshMaximumZoomCapability()
{
    if (!_recordingResolutionConfirmed) {
        _resetMaximumZoomCapability();
        return;
    }

    const double effectiveMaximum = _deviceMaximumZoomKnown
        ? qMin(_recordingResolutionMaximumZoom, _deviceMaximumZoom)
        : _recordingResolutionMaximumZoom;
    _applyMaximumZoomCapability(effectiveMaximum);
}

bool GimbalControlManager::_confirmPulledVideoResolution(
    const QSize& videoSize,
    const char* sourceDescription)
{
    const bool sizeAvailable = videoSize.isValid()
        && !videoSize.isEmpty()
        && videoSize.width() <= std::numeric_limits<quint16>::max()
        && videoSize.height() <= std::numeric_limits<quint16>::max();
    if (!sizeAvailable) {
        return false;
    }

    const quint16 width = static_cast<quint16>(videoSize.width());
    const quint16 height = static_cast<quint16>(videoSize.height());

    if (!A8MiniZoomPolicy::isSupportedPulledVideoResolution(width,
                                                            height)) {
        if (_lastRejectedPulledVideoSize != videoSize) {
            _lastRejectedPulledVideoSize = videoSize;
            qCWarning(GimbalControlLog)
                << "Ignoring unsupported pulled video resolution"
                << width << "x" << height
                << "from" << sourceDescription
                << "- waiting for negotiated 1280x720 or 1920x1080 video";
        }
        return false;
    }

    if (_pulledVideoResolutionConfirmed) {
        // Re-decoding a supported resolution starts a new usable stream
        // session. The ceiling remains owned by the independent recording
        // stream query, but rebuild the actual-zoom reference which the
        // disconnect path intentionally discarded.
        const bool resumingStreamSession = !_videoStreamAvailable;
        _lastRejectedPulledVideoSize = QSize();
        _pulledVideoFallbackTimer.stop();
        _videoManagerFallbackCandidate = QSize();
        _setVideoStreamAvailable(true);
        if (resumingStreamSession
            && enabled()
            && _maximumZoomKnown
            && !_latestActualZoomKnown
            && !_absoluteZoomPending
            && !_manualZoomFinalizePending
            && !_continuousZoomActive) {
            _beginStableZoomConfirmation(true, 0);
            _cancelOutstandingZoomQuery();
            _zoomResponseBlocked = false;
            if (!_sendCurrentZoomQuery(false)) {
                _scheduleZoomSync();
            }
        }
        return true;
    }

    _lastRejectedPulledVideoSize = QSize();
    _pulledVideoFallbackTimer.stop();
    _videoManagerFallbackCandidate = QSize();
    _pulledVideoResolutionConfirmed = true;
    _setVideoStreamAvailable(true);
    qCInfo(GimbalControlLog)
        << "Latched pulled video session resolution:"
        << width << "x" << height
        << "source" << sourceDescription
        << "recording capability known" << _maximumZoomKnown
        << "effective terminal maximum zoom"
        << (_maximumZoomKnown ? _maximumZoom : -1.0);
    return true;
}

void GimbalControlManager::_handleCurrentZoom(double zoomLevel)
{
    if (!enabled()) {
        return;
    }

    // 只消费本状态机主动打开的查询窗口；绝对命令刚发出后的旧0x18直接丢弃。
    if (_zoomResponseBlocked || !_zoomQueryOutstanding) {
        return;
    }
    _cancelOutstandingZoomQuery();
    _sdkResponseTimer.stop();
    _setSdkResponding(true);
    if (!qIsFinite(zoomLevel)
        || zoomLevel < kMinZoom
        || zoomLevel > kProtocolMaxZoom) {
        return;
    }

    const double normalizedActualZoom = qRound(zoomLevel * 10.0) / 10.0;
    const bool actualReferenceChanged = !_latestActualZoomKnown
        || qAbs(_latestActualZoom - normalizedActualZoom) > kZoomComparisonTolerance;
    _latestActualZoom = normalizedActualZoom;
    _latestActualZoomKnown = true;
    if (actualReferenceChanged) {
        emit zoomAvailabilityChanged();
    }

    if (!_maximumZoomKnown) {
        _setZoomStatusKnown(false);
        return;
    }

    if (_nativeHoldFeedbackLogPending && !_continuousZoomActive) {
        _nativeHoldFeedbackLogPending = false;
        if (_nativeHoldTargetLatched) {
            qCInfo(GimbalControlLog)
                << "Observed SIYI zoom after native hold:"
                << "target" << _currentZoom
                << "actual" << normalizedActualZoom;
        }
    }

    if (_manualZoomFinalizePending) {
        _finishManualZoomStop(normalizedActualZoom);
        return;
    }

    if (_absoluteZoomPending) {
        const A8MiniZoomPolicy::TargetObservation observation =
            _absoluteZoomTracker.observe(normalizedActualZoom);
        if (observation != A8MiniZoomPolicy::TargetObservation::TargetReached) {
            // A8数字变倍运动时可能连续多次返回同一个中间值。无论1.6等
            // 非网格值还是2.0等旧合法档，都不能据此提前取消更新的0x0f
            // 目标；只持续查询到精确目标或统一的10秒操作截止时间。
            _scheduleZoomSync();
            return;
        }
        if (!A8MiniZoomPolicy::isAlignedZoom(normalizedActualZoom,
                                             zoomStep(),
                                             kMinZoom,
                                             _maximumZoom)) {
            // 用户可能在镜头运动期间修改zoomStep。旧目标即使已到达，也不能
            // 以新步长的非网格值结束；转入与其他归整共用的有界恢复路径。
            _absoluteZoomPending = false;
            _absoluteZoomTracker.clear();
            const AlignmentAttemptResult alignmentResult =
                _tryRealignStableZoom(normalizedActualZoom);
            if (alignmentResult == AlignmentAttemptResult::CommandSent) {
                return;
            }
            if (alignmentResult == AlignmentAttemptResult::SendFailed) {
                if (_lastError.isEmpty()) {
                    _setLastError(tr("Failed to send the realigned SIYI camera zoom step."));
                }
                return;
            }
            _finalizeConfirmedZoom(normalizedActualZoom);
            _setLastError(tr("The SIYI camera could not be aligned after the zoom step changed; "
                             "the last legal value is retained and the controls remain available."));
            return;
        }

        // 0x0f ACK只表示受理；只有本状态机主动查询并精确命中目标的0x18
        // 才能完成实际位置核对。目标倍率在发送成功时已经显示。
        _finalizeConfirmedZoom(_requestedZoom);
        return;
    }

    if (_continuousZoomActive) {
        // Held targets are derived only from the immutable gesture origin and
        // elapsed time. Feedback is recorded above but never becomes a new
        // origin and therefore cannot reverse or re-phase the target sequence.
        _continuousZoomStepTimer.start(kManualZoomPollIntervalMs);
        return;
    }

    if (_nativeHoldTargetLatched) {
        // The displayed value is deliberately the legal elapsed-time target.
        // After native motion stops, a nearby or delayed 0x18 observation must
        // not rename that target or trigger an opposite-direction correction.
        // Retain it until a newer explicit command is accepted.
        double retainedTarget =
            qBound(kMinZoom, _currentZoom, _maximumZoom);
        if (!A8MiniZoomPolicy::isAlignedZoom(retainedTarget,
                                             zoomStep(),
                                             kMinZoom,
                                             _maximumZoom)
            && !A8MiniZoomPolicy::alignmentTarget(retainedTarget,
                                                   zoomStep(),
                                                   kMinZoom,
                                                   _maximumZoom,
                                                   0,
                                                   &retainedTarget)) {
            retainedTarget = kMinZoom;
        }
        _zoomOperationTimer.stop();
        _clearStableZoomConfirmation();
        _requestedZoom = retainedTarget;
        _setCurrentZoom(retainedTarget);
        _setZoomStatusKnown(true);
        _setZoomValueUncertain(false);
        return;
    }

    if (_suppressIdleAlignmentUntilExplicitZoom && _zoomStatusKnown) {
        // After an explicit command, currentZoom intentionally remains the
        // user target. Idle 0x18 polling may update _latestActualZoom, but it
        // must not rename a delayed/intermediate actual value as the target or
        // issue an automatic correction in the opposite direction.
        _zoomOperationTimer.stop();
        _clearStableZoomConfirmation();
        _requestedZoom = _currentZoom;
        _setZoomValueUncertain(false);
        return;
    }

    if (_zoomStatusKnown
        && normalizedActualZoom
            <= _maximumZoom + kZoomComparisonTolerance
        && qAbs(normalizedActualZoom - _currentZoom)
            <= kZoomComparisonTolerance) {
        // 空闲轮询只保留和设备精确一致的合法显示；非网格反馈走下面的
        // 有界稳定值流程，绝不再改名成相邻的整数倍率。
        _zoomOperationTimer.stop();
        _clearStableZoomConfirmation();
        _requestedZoom = _currentZoom;
        _setZoomValueUncertain(false);
        return;
    }

    if (zoomLevel > _maximumZoom + 0.05) {
        if (_zoomStatusKnown
            && qAbs(zoomLevel - _currentZoom) <= kZoomComparisonTolerance
            && _automaticAlignmentSuppressedFor(zoomLevel)) {
            // 已确认并锁存为“硬件无法归整”的同一超限真实值。空闲轮询
            // 只刷新显示，不再进入双确认或周期性短暂锁住两个按钮。
            _finalizeConfirmedZoom(zoomLevel);
            return;
        }

        // 有效卡录/设备上限降低时镜头可能仍在更高倍率。先确认两次相同超限值，再命令
        // 回有效网格上限；确认期间保留上一合法档，不公开超限raw值。
        if (!_stableZoomConfirmationPending
            || !_stableZoomCandidateValid
            || qAbs(zoomLevel - _stableZoomCandidate) > kZoomComparisonTolerance) {
            _beginStableZoomConfirmation(true, 0);
            _stableZoomCandidate = zoomLevel;
            _stableZoomCandidateValid = true;
            _scheduleZoomSync();
            return;
        }

        _clearStableZoomConfirmation();
        const AlignmentAttemptResult alignmentResult =
            _tryRealignStableZoom(zoomLevel);
        if (alignmentResult == AlignmentAttemptResult::SendFailed) {
            return;
        }
        if (alignmentResult == AlignmentAttemptResult::NotNeeded) {
            // 设备连续拒绝或归整查询超时后，保留上一合法显示并允许用户向
            // 安全上限方向缩小；抑制锁存防止空闲轮询周期性重发同一校正。
            _finalizeConfirmedZoom(zoomLevel);
            _setLastError(tr("The SIYI camera could not return to the current zoom limit; "
                             "the last legal value is retained and zoom-out remains available."));
            return;
        }
        qCWarning(GimbalControlLog)
            << "Confirmed SIYI zoom" << zoomLevel
            << "above effective recording-mode limit" << _maximumZoom
            << "- commanding the exact effective terminal maximum";
        return;
    }

    bool stableValueConfirmed = false;
    if (_stableZoomConfirmationPending) {
        // 首次同步或发现外部任意倍率时要求两次一致，再归整到固定分度。
        if (!_stableZoomCandidateValid
            || qAbs(zoomLevel - _stableZoomCandidate) > kZoomComparisonTolerance) {
            _stableZoomCandidate = zoomLevel;
            _stableZoomCandidateValid = true;
            _scheduleZoomSync();
            return;
        }

        const bool normalizeToStepGrid = _normalizeAfterStableZoom;
        const int alignmentDirection = _stableZoomDirection;
        _clearStableZoomConfirmation();
        stableValueConfirmed = true;
        if (normalizeToStepGrid) {
            if (_suppressIdleAlignmentUntilExplicitZoom
                && !A8MiniZoomPolicy::isAlignedZoom(zoomLevel,
                                                     zoomStep(),
                                                     kMinZoom,
                                                     _maximumZoom)) {
                // A timed-out user gesture owns no future movement. Keep its
                // off-grid result private until the next explicit gesture;
                // ordinary polling must never resurrect a correction later.
                _zoomSyncTimer.stop();
                _zoomOperationTimer.stop();
                _requestedZoom = _currentZoom;
                // Keep the displayed legal target visible. This expired
                // verification owns no future movement, so the raw off-grid
                // value is diagnostic only.
                _setZoomValueUncertain(false);
                _setLastError(
                    tr("The stopped SIYI zoom is outside the configured step; "
                       "no delayed alignment command will be sent."));
                return;
            }
            double alignedZoom = 0.0;
            if (!A8MiniZoomPolicy::alignmentTarget(zoomLevel,
                                                    zoomStep(),
                                                    kMinZoom,
                                                    _maximumZoom,
                                                    alignmentDirection,
                                                    &alignedZoom)) {
                _setZoomStatusKnown(false);
                _setLastError(tr("Failed to align the SIYI camera zoom to the configured step."));
                return;
            }
            if (qAbs(alignedZoom - zoomLevel) > kZoomComparisonTolerance) {
                const AlignmentAttemptResult alignmentResult =
                    _tryRealignStableZoom(zoomLevel);
                if (alignmentResult == AlignmentAttemptResult::CommandSent) {
                    return;
                }
                if (alignmentResult == AlignmentAttemptResult::SendFailed) {
                    return;
                }
                _finalizeConfirmedZoom(zoomLevel);
                _setLastError(tr("The SIYI camera could not be aligned to the configured zoom step; "
                                 "the last legal value is retained and the controls remain available."));
                return;
            }
        }
    }

    if (!stableValueConfirmed
        && (_zoomValueUncertain
            || !_zoomStatusKnown
            || qAbs(zoomLevel - _currentZoom) > kZoomComparisonTolerance)) {
        // 空闲状态下的首次值或外部倍率变化同样要求两次独立查询一致。
        // 这样单个迟到/运动中回包不会直接替换显示值。
        _beginStableZoomConfirmation(true, 0);
        _stableZoomCandidate = zoomLevel;
        _stableZoomCandidateValid = true;
        // 初次同步尚无合法档时保持--；已有值重新核对时保留上一合法档。
        // 两种情况都不改变视频会话解锁状态。
        if (!_zoomStatusKnown) {
            _setZoomStatusKnown(false);
        }
        if (!_zoomOperationTimer.isActive()) {
            _zoomOperationTimer.start();
        }
        _scheduleZoomSync();
        return;
    }

    if (!A8MiniZoomPolicy::isAlignedZoom(zoomLevel,
                                         zoomStep(),
                                         kMinZoom,
                                         _maximumZoom)) {
        if (_suppressIdleAlignmentUntilExplicitZoom) {
            _zoomSyncTimer.stop();
            _zoomOperationTimer.stop();
            _requestedZoom = _currentZoom;
            _setZoomValueUncertain(false);
            _setLastError(
                tr("The SIYI zoom is outside the configured step; "
                   "waiting for a new explicit zoom gesture."));
            return;
        }
        const AlignmentAttemptResult alignmentResult =
            _tryRealignStableZoom(zoomLevel);
        if (alignmentResult == AlignmentAttemptResult::CommandSent) {
            return;
        }
        if (alignmentResult == AlignmentAttemptResult::SendFailed) {
            return;
        }
        _finalizeConfirmedZoom(zoomLevel);
        _setLastError(tr("The SIYI camera reported a stable value outside the configured zoom-step grid; "
                         "the last legal value is retained and the controls remain available."));
        return;
    }

    _finalizeConfirmedZoom(zoomLevel);
}

void GimbalControlManager::_handleCameraSystemStatus(quint8 hdrStatus,
                                                     quint8 recordingStatus,
                                                     quint8 gimbalMotionMode,
                                                     quint8 gimbalMountingDirection,
                                                     quint8 videoOutputStatus,
                                                     quint8 zoomLinkage)
{
    if (!enabled()) {
        return;
    }

    Q_UNUSED(hdrStatus);
    Q_UNUSED(gimbalMotionMode);
    Q_UNUSED(gimbalMountingDirection);
    Q_UNUSED(videoOutputStatus);
    Q_UNUSED(zoomLinkage);

    if (_recordingCommandPending) {
        if (recordingStatus == 0 || recordingStatus == 1) {
            // 0/1 可能来自命令前的旧轮询。延迟查询尚未发出，或状态与目标不一致时继续等待。
            const bool responseRecording = recordingStatus == 1;
            if (!_recordingStatusResponseAllowed || responseRecording != _recordingCommandTarget) {
                return;
            }
        } else if (recordingStatus != 2 && recordingStatus != 3) {
            return;
        }
    }

    bool statusHandled = true;
    switch (recordingStatus) {
    case 0:
        _setCameraStatusKnown(true);
        _setRecording(false);
        _setLastError(QString());
        break;
    case 1:
        _cameraRecordingStartBlocked = false;
        _setCameraStatusKnown(true);
        _setRecording(true);
        _setLastError(QString());
        break;
    case 2:
        if (_cameraRecordingIntentValid
            && _cameraRecordingIntentTarget) {
            _cameraRecordingStartBlocked = true;
        }
        _setCameraStatusKnown(true);
        _setRecording(false);
        _setLastError(tr("The SIYI camera has no storage card."));
        break;
    case 3:
        if (_cameraRecordingIntentValid
            && _cameraRecordingIntentTarget) {
            _cameraRecordingStartBlocked = true;
        }
        _setCameraStatusKnown(false);
        _setRecording(false);
        _setLastError(tr("The SIYI camera reported video data loss."));
        break;
    default:
        statusHandled = false;
        break;
    }

    if (statusHandled && _recordingCommandPending) {
        _finishRecordingCommand();
    }
    if (statusHandled) {
        _reconcileCameraRecordingIntent();
        _notifyRecordingSessionStateChanged();
    }
}

void GimbalControlManager::_handleFunctionFeedback(quint8 infoType)
{
    if (!enabled()) {
        return;
    }

    switch (infoType) {
    case 0:
        if (_photoCommandPending) {
            _photoCommandPending = false;
            _photoFeedbackTimer.stop();
            ++_photoCount;
            emit photoCountChanged();
        }
        _setLastError(QString());
        break;
    case 1:
        _photoCommandPending = false;
        _photoFeedbackTimer.stop();
        _setLastError(tr("Photo capture failed or the storage card is unavailable."));
        break;
    case 4:
        if ((_recordingCommandPending && _recordingCommandTarget)
            || (_cameraRecordingIntentValid
                && _cameraRecordingIntentTarget)) {
            _cameraRecordingStartBlocked = true;
            _setRecording(false);
        } else if ((_recordingCommandPending
                    && !_recordingCommandTarget)
                   || (_cameraRecordingIntentValid
                       && !_cameraRecordingIntentTarget)) {
            _cameraRecordingStopBlocked = true;
        }
        _finishRecordingCommand();
        _setCameraStatusKnown(false);
        _setLastError(tr("Video recording failed or the storage card is unavailable."));
        _syncCameraStatus();
        _notifyRecordingSessionStateChanged();
        break;
    default:
        break;
    }
}

void GimbalControlManager::_handleCommunicationError(const QString& message)
{
    const bool manualStopMayBeUnconfirmed =
        !_manualZoomSessionHost.isEmpty()
        && _manualZoomSessionPort != 0
        && _manualZoomStopRetryAttemptsRemaining > 0;
    const bool zoomMovementMayBeUnconfirmed = _continuousZoomActive
        || _manualZoomFinalizePending
        || _absoluteZoomPending
        || manualStopMayBeUnconfirmed;
    if (_continuousZoomActive) {
        _stopContinuousZoom(false);
    }
    _cancelManualZoomFinalize();
    _sdkResponseTimer.stop();
    _zoomSyncTimer.stop();
    _zoomOperationTimer.stop();
    _cancelOutstandingZoomQuery();
    _zoomResponseBlocked = false;
    _absoluteZoomPending = false;
    _absoluteZoomTracker.clear();
    _alignmentAttemptCount = 0;
    _clearAutomaticAlignmentSuppression();
    if (zoomMovementMayBeUnconfirmed) {
        _suppressIdleAlignmentUntilExplicitZoom = true;
    }
    _clearStableZoomConfirmation();
    _resetMaximumZoomCapability();
    _photoFeedbackTimer.stop();
    _photoCommandPending = false;
    if (_cameraRecordingIntentValid) {
        if (_cameraRecordingIntentTarget) {
            _cameraRecordingStartBlocked = true;
        } else {
            _cameraRecordingStopBlocked = true;
        }
    }
    _finishRecordingCommand();
    _setSdkResponding(false);
    if (enabled()
        && _videoStreamAvailable
        && _maximumZoomKnown
        && _zoomStatusKnown
        && A8MiniZoomPolicy::isAlignedZoom(_currentZoom,
                                            zoomStep(),
                                            kMinZoom,
                                            _maximumZoom)) {
        // SDK reachability is diagnostic and may briefly flap while the
        // decoded-video session remains healthy. Keep the last published legal
        // target visible and usable; no old movement is replayed.
        _requestedZoom = _currentZoom;
        _setZoomValueUncertain(false);
        emit zoomAvailabilityChanged();
    } else {
        _setZoomStatusKnown(false);
    }
    _setCameraStatusKnown(false);
    _setRecording(false);
    _setLastError(message);
    _notifyRecordingSessionStateChanged();
}

void GimbalControlManager::_markSdkNotResponding()
{
    const bool manualStopMayBeUnconfirmed =
        !_manualZoomSessionHost.isEmpty()
        && _manualZoomSessionPort != 0
        && _manualZoomStopRetryAttemptsRemaining > 0;
    const bool zoomMovementMayBeUnconfirmed = _continuousZoomActive
        || _manualZoomFinalizePending
        || _absoluteZoomPending
        || manualStopMayBeUnconfirmed;
    if (_continuousZoomActive) {
        _stopContinuousZoom(false);
    }
    _cancelManualZoomFinalize();
    _continuousZoomWatchdog.stop();
    _zoomSyncTimer.stop();
    _zoomOperationTimer.stop();
    _cancelOutstandingZoomQuery();
    _zoomResponseBlocked = false;
    _absoluteZoomPending = false;
    _absoluteZoomTracker.clear();
    _alignmentAttemptCount = 0;
    _clearStableZoomConfirmation();
    if (zoomMovementMayBeUnconfirmed) {
        _suppressIdleAlignmentUntilExplicitZoom = true;
    }
    _resetMaximumZoomCapability();
    _photoFeedbackTimer.stop();
    _photoCommandPending = false;
    if (_cameraRecordingIntentValid) {
        if (_cameraRecordingIntentTarget) {
            _cameraRecordingStartBlocked = true;
        } else {
            _cameraRecordingStopBlocked = true;
        }
    }
    _finishRecordingCommand();
    _setSdkResponding(false);
    if (enabled()
        && _videoStreamAvailable
        && _maximumZoomKnown
        && _zoomStatusKnown
        && A8MiniZoomPolicy::isAlignedZoom(_currentZoom,
                                            zoomStep(),
                                            kMinZoom,
                                            _maximumZoom)) {
        _requestedZoom = _currentZoom;
        _setZoomValueUncertain(false);
        emit zoomAvailabilityChanged();
    } else {
        _setZoomStatusKnown(false);
    }
    _setCameraStatusKnown(false);
    _setRecording(false);
    _setLastError(tr("No response from the SIYI SDK endpoint."));
    _notifyRecordingSessionStateChanged();
}

void GimbalControlManager::_pollSdk()
{
    if (!enabled()) {
        return;
    }

    // Signals are the primary path. This retry also covers a receiver which had
    // already published its negotiated size before this manager was initialized.
    _tryConfirmPulledVideoResolution();
    _schedulePulledVideoResolutionFallback();

    // The recording stream (0x20, stream type 0) owns the mode-to-limit
    // mapping. 0x16 is polled alongside it as a device-side safety cross-check.
    // Replies may be coalesced into one UDP datagram; SiyiSdk splits and
    // validates every frame before dispatching it.
    _configureSdkEndpoint();
    (void) _sdk->requestRecordingStreamParameters();
    (void) _sdk->requestMaximumZoom();

    // 有绝对目标、稳定值确认或长按重复步骤时，由专用同步定时器串行查询。
    const bool shouldRequestZoom = _maximumZoomKnown
        && !_continuousZoomActive
        && !_manualZoomFinalizePending
        && !_absoluteZoomPending
        && !_stableZoomConfirmationPending
        && !_zoomResponseBlocked
        && !_zoomQueryOutstanding;
    if (shouldRequestZoom && !_sendCurrentZoomQuery(true)) {
        return;
    }

    _configureSdkEndpoint();
    const bool cameraStatusRequestSent = _sdk->requestCameraSystemStatus();
    if (cameraStatusRequestSent && !_sdkResponseTimer.isActive()) {
        _sdkResponseTimer.start();
    }
}

void GimbalControlManager::_markZoomStatusUnknown()
{
    const bool hadConfirmedZoom = _zoomStatusKnown;
    const bool timedOutUserTarget = _absoluteZoomPending;
    const bool timedOutDuringAlignment =
        _alignmentAttemptCount > 0 && _alignmentSourceZoomValid;
    const double timedOutAlignmentSource = _alignmentSourceZoom;
    _zoomSyncTimer.stop();
    _zoomOperationTimer.stop();
    _cancelOutstandingZoomQuery();
    _zoomResponseBlocked = false;
    _absoluteZoomPending = false;
    _absoluteZoomTracker.clear();
    if (timedOutDuringAlignment) {
        // 自动归整已发出但没有得到可判定结果时，不允许下一次空闲轮询
        // 立即重启同一轮归整。实际值、步长、能力或用户操作变化后才重试。
        _suppressAutomaticAlignment(timedOutAlignmentSource);
    }
    _alignmentAttemptCount = 0;
    _clearStableZoomConfirmation();
    if (!hadConfirmedZoom) {
        _setZoomStatusKnown(false);
    } else {
        // Retain the displayed legal target. Verification expiration never
        // replays the command and never replaces the target with a raw sample.
        _setZoomValueUncertain(false);
    }
    // 即使显示状态未改变，仍需显式通知absolute/stable pending已经清除，
    // 让QML刷新计划边界；pending不参与视频会话解锁。
    emit zoomAvailabilityChanged();
    _setLastError(tr("Timed out verifying the SIYI zoom target; the displayed legal target is retained and no old input will be replayed."));

    _requestedZoom = _currentZoom;
    if (timedOutUserTarget) {
        _suppressIdleAlignmentUntilExplicitZoom = true;
    }
    if (enabled() && _maximumZoomKnown) {
        (void) _sendCurrentZoomQuery(false);
    }
}

void GimbalControlManager::_handleZoomQueryTimeout()
{
    if (!_zoomQueryOutstanding) {
        return;
    }

    _cancelOutstandingZoomQuery();
    if (!enabled()) {
        return;
    }

    if (_continuousZoomActive) {
        // A missed 0x18 sample cannot change or reverse the elapsed-time target
        // sequence. Keep the bounded absolute-target gesture running and retry
        // verification independently.
        if (_absoluteZoomPending) {
            _scheduleZoomSync();
        }
        _continuousZoomStepTimer.start(kManualZoomPollIntervalMs);
        _setLastError(
            tr("Current SIYI zoom feedback timed out; the held target sequence is continuing."));
        return;
    }
    if (_manualZoomFinalizePending) {
        // The bounded finalize timer owns the deadline. Retry only while that
        // short window is still active; never let a lost query create a late
        // camera movement after the gesture is over.
        if (_manualZoomFinalizeDeadlineOpen()) {
            _continuousZoomStepTimer.start(kManualZoomPollIntervalMs);
        } else {
            _expireManualZoomFinalize();
        }
        return;
    }

    if (_zoomOperationTimer.isActive()
        && (_absoluteZoomPending
            || _stableZoomConfirmationPending
            || !_zoomStatusKnown)) {
        // 单个UDP查询丢失后先经过隔离窗口再重试，始终只有一个0x18查询在途。
        // 活动缩放由10秒整体deadline兜底，给下一次查询保留完整在线响应窗口。
        _sdkResponseTimer.start();
        _scheduleZoomSync();
    } else {
        // 普通空闲轮询丢一包不推翻上一次已确认倍率。
        _zoomOperationTimer.stop();
    }
}

void GimbalControlManager::_requestZoomAfterSettle()
{
    _zoomResponseBlocked = false;
    if (!enabled()
        || !_maximumZoomKnown
        || (!_absoluteZoomPending
            && !_stableZoomConfirmationPending
            && _zoomStatusKnown
            && !_zoomValueUncertain
            && !_suppressIdleAlignmentUntilExplicitZoom)) {
        return;
    }
    if (_zoomQueryOutstanding) {
        return;
    }

    _sendCurrentZoomQuery(false);
}

void GimbalControlManager::_pollContinuousZoom()
{
    if (_manualZoomFinalizePending
        && !_manualZoomFinalizeDeadlineOpen()) {
        _expireManualZoomFinalize();
        return;
    }

    if (_continuousZoomActive) {
        if (!enabled() || !_videoStreamAvailable || !_maximumZoomKnown) {
            (void) _stopContinuousZoom(false);
            return;
        }
        if (!_advanceHeldZoomDisplayTarget()) {
            (void) _stopContinuousZoom(false);
            if (_lastError.isEmpty()) {
                _setLastError(
                    tr("Failed to advance the timed SIYI zoom target."));
            }
            return;
        }
        if (_heldZoomDisplayAtTerminal()) {
            // The recording-mode endpoint is a hard logical boundary.
            // Stop native motion as soon as the timed target first reaches it;
            // never let free-running 0x05 continue toward another recording mode's
            // larger range. No post-stop 0x0f correction is issued because it
            // could reverse an overshot physical lens.
            if (!_stopContinuousZoom(false) && _lastError.isEmpty()) {
                _setLastError(
                    tr("Failed to stop SIYI zoom at the timed recording-mode limit."));
            }
            return;
        }

        // Keep elapsed-time advancement and terminal stopping on an independent
        // 120 ms cadence. Serializing it behind a diagnostic 0x18 query would
        // let a lost reply delay the recording-limit stop by the query timeout.
        // One settled 0x18 observation is requested after the real 0x05 stop.
        if (_continuousZoomActive) {
            _continuousZoomStepTimer.start(kManualZoomPollIntervalMs);
        }
        return;
    }

    if (!enabled()
        || !_maximumZoomKnown
        || !_manualZoomFinalizePending
        || _zoomResponseBlocked
        || _zoomQueryOutstanding) {
        return;
    }

    // Compatibility cleanup for a finalize transaction created by an older
    // manager session. New held gestures never enter this path.
    if (!_sendCurrentZoomQuery(false)) {
        if (_manualZoomFinalizePending) {
            _continuousZoomStepTimer.start(kManualZoomPollIntervalMs);
        }
    }
}

void GimbalControlManager::_retryManualZoomStop()
{
    (void) _sendPendingManualZoomStop();
}

void GimbalControlManager::_expireManualZoomFinalize()
{
    if (!_manualZoomFinalizePending) {
        return;
    }

    _manualZoomFinalizePending = false;
    _manualZoomFinalizeDirection = 0;
    _manualZoomFinalCandidateValid = false;
    _manualZoomFinalCandidate = kMinZoom;
    _manualZoomFinalMatchCount = 0;
    _manualZoomFinalizeElapsed.invalidate();
    _manualZoomFinalizeTimer.stop();
    _continuousZoomStepTimer.stop();
    _cancelOutstandingZoomQuery();
    // Discard any final 0x18 packet that arrives after the bounded ownership
    // window. Re-open ordinary polling only after the normal isolation delay.
    _zoomResponseBlocked = true;
    _zoomSyncTimer.start();
    _requestedZoom = _currentZoom;
    _suppressIdleAlignmentUntilExplicitZoom = true;
    // The bounded ownership window only decides whether a post-release 0x0f
    // correction may still be sent. It must not erase the latest
    // feedback-quantized legal display. The scheduled query below is read-only
    // recovery and can never replay the expired gesture.
    const bool stopRetryPending =
        _manualZoomStopRetryTimer.isActive()
        || (!_manualZoomSessionHost.isEmpty()
            && _manualZoomSessionPort != 0
            && _manualZoomStopRetryAttemptsRemaining > 0);
    if (!stopRetryPending) {
        _manualZoomSessionHost.clear();
        _manualZoomSessionPort = 0;
        _manualZoomStopRetryAttemptsRemaining = 0;
    }
    emit zoomAvailabilityChanged();
    _setLastError(
        tr("The SIYI manual zoom stopped, but its final value was not received in time; "
           "no delayed correction will be replayed."));
}

void GimbalControlManager::_stopContinuousZoomForSafety()
{
    if (!_continuousZoomActive) {
        return;
    }

    _stopContinuousZoom(false);
    _setLastError(tr("Held SIYI zoom was stopped by the safety timeout."));
}

void GimbalControlManager::_requestRecordingStatusAfterDelay()
{
    if (!_recordingCommandPending || !enabled()) {
        return;
    }

    _configureSdkEndpoint();
    if (_sdk->requestCameraSystemStatus()) {
        // 同线程事件循环不会在本函数返回前处理回包，因此成功发送后再开放响应窗口。
        _recordingStatusResponseAllowed = true;
    }
}

void GimbalControlManager::_handleRecordingCommandTimeout()
{
    if (!_recordingCommandPending) {
        return;
    }

    const bool timedOutStarting = _recordingCommandTarget;
    if (timedOutStarting) {
        _cameraRecordingStartBlocked = true;
        _setRecording(false);
    } else {
        _cameraRecordingStopBlocked = true;
    }
    _finishRecordingCommand();
    _setCameraStatusKnown(false);
    _setLastError(tr("Timed out waiting for the SIYI camera recording status."));
    _syncCameraStatus();
    _notifyRecordingSessionStateChanged();
}

void GimbalControlManager::_configureSdkEndpoint()
{
    if (!_settings || !_sdk) {
        return;
    }

    _sdk->setEndpoint(_settings->sdkHost()->rawValue().toString().trimmed(), _sdkPort());
}

bool GimbalControlManager::_sendCurrentZoomQuery(bool startOperationDeadline)
{
    if (!_sdk
        || !enabled()
        || _zoomResponseBlocked
        || _zoomQueryOutstanding) {
        return false;
    }

    _configureSdkEndpoint();
    if (!_sdk->requestCurrentZoom()) {
        return false;
    }

    _zoomQueryOutstanding = true;
    _zoomQueryTimeoutTimer.start(
        (_continuousZoomActive || _manualZoomFinalizePending)
            ? kManualZoomQueryTimeoutMs
            : kDefaultZoomQueryTimeoutMs);
    if (startOperationDeadline) {
        _zoomOperationTimer.start();
    }
    if (!_sdkResponseTimer.isActive()) {
        _sdkResponseTimer.start();
    }
    return true;
}

void GimbalControlManager::_cancelOutstandingZoomQuery()
{
    _zoomQueryTimeoutTimer.stop();
    _zoomQueryOutstanding = false;
}

bool GimbalControlManager::_sendAbsoluteZoomTarget(double zoomLevel,
                                                   bool alignmentCorrection,
                                                   bool replacePendingTarget,
                                                   bool manualFinalizeCorrection)
{
    const bool replacingPendingTarget = _absoluteZoomPending;
    if (!_sdk
        || !_maximumZoomKnown
        || (manualFinalizeCorrection
            && (replacingPendingTarget
                || !_manualZoomFinalizeDeadlineOpen()))
        || (replacingPendingTarget && !replacePendingTarget)
        || (replacePendingTarget && !replacingPendingTarget)
        || (replacePendingTarget && alignmentCorrection)
        || !qIsFinite(zoomLevel)
        || zoomLevel < kMinZoom - kZoomComparisonTolerance
        || zoomLevel > _maximumZoom + kZoomComparisonTolerance) {
        return false;
    }

    const double targetZoom =
        qRound(qBound(kMinZoom, zoomLevel, _maximumZoom) * 10.0) / 10.0;
    if (!A8MiniZoomPolicy::isAlignedZoom(targetZoom,
                                         zoomStep(),
                                         kMinZoom,
                                         _maximumZoom)) {
        _setLastError(tr("Refusing to send a SIYI zoom target outside the configured legal stops."));
        return false;
    }

    // Drain any bounded native-hold stop retry before a new 0x0f command so a
    // delayed stop can never arrive behind this absolute target.
    if (!_flushPendingManualZoomStop()) {
        return false;
    }

    _clearStableZoomConfirmation();
    _cancelOutstandingZoomQuery();
    _zoomSyncTimer.stop();
    _configureSdkEndpoint();
    if (manualFinalizeCorrection
        && !_manualZoomFinalizeDeadlineOpen()) {
        if (_manualZoomFinalizePending) {
            _expireManualZoomFinalize();
        }
        return false;
    }
    if (!_sdk->sendAbsoluteZoom(targetZoom)) {
        // A failed replacement send must not strand the old transaction after
        // its query was canceled above. Keep confirming the old target.
        if (replacingPendingTarget && _absoluteZoomPending) {
            _scheduleZoomSync();
        }
        return false;
    }
    _nativeHoldFeedbackLogPending = false;
    if (manualFinalizeCorrection) {
        // The deadline check above is adjacent to the actual UDP write. Only
        // after that write succeeds may the finalize transaction be replaced
        // by the new absolute-target confirmation transaction.
        _cancelManualZoomFinalize();
    }
    if (!alignmentCorrection) {
        // A newer explicit target owns a fresh bounded confirmation window.
        // Stop only after the replacement datagram was accepted locally so a
        // failed send cannot remove the old transaction's deadline.
        _zoomOperationTimer.stop();
    }

    // Keep only one 0x0f target in flight. A successful local send publishes
    // that legal target immediately; ACK/0x18 only verify actual lens arrival.
    if (alignmentCorrection) {
        ++_alignmentAttemptCount;
    } else {
        // 用户明确发起的新目标可以重新尝试此前被硬件拒绝的网格点。
        _clearAutomaticAlignmentSuppression();
        _alignmentAttemptCount = 0;
    }
    _requestedZoom = targetZoom;
    _absoluteZoomPending = true;
    _absoluteZoomTracker.reset(targetZoom);

    // currentZoom is the current legal target shown by QML. The independent
    // 0x18 device observation remains in _latestActualZoom and is used only to
    // confirm whether the camera has physically caught up with this target.
    _setCurrentZoom(targetZoom);
    _setZoomStatusKnown(true);
    _setZoomValueUncertain(false);
    if (!alignmentCorrection) {
        _suppressIdleAlignmentUntilExplicitZoom = true;
    }
    emit zoomAvailabilityChanged();
    if (!_zoomOperationTimer.isActive()) {
        _zoomOperationTimer.start();
    }
    _sdkResponseTimer.start();
    _scheduleZoomSync();
    return true;
}

bool GimbalControlManager::_sendAlignmentCorrection(double targetZoom,
                                                    double sourceZoom)
{
    _alignmentSourceZoom = qRound(sourceZoom * 10.0) / 10.0;
    _alignmentSourceZoomValid = true;
    const bool sent = _sendAbsoluteZoomTarget(targetZoom,
                                               true,
                                               false,
                                               false);
    if (!sent) {
        _alignmentSourceZoomValid = false;
    }
    return sent;
}

bool GimbalControlManager::_stopContinuousZoom(bool finalizeAfterStop)
{
    if (!_continuousZoomActive) {
        return true;
    }

    // Normal release takes one final time sample before stopping. Cancellation
    // intentionally preserves the last target already shown and creates no
    // extra target transition.
    const bool finalTargetResolved =
        !finalizeAfterStop || _advanceHeldZoomDisplayTarget();
    const int stoppedDirection = _continuousZoomDirection;
    const QString stopHost = _manualZoomSessionHost;
    const quint16 stopPort = _manualZoomSessionPort;

    // Clear the running state before the UDP write. A local write failure can
    // synchronously emit communicationError; this ordering prevents recursive
    // stop attempts while retaining the endpoint in local snapshots.
    _continuousZoomWatchdog.stop();
    _continuousZoomStepTimer.stop();
    _manualZoomStopRetryTimer.stop();
    _manualZoomStopRetryAttemptsRemaining = 0;
    _zoomSyncTimer.stop();
    _zoomOperationTimer.stop();
    _cancelOutstandingZoomQuery();
    _zoomResponseBlocked = true;
    _heldZoomElapsed.invalidate();
    _setContinuousZoomState(false);
    _cancelManualZoomFinalize();

    const bool stopEndpointValid =
        _sdk && !stopHost.isEmpty() && stopPort != 0;
    _manualZoomSessionHost = stopEndpointValid ? stopHost : QString();
    _manualZoomSessionPort = stopEndpointValid ? stopPort : 0;
    _manualZoomStopRetryAttemptsRemaining =
        stopEndpointValid ? kManualZoomStopMaximumRetryAttempts : 0;
    const bool stopSent =
        stopEndpointValid && _sdk->sendManualZoomTo(0, stopHost, stopPort);

    if (stopSent) {
        // UDP write success does not prove delivery. Retain one bounded safety
        // duplicate; a newer gesture flushes it before sending its start.
        _manualZoomStopRetryTimer.start(kManualZoomStopRetryMs);
    } else if (stopEndpointValid) {
        // The start may have reached the camera even when the local stop write
        // fails. Retry only stop packets and never replay the direction.
        _manualZoomStopRetryTimer.start(kManualZoomStopRetryMs);
    } else {
        _manualZoomSessionHost.clear();
        _manualZoomSessionPort = 0;
        _manualZoomStopRetryAttemptsRemaining = 0;
    }

    // Keep the legal time-derived target. A settled 0x18 value may differ
    // slightly because of camera quantization or motion timing, so it is useful
    // as observation but never as grounds for a post-release absolute command
    // that could reverse the just-finished gesture.
    _requestedZoom = _currentZoom;
    _suppressIdleAlignmentUntilExplicitZoom = true;
    _setZoomValueUncertain(false);
    if (enabled() && _maximumZoomKnown) {
        _nativeHoldFeedbackLogPending = true;
        _zoomSyncTimer.start(kManualZoomStopQueryDelayMs);
    } else {
        _nativeHoldFeedbackLogPending = false;
    }
    emit zoomAvailabilityChanged();

    qCInfo(GimbalControlLog)
        << "Stopped native continuous SIYI zoom direction"
        << stoppedDirection
        << "final display target resolved" << finalTargetResolved
        << "target" << _currentZoom
        << "stop sent" << stopSent;
    return finalTargetResolved && stopSent;
}

bool GimbalControlManager::_flushPendingManualZoomStop()
{
    if (_continuousZoomActive) {
        return false;
    }
    if (!_manualZoomStopRetryTimer.isActive()
        && (_manualZoomSessionHost.isEmpty()
            || _manualZoomSessionPort == 0
            || _manualZoomStopRetryAttemptsRemaining <= 0)) {
        return true;
    }

    _manualZoomStopRetryTimer.stop();
    return _sendPendingManualZoomStop();
}

bool GimbalControlManager::_sendPendingManualZoomStop()
{
    if (_continuousZoomActive) {
        return false;
    }
    _manualZoomStopRetryTimer.stop();
    if (_manualZoomSessionHost.isEmpty()
        || _manualZoomSessionPort == 0
        || _manualZoomStopRetryAttemptsRemaining <= 0) {
        _manualZoomSessionHost.clear();
        _manualZoomSessionPort = 0;
        _manualZoomStopRetryAttemptsRemaining = 0;
        return true;
    }

    const QString host = _manualZoomSessionHost;
    const quint16 port = _manualZoomSessionPort;
    const int attemptsBeforeSend = _manualZoomStopRetryAttemptsRemaining;
    if (!_sdk) {
        _manualZoomSessionHost.clear();
        _manualZoomSessionPort = 0;
        _manualZoomStopRetryAttemptsRemaining = 0;
        _setLastError(tr("Cannot stop SIYI manual zoom because the SDK is unavailable."));
        return false;
    }

    if (_sdk->sendManualZoomTo(0, host, port)) {
        _manualZoomSessionHost.clear();
        _manualZoomSessionPort = 0;
        _manualZoomStopRetryAttemptsRemaining = 0;
        return true;
    }

    // communicationError is emitted synchronously on a local UDP failure and
    // may clear Manager state. Restore only this bounded stop obligation; no
    // zoom movement command is retained or replayed.
    const int attemptsRemaining = qMax(0, attemptsBeforeSend - 1);
    if (attemptsRemaining > 0) {
        _manualZoomSessionHost = host;
        _manualZoomSessionPort = port;
        _manualZoomStopRetryAttemptsRemaining = attemptsRemaining;
        _manualZoomStopRetryTimer.start(kManualZoomStopRetryMs);
    } else {
        _manualZoomSessionHost.clear();
        _manualZoomSessionPort = 0;
        _manualZoomStopRetryAttemptsRemaining = 0;
    }
    return false;
}

bool GimbalControlManager::_manualZoomFinalizeDeadlineOpen() const
{
    return _manualZoomFinalizePending
        && _manualZoomFinalizeTimer.isActive()
        && _manualZoomFinalizeElapsed.isValid()
        && _manualZoomFinalizeElapsed.elapsed()
            < kManualZoomFinalizeTimeoutMs;
}

void GimbalControlManager::_cancelManualZoomFinalize()
{
    const bool wasPending = _manualZoomFinalizePending;
    _manualZoomFinalizePending = false;
    _manualZoomFinalizeDirection = 0;
    _manualZoomFinalCandidateValid = false;
    _manualZoomFinalCandidate = kMinZoom;
    _manualZoomFinalMatchCount = 0;
    _manualZoomFinalizeElapsed.invalidate();
    _manualZoomFinalizeTimer.stop();
    if (!_continuousZoomActive) {
        _continuousZoomStepTimer.stop();
    }
    if (wasPending) {
        _cancelOutstandingZoomQuery();
        _zoomResponseBlocked = false;
        emit zoomAvailabilityChanged();
    }
    // A teardown may cancel value finalization immediately after sending stop.
    // Preserve the independently bounded 80ms stop retry in that case.
    const bool stopRetryPending =
        _manualZoomStopRetryTimer.isActive()
        || (!_manualZoomSessionHost.isEmpty()
            && _manualZoomSessionPort != 0
            && _manualZoomStopRetryAttemptsRemaining > 0);
    if (!stopRetryPending && !_continuousZoomActive) {
        _manualZoomSessionHost.clear();
        _manualZoomSessionPort = 0;
        _manualZoomStopRetryAttemptsRemaining = 0;
    }
}

void GimbalControlManager::_finishManualZoomStop(double zoomLevel)
{
    if (!_manualZoomFinalizePending || !qIsFinite(zoomLevel)) {
        return;
    }

    // Compatibility cleanup only. Current native holds never enter a finalize
    // transaction, and even an unexpected legacy state must remain read-only:
    // delayed or quantized 0x18 feedback is not sufficient grounds for an
    // absolute correction which could reverse the lens.
    _cancelManualZoomFinalize();
    _nativeHoldTargetLatched = true;
    _requestedZoom = _currentZoom;
    _suppressIdleAlignmentUntilExplicitZoom = true;
    _setZoomValueUncertain(false);
    qCWarning(GimbalControlLog)
        << "Discarded legacy SIYI manual-finalize sample" << zoomLevel
        << "without sending an absolute correction";
}

GimbalControlManager::AlignmentAttemptResult
GimbalControlManager::_tryRealignStableZoom(double zoomLevel, int direction)
{
    if (_nativeHoldTargetLatched) {
        // Defense in depth: reconnect/settings code may request a stable
        // normalization pass, but a native hold owns no delayed movement.
        return AlignmentAttemptResult::NotNeeded;
    }
    if (_automaticAlignmentSuppressedFor(zoomLevel)) {
        return AlignmentAttemptResult::NotNeeded;
    }
    if (_automaticAlignmentSuppressed) {
        // 实际倍率、配置步长或有效上限已经变化，旧抑制条件失效。
        _clearAutomaticAlignmentSuppression();
        _alignmentAttemptCount = 0;
    }

    if (_alignmentAttemptCount >= kMaximumAlignmentAttempts) {
        _suppressAutomaticAlignment(zoomLevel);
        return AlignmentAttemptResult::NotNeeded;
    }

    const double boundedZoom =
        qBound(kMinZoom, zoomLevel, _maximumZoom);
    double alignedZoom = 0.0;
    const int normalizedDirection = qBound(-1, direction, 1);
    const bool targetResolved = normalizedDirection == 0
        ? A8MiniZoomPolicy::alignmentTarget(boundedZoom,
                                            zoomStep(),
                                            kMinZoom,
                                            _maximumZoom,
                                            0,
                                            &alignedZoom)
        : A8MiniZoomPolicy::stepTarget(zoomLevel,
                                      zoomStep(),
                                      kMinZoom,
                                      _maximumZoom,
                                      normalizedDirection,
                                      &alignedZoom);
    if (!targetResolved
        || qAbs(alignedZoom - zoomLevel) <= kZoomComparisonTolerance) {
        _suppressAutomaticAlignment(zoomLevel);
        return AlignmentAttemptResult::NotNeeded;
    }

    _absoluteZoomPending = false;
    _absoluteZoomTracker.clear();
    if (!_sendAlignmentCorrection(alignedZoom, zoomLevel)) {
        return AlignmentAttemptResult::SendFailed;
    }
    return AlignmentAttemptResult::CommandSent;
}

bool GimbalControlManager::_automaticAlignmentSuppressedFor(double zoomLevel) const
{
    return _automaticAlignmentSuppressed
        && qAbs(zoomLevel - _suppressedAlignmentZoom) <= kZoomComparisonTolerance
        && qAbs(zoomStep() - _suppressedAlignmentStep) <= kZoomComparisonTolerance
        && qAbs(_maximumZoom - _suppressedAlignmentMaximum) <= kZoomComparisonTolerance;
}

void GimbalControlManager::_suppressAutomaticAlignment(double zoomLevel)
{
    _automaticAlignmentSuppressed = true;
    _suppressedAlignmentZoom = qRound(zoomLevel * 10.0) / 10.0;
    _suppressedAlignmentStep = zoomStep();
    _suppressedAlignmentMaximum = _maximumZoom;
    _alignmentSourceZoomValid = false;
}

void GimbalControlManager::_clearAutomaticAlignmentSuppression()
{
    _automaticAlignmentSuppressed = false;
    _alignmentSourceZoomValid = false;
}

void GimbalControlManager::_clearStableZoomConfirmation()
{
    const bool wasPending = _stableZoomConfirmationPending;
    _stableZoomConfirmationPending = false;
    _stableZoomCandidateValid = false;
    _stableZoomCandidate = kMinZoom;
    _normalizeAfterStableZoom = false;
    _stableZoomDirection = 0;
    if (wasPending) {
        emit zoomAvailabilityChanged();
    }
}

void GimbalControlManager::_beginStableZoomConfirmation(bool normalizeToStepGrid,
                                                        int direction)
{
    const bool wasPending = _stableZoomConfirmationPending;
    _stableZoomConfirmationPending = true;
    _stableZoomCandidateValid = false;
    _stableZoomCandidate = kMinZoom;
    _normalizeAfterStableZoom = normalizeToStepGrid;
    _stableZoomDirection = qBound(-1, direction, 1);
    // A stable-value check must not blank an already confirmed legal stop.
    // It will atomically replace that stop only after the new feedback has
    // been resolved to a legal stop.
    if (!wasPending) {
        emit zoomAvailabilityChanged();
    }
    // 任何pending状态都必须有总截止时间。只在尚未计时时启动，候选值
    // 变化或UDP重试不能刷新10秒deadline。
    if (enabled() && !_zoomOperationTimer.isActive()) {
        _zoomOperationTimer.start();
    }
}

void GimbalControlManager::_finalizeConfirmedZoom(double zoomLevel)
{
    const bool completedAbsoluteTarget = _absoluteZoomPending;
    const double completedRequestedZoom = _requestedZoom;
    const double normalizedZoom = qRound(zoomLevel * 10.0) / 10.0;
    if (!A8MiniZoomPolicy::isAlignedZoom(normalizedZoom,
                                         zoomStep(),
                                         kMinZoom,
                                         _maximumZoom)) {
        // Defense in depth: no recovery branch is ever allowed to publish a
        // transient value such as 1.6x or 1.8x.
        qCWarning(GimbalControlLog)
            << "Refusing to publish off-grid SIYI zoom" << normalizedZoom
            << "step" << zoomStep() << "maximum" << _maximumZoom;
        const bool hadConfirmedZoom = _zoomStatusKnown;
        _zoomSyncTimer.stop();
        _zoomOperationTimer.stop();
        _cancelOutstandingZoomQuery();
        _zoomResponseBlocked = false;
        _absoluteZoomPending = false;
        _absoluteZoomTracker.clear();
        _alignmentAttemptCount = 0;
        _clearStableZoomConfirmation();
        _requestedZoom = _currentZoom;
        if (hadConfirmedZoom) {
            _setZoomValueUncertain(false);
        } else {
            _setZoomStatusKnown(false);
        }
        emit zoomAvailabilityChanged();
        return;
    }

    const bool controlsWereWaiting = !_zoomStatusKnown;
    _zoomSyncTimer.stop();
    _zoomOperationTimer.stop();
    _cancelOutstandingZoomQuery();
    _zoomResponseBlocked = false;
    _absoluteZoomPending = false;
    _absoluteZoomTracker.clear();
    _alignmentAttemptCount = 0;
    _clearAutomaticAlignmentSuppression();
    _clearStableZoomConfirmation();
    _requestedZoom = normalizedZoom;
    // _handleCurrentZoom records the solicited device value before publishing
    // the same legal stop. Do not replace that evidence with command/ACK data.
    if (!_latestActualZoomKnown) {
        _latestActualZoom = normalizedZoom;
        _latestActualZoomKnown = true;
    }
    _setCurrentZoom(normalizedZoom);
    _setZoomValueUncertain(false);
    _setZoomStatusKnown(true);
    emit zoomAvailabilityChanged();
    _setLastError(QString());

    if (completedAbsoluteTarget) {
        qCInfo(GimbalControlLog)
            << "Confirmed SIYI absolute zoom target" << completedRequestedZoom
            << "at actual" << normalizedZoom;
    } else if (controlsWereWaiting) {
        qCInfo(GimbalControlLog)
            << "Confirmed stable SIYI zoom" << zoomLevel
            << "- zoom controls are ready";
    }

}

void GimbalControlManager::_resetMaximumZoomCapability()
{
    // Neither a pulled-video size nor a stale device maximum may substitute
    // for the recording-stream resolution. A fresh 0x20 reply is required
    // before zoom controls can be unlocked again.
    _recordingCapabilityTimeoutTimer.stop();
    _recordingResolutionConfirmed = false;
    _recordingVideoSize = QSize();
    _recordingResolutionMaximumZoom = kDefaultMaxZoom;
    _deviceMaximumZoomKnown = false;
    _deviceMaximumZoom = kProtocolMaxZoom;
    _nativeHoldTargetLatched = false;
    _nativeHoldFeedbackLogPending = false;
    _clearAutomaticAlignmentSuppression();
    _setMaximumZoomKnown(false);
    _capabilityMaximumZoom = kDefaultMaxZoom;
    double alignedMaximum = kMinZoom;
    if (A8MiniZoomPolicy::alignedMaximumZoom(kDefaultMaxZoom,
                                              zoomStep(),
                                              kMinZoom,
                                              &alignedMaximum)) {
        _setMaximumZoom(alignedMaximum);
    }
    _clearStableZoomConfirmation();
}

void GimbalControlManager::_scheduleZoomSync()
{
    _cancelOutstandingZoomQuery();
    _zoomResponseBlocked = true;
    _zoomSyncTimer.start();
}

void GimbalControlManager::_setCurrentZoom(double zoomLevel)
{
    // currentZoom is the published legal target. It may come from a locally
    // accepted tap 0x0f, a time-derived native-hold target, or a validated idle
    // device observation. Off-grid/intermediate device samples stay private.
    if (!qIsFinite(zoomLevel)
        || !A8MiniZoomPolicy::isAlignedZoom(zoomLevel,
                                            zoomStep(),
                                            kMinZoom,
                                            _maximumZoom)) {
        qCWarning(GimbalControlLog)
            << "Rejected non-publishable SIYI current zoom" << zoomLevel;
        return;
    }
    if (!qFuzzyCompare(_currentZoom + 1.0, zoomLevel + 1.0)) {
        _currentZoom = zoomLevel;
        emit currentZoomChanged();
        emit zoomAvailabilityChanged();
    }
}

void GimbalControlManager::_setMaximumZoom(double zoomLevel)
{
    if (!qIsFinite(zoomLevel)) {
        return;
    }

    const double normalizedZoom = qRound(qBound(kMinZoom, zoomLevel, kProtocolMaxZoom) * 10.0) / 10.0;
    if (!qFuzzyCompare(_maximumZoom + 1.0, normalizedZoom + 1.0)) {
        _maximumZoom = normalizedZoom;
        _clearAutomaticAlignmentSuppression();
        emit maximumZoomChanged();
        emit zoomAvailabilityChanged();

        if (_zoomStatusKnown && _currentZoom > _maximumZoom + 0.05) {
            _absoluteZoomPending = false;
            _absoluteZoomTracker.clear();
            _alignmentAttemptCount = 0;
            _requestedZoom = _currentZoom;
            _setZoomStatusKnown(false);
        }
    }
}

void GimbalControlManager::_setMaximumZoomKnown(bool known)
{
    if (_maximumZoomKnown != known) {
        _maximumZoomKnown = known;
        emit zoomAvailabilityChanged();
    }
}

void GimbalControlManager::_setVideoStreamAvailable(bool available)
{
    if (_videoStreamAvailable == available) {
        return;
    }

    _videoStreamAvailable = available;
    emit zoomAvailabilityChanged();
    _notifyRecordingSessionStateChanged();
}

void GimbalControlManager::_setSdkResponding(bool responding)
{
    if (_sdkResponding != responding) {
        _sdkResponding = responding;
        emit sdkRespondingChanged();
    }
}

void GimbalControlManager::_setContinuousZoomState(bool active, int direction)
{
    const bool activeChanged = _continuousZoomActive != active;
    _continuousZoomActive = active;
    _continuousZoomDirection = active ? direction : 0;
    if (activeChanged) {
        emit continuousZoomActiveChanged();
        emit zoomAvailabilityChanged();
    }
}

void GimbalControlManager::_setCameraStatusKnown(bool known)
{
    if (_cameraStatusKnown != known) {
        _cameraStatusKnown = known;
        emit cameraStatusKnownChanged();
        _notifyRecordingSessionStateChanged();
    }
}

void GimbalControlManager::_setRecording(bool recording)
{
    if (_recording != recording) {
        _recording = recording;
        emit recordingChanged();
        _notifyRecordingSessionStateChanged();
    }
}

void GimbalControlManager::_setRecordingCommandPending(bool pending)
{
    if (_recordingCommandPending != pending) {
        _recordingCommandPending = pending;
        emit recordingCommandPendingChanged();
        _notifyRecordingSessionStateChanged();
    }
}

void GimbalControlManager::_finishRecordingCommand()
{
    _recordingStatusDelayTimer.stop();
    _recordingCommandTimeoutTimer.stop();
    _recordingCommandTarget = false;
    _recordingStatusResponseAllowed = false;
    _setRecordingCommandPending(false);
}

bool GimbalControlManager::_startRecordingSession()
{
    if (recordingSessionActive()) {
        return false;
    }

    const bool requestLocal = localMediaStorageEnabled();
    const bool requestCamera = enabled();
    if (!requestLocal && !requestCamera) {
        return false;
    }

    _recordingSessionRequested = true;
    _localRecordingIntent = requestLocal;
    _localRecordingStartBlocked = false;
    _localRecordingResumeOnStream = false;
    _localRecordingRetryAfterGenerationResolved =
        requestLocal && !_localRecordingIssuedFileBases.isEmpty();

    _cameraRecordingIntentValid = requestCamera;
    _cameraRecordingIntentTarget = true;
    _cameraRecordingStartBlocked = false;
    _cameraRecordingStopBlocked = false;

    if (_localRecordingIssuedFileBases.isEmpty()) {
        _setLocalMediaError(QString());
    }
    _notifyRecordingSessionStateChanged();

    _reconcileLocalRecording();
    _reconcileCameraRecordingIntent();
    return true;
}

bool GimbalControlManager::_stopRecordingSession()
{
    const bool hadActiveSession = recordingSessionActive();
    if (!hadActiveSession) {
        return false;
    }

    _recordingSessionRequested = false;
    _localRecordingIntent = false;
    _localRecordingResumeOnStream = false;
    _localRecordingRetryAfterGenerationResolved = false;

    const bool cameraMayNeedStop = enabled()
        && (_recording
            || _recordingCommandPending
            || _cameraRecordingIntentValid);
    _cameraRecordingIntentValid = cameraMayNeedStop;
    _cameraRecordingIntentTarget = false;
    _cameraRecordingStartBlocked = false;
    _cameraRecordingStopBlocked = false;

    _notifyRecordingSessionStateChanged();
    _reconcileLocalRecording();
    _reconcileCameraRecordingIntent();
    return true;
}

bool GimbalControlManager::_sendCameraRecordingToggle(bool targetRecording)
{
    if (!_sdk
        || !enabled()
        || !_cameraStatusKnown
        || _recordingCommandPending
        || _recording == targetRecording) {
        return false;
    }

    _configureSdkEndpoint();
    if (!_sdk->toggleVideoRecording()) {
        if (targetRecording) {
            _cameraRecordingStartBlocked = true;
        } else {
            _cameraRecordingStopBlocked = true;
        }
        return false;
    }

    // 0x0c has no direct ACK. Publish a pending optimistic state, ignore old
    // in-flight 0x0a replies, then query for a matching confirmed state.
    _recordingCommandTarget = targetRecording;
    _recordingStatusResponseAllowed = false;
    _recordingStatusDelayTimer.start();
    _recordingCommandTimeoutTimer.start();
    _setRecordingCommandPending(true);
    _setRecording(targetRecording);
    _setLastError(QString());
    return true;
}

void GimbalControlManager::_reconcileCameraRecordingIntent()
{
    if (!_cameraRecordingIntentValid || _recordingCommandPending) {
        return;
    }

    if (!enabled() || !_sdk) {
        _cameraRecordingIntentValid = false;
        _notifyRecordingSessionStateChanged();
        return;
    }

    if (_cameraRecordingIntentTarget && _cameraRecordingStartBlocked) {
        return;
    }

    if (!_cameraStatusKnown) {
        _syncCameraStatus();
        return;
    }

    if (_recording == _cameraRecordingIntentTarget) {
        if (!_cameraRecordingIntentTarget) {
            _cameraRecordingIntentValid = false;
            _cameraRecordingStopBlocked = false;
        }
        _notifyRecordingSessionStateChanged();
        return;
    }

    if (!_cameraRecordingIntentTarget && _cameraRecordingStopBlocked) {
        return;
    }

    (void) _sendCameraRecordingToggle(_cameraRecordingIntentTarget);
}

bool GimbalControlManager::_captureLocalVideoFrame()
{
    if (_localPhotoCapturePending
        || _localPhotoGrabLifetimeCount > 0) {
        _setLocalMediaError(
            tr("A local photo is still being processed."));
        qCWarning(GimbalControlLog)
            << "Ignoring local photo request while the previous frame is still in flight";
        return false;
    }

    // Advance the generation before validating the request. It also makes a
    // timed-out or explicitly cancelled grab/save completion harmless.
    const quint64 requestSequence = ++_localPhotoRequestSequence;

    if (!_videoManager
        || !_videoManager->decoding()
        || !_mainVideoItem
        || _mainVideoItem->width() <= 0.0
        || _mainVideoItem->height() <= 0.0) {
        _setLocalMediaError(
            tr("No decoded video frame is available for a local photo."));
        return false;
    }

    AppSettings* appSettings =
        SettingsManager::instance()->appSettings();
    const QString photoDirectory = localMediaSaveDirectory(
        appSettings,
        LocalMediaKind::Photo);
    if (photoDirectory.isEmpty()
        || (!QDir(photoDirectory).exists()
            && !QDir().mkpath(photoDirectory))
        || !QFileInfo(photoDirectory).isWritable()) {
        _setLocalMediaError(
            tr("The local photo save path is unavailable or not writable."));
        return false;
    }

    const QString filename = QDir(photoDirectory).filePath(
        QStringLiteral("%1_local_%2.jpg")
            .arg(QDateTime::currentDateTime().toString(
                     QStringLiteral("yyyy-MM-dd_hh.mm.ss.zzz")))
            .arg(requestSequence,
                 3,
                 10,
                 QLatin1Char('0')));

    QQuickWindow* videoWindow = _mainVideoItem->window();
    const qreal reportedDevicePixelRatio = videoWindow
        ? videoWindow->effectiveDevicePixelRatio()
        : 1.0;
    const qreal effectiveDevicePixelRatio =
        qIsFinite(reportedDevicePixelRatio)
            && reportedDevicePixelRatio > 0.0
        ? reportedDevicePixelRatio
        : 1.0;
    const QSize itemPixelSize(
        qMax(1,
             qRound(_mainVideoItem->width()
                    * effectiveDevicePixelRatio)),
        qMax(1,
             qRound(_mainVideoItem->height()
                    * effectiveDevicePixelRatio)));
    const QSize implicitVideoSize(
        qRound(_mainVideoItem->implicitWidth()),
        qRound(_mainVideoItem->implicitHeight()));
    const QSize videoManagerSize = _videoManager->videoSize();

    QSize sourcePixelSize = _negotiatedPulledVideoSize;
    const char* sourceSizeDescription = "negotiated main stream";
    if (!sourcePixelSize.isValid() || sourcePixelSize.isEmpty()) {
        sourcePixelSize = videoManagerSize;
        sourceSizeDescription = "VideoManager fallback";
    }
    if (!sourcePixelSize.isValid() || sourcePixelSize.isEmpty()) {
        sourcePixelSize = implicitVideoSize;
        sourceSizeDescription = "video item implicit-size fallback";
    }
    if (!sourcePixelSize.isValid() || sourcePixelSize.isEmpty()) {
        sourcePixelSize = itemPixelSize;
        sourceSizeDescription = "video item physical-size fallback";
    }

    const bool usingRecordingResolution =
        _recordingResolutionConfirmed
        && _recordingVideoSize.isValid()
        && !_recordingVideoSize.isEmpty();
    const QSize outputPixelSize = usingRecordingResolution
        ? _recordingVideoSize
        : sourcePixelSize;
    const auto captureGeometry =
        GimbalPhotoCapturePolicy::captureGeometry(
            outputPixelSize,
            sourcePixelSize,
            effectiveDevicePixelRatio);
    if (!captureGeometry.isValid()) {
        _setLocalMediaError(
            tr("Failed to capture the local video frame."));
        qCWarning(GimbalControlLog)
            << "Rejected invalid local photo geometry"
            << "recording" << _recordingVideoSize
            << "recording confirmed" << _recordingResolutionConfirmed
            << "source" << sourcePixelSize
            << "DPR" << effectiveDevicePixelRatio;
        return false;
    }

    qCInfo(GimbalControlLog)
        << "Starting local camera-frame capture"
        << "output" << captureGeometry.outputPixelSize
        << "output source"
        << (usingRecordingResolution
                ? "SIYI recording stream 0x20"
                : sourceSizeDescription)
        << "decoded source" << sourcePixelSize
        << "negotiated" << _negotiatedPulledVideoSize
        << "VideoManager" << videoManagerSize
        << "item DIP"
        << QSize(qRound(_mainVideoItem->width()),
                 qRound(_mainVideoItem->height()))
        << "item implicit" << implicitVideoSize
        << "DPR" << effectiveDevicePixelRatio
        << "content" << captureGeometry.contentPixelSize
        << "grab logical target" << captureGeometry.grabLogicalSize
        << "JPEG quality" << kLocalPhotoJpegQuality;

    const QSharedPointer<QQuickItemGrabResult> result =
        _mainVideoItem->grabToImage(captureGeometry.grabLogicalSize);
    if (!result || !videoWindow) {
        _setLocalMediaError(tr("Failed to capture the local video frame."));
        return false;
    }

    auto* const grabLifetime =
        new LocalPhotoGrabLifetime(result, videoWindow);
    ++_localPhotoGrabLifetimeCount;
    connect(grabLifetime,
            &QObject::destroyed,
            this,
            [this]() {
                Q_ASSERT(_localPhotoGrabLifetimeCount > 0);
                --_localPhotoGrabLifetimeCount;
                if (!_localPhotoCapturePending
                    && _localPhotoGrabLifetimeCount == 0
                    && _localMediaError
                        == tr("A local photo is still being processed.")) {
                    _setLocalMediaError(QString());
                }
            });
    _localPhotoCapturePending = true;
    _localPhotoGrabLifetime = grabLifetime;
    const QPointer<QObject> guardedGrabLifetime(grabLifetime);
    const QWeakPointer<QQuickItemGrabResult> weakResult =
        result.toWeakRef();
    _setLocalMediaError(QString());
    connect(result.data(),
            &QQuickItemGrabResult::ready,
            this,
            [this,
             guardedGrabLifetime,
             weakResult,
             filename,
             requestSequence,
             captureGeometry]() {
                const QSharedPointer<QQuickItemGrabResult> result =
                    weakResult.toStrongRef();
                if (!result
                    || !guardedGrabLifetime
                    || guardedGrabLifetime != _localPhotoGrabLifetime) {
                    return;
                }

                _localPhotoCaptureTimer.stop();
                _localPhotoGrabLifetime.clear();
                QImage grabbedImage = result->image();
                const QSize grabbedPixelSize = grabbedImage.size();
                if (grabbedImage.isNull()) {
                    _localPhotoCapturePending = false;
                    if (requestSequence == _localPhotoRequestSequence) {
                        _setLocalMediaError(
                            tr("Failed to capture the local video frame."));
                    }
                    qCWarning(GimbalControlLog)
                        << "Local camera-frame grab produced no usable image"
                        << "raw" << grabbedPixelSize
                        << "expected content"
                        << captureGeometry.contentPixelSize
                        << "expected output"
                        << captureGeometry.outputPixelSize;
                    return;
                }

                _localPhotoSaveThreadPool.start(
                    [this,
                     grabbedImage = std::move(grabbedImage),
                     filename,
                     requestSequence,
                     grabbedPixelSize,
                     captureGeometry]() mutable {
                        const LocalPhotoSaveOutcome outcome =
                            saveLocalPhotoImage(
                                std::move(grabbedImage),
                                captureGeometry,
                                filename);
                        const bool completionQueued =
                            QMetaObject::invokeMethod(
                                this,
                                [this,
                                 outcome,
                                 filename,
                                 requestSequence,
                                 grabbedPixelSize,
                                 captureGeometry]() {
                                    if (requestSequence
                                        == _localPhotoRequestSequence) {
                                        _localPhotoCapturePending = false;
                                    }

                                    if (!outcome.success) {
                                        if (requestSequence
                                            == _localPhotoRequestSequence) {
                                            _setLocalMediaError(
                                                tr("Failed to save the local video frame."));
                                        }
                                        qCWarning(GimbalControlLog)
                                            << "Failed to save local camera frame"
                                            << filename << outcome.error
                                            << "raw" << grabbedPixelSize
                                            << "expected output"
                                            << captureGeometry.outputPixelSize;
                                        return;
                                    }

                                    ++_localPhotoCount;
                                    emit localPhotoCountChanged();
                                    if (_localMediaError
                                        == tr("A local photo is still being processed.")) {
                                        _setLocalMediaError(QString());
                                    }
                                    qCInfo(GimbalControlLog)
                                        << "Saved local camera frame" << filename
                                        << "raw grab" << grabbedPixelSize
                                        << "output" << outcome.outputPixelSize
                                        << "bytes" << outcome.fileSize;
                                    registerLocalMediaFile(filename);
                                },
                                Qt::QueuedConnection);
                        if (!completionQueued) {
                            qCWarning(GimbalControlLog)
                                << "Failed to queue local photo completion"
                                << filename;
                        }
                    });
            },
            Qt::SingleShotConnection);
    connect(result.data(),
            &QQuickItemGrabResult::ready,
            grabLifetime,
            [grabLifetime]() {
                // Deferred deletion keeps the result alive until ready()
                // returns, including every manager-side image consumer.
                grabLifetime->deleteLater();
            },
            Qt::SingleShotConnection);
    _localPhotoCaptureTimer.start();
    return true;
}

void GimbalControlManager::_reconcileLocalRecording()
{
    if (!_videoManager) {
        if (_localRecordingIntent) {
            _setLocalMediaError(
                tr("Local video recording is unavailable."));
        }
        return;
    }

    GimbalMediaSessionPolicy::LocalState state;
    state.intent = _localRecordingIntent;
    state.settingEnabled = localMediaStorageEnabled();
    state.streaming = _videoManager->streaming();
    state.actualRecording = _videoManager->recording();
    state.active = _localRecordingActive;
    // A start call is only provisional. The matching main-receiver result is
    // the authority which promotes it to an owned global recorder session.
    state.owned = _localRecordingOwned
        && _localRecordingOwnershipConfirmed;
    state.usingExternal = _localRecordingUsingExternalSession;
    state.startPending = _localRecordingStartPending;
    state.stopPending = _localRecordingStopPending;
    state.startBlocked = _localRecordingStartBlocked;

    switch (GimbalMediaSessionPolicy::localAction(state)) {
    case GimbalMediaSessionPolicy::StartOwned:
        _startLocalRecording();
        return;
    case GimbalMediaSessionPolicy::StopOwned:
    case GimbalMediaSessionPolicy::ReleaseExternal:
        _stopLocalRecording();
        return;
    case GimbalMediaSessionPolicy::AdoptExternal:
        _localRecordingOwned = false;
        _localRecordingOwnershipConfirmed = false;
        _localRecordingUsingExternalSession = true;
        _setLocalRecordingActive(true);
        _setLocalMediaError(QString());
        return;
    case GimbalMediaSessionPolicy::ConfirmOwned:
        _localRecordingUsingExternalSession = false;
        _setLocalRecordingActive(true);
        _setLocalMediaError(QString());
        return;
    case GimbalMediaSessionPolicy::None:
        break;
    }

    if (_localRecordingIntent
        && localMediaStorageEnabled()
        && !_videoManager->streaming()
        && !_videoManager->recording()
        && !_localRecordingStartPending
        && !_localRecordingStopPending) {
        _setLocalRecordingActive(false);
        _localRecordingResumeOnStream = true;
        _setLocalMediaError(
            tr("Local video recording requires an active video stream."));
    }
}

void GimbalControlManager::_startLocalRecording()
{
    if (!_videoManager
        || !_localRecordingIntent
        || !localMediaStorageEnabled()
        || _localRecordingStartPending
        || _localRecordingStopPending
        || _localRecordingStartBlocked) {
        return;
    }

    if (!_localRecordingIssuedFileBases.isEmpty()) {
        _localRecordingRetryAfterGenerationResolved = true;
        return;
    }

    if (_videoManager->recording()) {
        _localRecordingOwned = false;
        _localRecordingOwnershipConfirmed = false;
        _localRecordingUsingExternalSession = true;
        _setLocalRecordingActive(true);
        _notifyRecordingSessionStateChanged();
        return;
    }

    if (!_videoManager->streaming()) {
        _setLocalMediaError(
            tr("Local video recording requires an active video stream."));
        return;
    }

    if (!_mainVideoReceiver || !_mainVideoReceiver->started()) {
        _localRecordingStartBlocked = true;
        _setLocalMediaError(
            tr("Local video recording is unavailable."));
        _notifyRecordingSessionStateChanged();
        return;
    }

    AppSettings* appSettings =
        SettingsManager::instance()->appSettings();
    VideoSettings* videoSettings =
        SettingsManager::instance()->videoSettings();
    const QString videoDirectory = localMediaSaveDirectory(
        appSettings,
        LocalMediaKind::Video);
    if (videoDirectory.isEmpty()
        || (!QDir(videoDirectory).exists()
            && !QDir().mkpath(videoDirectory))
        || !QFileInfo(videoDirectory).isWritable()) {
        _localRecordingStartBlocked = true;
        _setLocalMediaError(
            tr("The local video save path is unavailable or not writable."));
        _notifyRecordingSessionStateChanged();
        return;
    }

    const VideoReceiver::FILE_FORMAT fileFormat =
        videoSettings
        ? static_cast<VideoReceiver::FILE_FORMAT>(
              videoSettings->recordingFormat()->rawValue().toInt())
        : VideoReceiver::FILE_FORMAT_MAX;
    if (!VideoReceiver::isValidFileFormat(fileFormat)) {
        _localRecordingStartBlocked = true;
        _setLocalMediaError(
            tr("The configured local video format is invalid."));
        _notifyRecordingSessionStateChanged();
        return;
    }

    cleanupOldLocalVideos(videoSettings, videoDirectory);

    _localRecordingFileBase =
        QStringLiteral("%1_local_%2")
            .arg(QDateTime::currentDateTime().toString(
                     QStringLiteral("yyyy-MM-dd_hh.mm.ss.zzz")))
            .arg(++_localRecordingSegmentCounter,
                 3,
                 10,
                 QLatin1Char('0'));
    _localRecordingIssuedFileBases.append(_localRecordingFileBase);
    _localRecordingResumeOnStream = false;
    _localRecordingRetryAfterGenerationResolved = false;
    _localRecordingOwned = true;
    _localRecordingOwnershipConfirmed = false;
    _localRecordingUsingExternalSession = false;
    _localRecordingStartPending = true;
    _localRecordingStopRetryCount = 0;
    _localRecordingStartTimer.start();
    _setLocalMediaError(QString());
    emit localRecordingStateChanged();
    _notifyRecordingSessionStateChanged();

    qCInfo(GimbalControlLog)
        << "Starting owned local video segment"
        << _localRecordingFileBase;
    const QString outputFile = QDir(videoDirectory).filePath(
        _localRecordingFileBase
        + QLatin1Char('.')
        + QLatin1String(kLocalRecordingFileExtensions[fileFormat]));
    _localRecordingOutputFile = outputFile;
    _mainVideoReceiver->startRecording(outputFile, fileFormat);
    if (_videoManager->recording()) {
        _handleVideoRecordingChanged();
    }
}

void GimbalControlManager::_stopLocalRecording()
{
    if (!_localRecordingActive
        && !_localRecordingStartPending
        && !_localRecordingOwned
        && !_localRecordingUsingExternalSession) {
        return;
    }

    if (_localRecordingUsingExternalSession && !_localRecordingOwned) {
        _localRecordingUsingExternalSession = false;
        _localRecordingOwnershipConfirmed = false;
        _setLocalRecordingActive(false);
        return;
    }

    if (_localRecordingOwned && !_localRecordingOwnershipConfirmed) {
        // A start request is not ours until its success callback matches the
        // requested basename. Defer cancellation instead of risking a stop of
        // a concurrent recorder which won the main receiver first.
        return;
    }

    if (!_localRecordingOwned || !_mainVideoReceiver) {
        _localRecordingStartTimer.stop();
        _localRecordingStartPending = false;
        _localRecordingOwned = false;
        _localRecordingOwnershipConfirmed = false;
        _localRecordingUsingExternalSession = false;
        const bool wasActive = _localRecordingActive;
        _setLocalRecordingActive(false);
        if (!wasActive) {
            emit localRecordingStateChanged();
            _notifyRecordingSessionStateChanged();
        }
        return;
    }

    if (_localRecordingStopPending) {
        return;
    }

    _localRecordingStartTimer.stop();
    _localRecordingStartPending = false;
    _localRecordingStopPending = true;
    _localRecordingStopRetryCount = 0;
    _localRecordingStopTimer.start();
    emit localRecordingStateChanged();
    _notifyRecordingSessionStateChanged();

    qCInfo(GimbalControlLog)
        << "Stopping owned local video segment"
        << _localRecordingFileBase;
    _mainVideoReceiver->stopRecording();
}

bool GimbalControlManager::_setLocalRecordingActive(bool active)
{
    if (_localRecordingActive == active) {
        return false;
    }

    _localRecordingActive = active;
    emit localRecordingStateChanged();
    _notifyRecordingSessionStateChanged();
    return true;
}

void GimbalControlManager::_setLocalMediaError(const QString& message)
{
    if (_localMediaError == message) {
        return;
    }

    _localMediaError = message;
    emit localMediaErrorChanged();
}

void GimbalControlManager::_notifyRecordingSessionStateChanged()
{
    emit recordingSessionStateChanged();
}

void GimbalControlManager::_syncCameraStatus()
{
    if (!enabled() || !_sdk) {
        return;
    }

    _configureSdkEndpoint();
    if (_sdk->requestCameraSystemStatus()) {
        _sdkResponseTimer.start();
    }
}

void GimbalControlManager::_setLastError(const QString& message)
{
    if (_lastError != message) {
        _lastError = message;
        emit lastErrorChanged();
    }
}

bool GimbalControlManager::_cameraCommandAvailable()
{
    if (!enabled()) {
        _setLastError(tr("SIYI gimbal camera control is disabled."));
        return false;
    }

    // sdkResponding describes recent validated replies, not whether UDP can send.
    // Keep zoom and photo usable across a transient probe timeout. Local send
    // failures are still reported through SiyiSdk::communicationError.
    return true;
}

void GimbalControlManager::_setZoomStatusKnown(bool known)
{
    if (!known) {
        _setZoomValueUncertain(false);
    }
    if (_zoomStatusKnown != known) {
        _zoomStatusKnown = known;
        emit zoomStatusKnownChanged();
        emit zoomAvailabilityChanged();
    }
}

void GimbalControlManager::_setZoomValueUncertain(bool uncertain)
{
    if (_zoomValueUncertain != uncertain) {
        _zoomValueUncertain = uncertain;
        emit zoomAvailabilityChanged();
    }
}

quint16 GimbalControlManager::_sdkPort() const
{
    const uint port = _settings ? _settings->sdkPort()->rawValue().toUInt() : 37260;
    return static_cast<quint16>(qBound(1u, port, 65535u));
}
