/****************************************************************************
 *
 * UniPod MT11 camera-control and local-media coordinator.
 *
 ****************************************************************************/

#include "Mt11ControlManager.h"

#include "Android/AndroidMediaLibrary.h"
#include "AppSettings.h"
#include "Fact.h"
#include "GimbalControlSettings.h"
#include "GimbalPhotoCapturePolicy.h"
#include "Mt11Protocol.h"
#include "Mt11Sdk.h"
#include "Mt11ZoomPolicy.h"
#include "QGCLoggingCategory.h"
#include "SettingsManager.h"
#include "VideoManager/VideoReceiver/VideoReceiver.h"
#include "VideoSettings.h"

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

QGC_LOGGING_CATEGORY(Mt11ControlLog, "gcs.custom.gimbal.mt11.control")

constexpr double kZoomTolerance = 0.051;
constexpr int kZoomFeedbackFreshnessMs = 6500;
constexpr int kAbsoluteZoomPollMs = 250;
constexpr int kAbsoluteZoomConfirmationTimeoutMs = 10000;
constexpr int kContinuousZoomPollMs = 350;
constexpr int kContinuousZoomStopRetryMs = 150;
constexpr int kLocalPhotoTimeoutMs = 5000;
constexpr int kLocalRecordingStartTimeoutMs = 3000;
constexpr int kLocalRecordingStopTimeoutMs = 5000;
constexpr int kShutdownRecordingWaitMs = 3000;
constexpr const char* kRecordingExtensions[VideoReceiver::FILE_FORMAT_MAX + 1] = {
    "mkv", "mov", "mp4",
};

class Mt11PhotoGrabLifetime final : public QObject
{
public:
    Mt11PhotoGrabLifetime(
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

    QSaveFile file(filename);
    if (!file.open(QIODevice::WriteOnly)) {
        outcome.error = file.errorString();
        return outcome;
    }
    QImageWriter writer(&file, "jpg");
    writer.setQuality(100);
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
    return outcome;
}

} // namespace

Mt11ControlManager::Mt11ControlManager(GimbalControlSettings* settings,
                                       QObject* parent)
    : QObject(parent)
    , _settings(settings)
    , _sdk(new Mt11Sdk(this))
{
    Q_CHECK_PTR(_settings);
    _sdk->setZoomRange(kMinimumZoom, kAbsoluteCommandMaximumZoom);
    _sdk->setFeedbackZoomRange(kMinimumZoom, kFeedbackMaximumZoom);

    _pollTimer.setInterval(2000);
    _sdkResponseTimer.setSingleShot(true);
    // A complete 0x16/0x18/0x0a/0x10 poll batch repeats every two seconds.
    // Keep the reachability watchdog safely beyond one missed batch.
    _sdkResponseTimer.setInterval(6000);
    _maximumZoomFreshnessTimer.setSingleShot(true);
    _maximumZoomFreshnessTimer.setInterval(kZoomFeedbackFreshnessMs);
    _zoomStatusFreshnessTimer.setSingleShot(true);
    _zoomStatusFreshnessTimer.setInterval(kZoomFeedbackFreshnessMs);
    _absoluteZoomPollTimer.setInterval(kAbsoluteZoomPollMs);
    _absoluteZoomConfirmationTimer.setSingleShot(true);
    _absoluteZoomConfirmationTimer.setInterval(
        kAbsoluteZoomConfirmationTimeoutMs);
    _continuousZoomPollTimer.setInterval(kContinuousZoomPollMs);
    _continuousZoomWatchdog.setSingleShot(true);
    _continuousZoomWatchdog.setInterval(60000);
    _continuousZoomStopRetryTimer.setSingleShot(true);
    _continuousZoomStopRetryTimer.setInterval(kContinuousZoomStopRetryMs);
    _recordingStatusDelayTimer.setSingleShot(true);
    _recordingStatusDelayTimer.setInterval(400);
    _recordingCommandTimer.setSingleShot(true);
    _recordingCommandTimer.setInterval(2500);
    _thermalCommandTimer.setSingleShot(true);
    _thermalCommandTimer.setInterval(2500);
    _photoCommandTimer.setSingleShot(true);
    _photoCommandTimer.setInterval(2000);
    _localPhotoTimer.setSingleShot(true);
    _localPhotoTimer.setInterval(kLocalPhotoTimeoutMs);
    _localRecordingStartTimer.setSingleShot(true);
    _localRecordingStartTimer.setInterval(kLocalRecordingStartTimeoutMs);
    _localRecordingStopTimer.setSingleShot(true);
    _localRecordingStopTimer.setInterval(kLocalRecordingStopTimeoutMs);
    _localPhotoSaveThreadPool.setMaxThreadCount(1);

    connect(_sdk, &Mt11Sdk::manualZoomReceived,
            this, &Mt11ControlManager::_handleManualZoom);
    connect(_sdk, &Mt11Sdk::absoluteZoomFeedbackReceived,
            this, &Mt11ControlManager::_handleAbsoluteZoomFeedback);
    connect(_sdk, &Mt11Sdk::maximumZoomReceived,
            this, &Mt11ControlManager::_handleMaximumZoom);
    connect(_sdk, &Mt11Sdk::currentZoomReceived,
            this, &Mt11ControlManager::_handleCurrentZoom);
    connect(_sdk, &Mt11Sdk::cameraSystemStatusReceived,
            this, &Mt11ControlManager::_handleCameraSystemStatus);
    connect(_sdk, &Mt11Sdk::functionFeedbackReceived,
            this, &Mt11ControlManager::_handleFunctionFeedback);
    connect(_sdk, &Mt11Sdk::videoModeReceived,
            this, &Mt11ControlManager::_handleVideoMode);
    connect(_sdk, &Mt11Sdk::communicationError,
            this, &Mt11ControlManager::_handleCommunicationError);
    connect(_sdk, &Mt11Sdk::packetReceived, this, [this]() {
        if (!enabled()) {
            return;
        }
        // Treat this as a last-seen watchdog. The poll period is two seconds;
        // restarting a six-second deadline avoids an online/offline flicker
        // while still declaring the endpoint lost after several missed polls.
        _sdkResponseTimer.start();
        _setSdkResponding(true);
        if (_lastError == tr("No response from the MT11 SDK endpoint.")) {
            _setLastError(QString());
        }
    });

    connect(&_pollTimer, &QTimer::timeout,
            this, &Mt11ControlManager::_pollSdk);
    connect(&_sdkResponseTimer, &QTimer::timeout,
            this, &Mt11ControlManager::_markSdkNotResponding);
    connect(&_maximumZoomFreshnessTimer, &QTimer::timeout, this, [this]() {
        if (_continuousZoomActive) {
            (void) stopZoom();
        }
        _setMaximumZoomKnown(false);
    });
    connect(&_zoomStatusFreshnessTimer, &QTimer::timeout, this, [this]() {
        if (_continuousZoomActive) {
            (void) stopZoom();
        }
        // An absolute command owns its separate 10-second confirmation
        // generation. Expiring the old/current feedback must not cancel that
        // generation and unlock a second 0x0f command while the first moves.
        _measuredZoomKnown = false;
        _setZoomStatusKnown(false);
    });
    connect(&_absoluteZoomPollTimer, &QTimer::timeout,
            this, &Mt11ControlManager::_pollPendingAbsoluteZoom);
    connect(&_absoluteZoomConfirmationTimer, &QTimer::timeout,
            this, &Mt11ControlManager::_handleAbsoluteZoomConfirmationTimeout);
    connect(&_continuousZoomPollTimer, &QTimer::timeout,
            this, &Mt11ControlManager::_pollContinuousZoom);
    connect(&_continuousZoomWatchdog, &QTimer::timeout, this, [this]() {
        if (_continuousZoomActive) {
            (void) stopZoom();
            _setLastError(
                tr("MT11 continuous zoom stopped after the safety timeout."));
        }
    });
    connect(&_continuousZoomStopRetryTimer, &QTimer::timeout,
            this, &Mt11ControlManager::_retryContinuousZoomStop);
    connect(&_recordingStatusDelayTimer, &QTimer::timeout,
            this, &Mt11ControlManager::_requestRecordingStatusAfterDelay);
    connect(&_recordingCommandTimer, &QTimer::timeout,
            this, &Mt11ControlManager::_handleRecordingCommandTimeout);
    connect(&_thermalCommandTimer, &QTimer::timeout,
            this, &Mt11ControlManager::_handleThermalCommandTimeout);
    connect(&_photoCommandTimer, &QTimer::timeout,
            this, &Mt11ControlManager::_handlePhotoCommandTimeout);
    connect(&_localPhotoTimer, &QTimer::timeout,
            this, &Mt11ControlManager::_handleLocalPhotoTimeout);
    connect(&_localRecordingStartTimer, &QTimer::timeout,
            this, &Mt11ControlManager::_handleLocalRecordingStartTimeout);
    connect(&_localRecordingStopTimer, &QTimer::timeout,
            this, &Mt11ControlManager::_handleLocalRecordingStopTimeout);

    connect(_settings->mt11Enabled(), &Fact::rawValueChanged,
            this, &Mt11ControlManager::_settingsChanged);
    connect(_settings->mt11SdkHost(), &Fact::rawValueChanged,
            this, &Mt11ControlManager::_settingsChanged);
    connect(_settings->mt11SdkPort(), &Fact::rawValueChanged,
            this, &Mt11ControlManager::_settingsChanged);
    connect(_settings->mt11ZoomStep(), &Fact::rawValueChanged,
            this, &Mt11ControlManager::_zoomStepChanged);
    connect(_settings->localMediaStorageEnabled(), &Fact::rawValueChanged,
            this, [this]() {
                emit localMediaStorageEnabledChanged();
                if (!localMediaStorageEnabled() && _localRecordingIntent) {
                    _localRecordingIntent = false;
                    _stopLocalRecording();
                }
                _notifyRecordingSessionStateChanged();
            });

    _lastEnabled = enabled();
    _appliedSdkHost = _settings->mt11SdkHost()->rawValue().toString();
    _appliedSdkPort = _configuredSdkPort();
    _configureSdkEndpoint();
    if (_lastEnabled) {
        _pollTimer.start();
        _pollSdk();
    }
}

Mt11ControlManager::~Mt11ControlManager()
{
    const bool stopZoomRequired = _zoomCommandPending || _continuousZoomActive
        || _continuousZoomStopRetryTimer.isActive();
    // shutdownLocalMedia(true) can run a bounded nested event loop. Retire the
    // entire SDK-control generation before entering it so timers or late UDP
    // ACKs cannot re-enter this half-destructed manager and restart polling.
    _pollTimer.stop();
    _sdkResponseTimer.stop();
    _maximumZoomFreshnessTimer.stop();
    _zoomStatusFreshnessTimer.stop();
    _absoluteZoomPollTimer.stop();
    _absoluteZoomConfirmationTimer.stop();
    _continuousZoomPollTimer.stop();
    _continuousZoomWatchdog.stop();
    _continuousZoomStopRetryTimer.stop();
    _recordingStatusDelayTimer.stop();
    _recordingCommandTimer.stop();
    _thermalCommandTimer.stop();
    _photoCommandTimer.stop();
    _zoomCommandPending = false;
    _continuousZoomActive = false;
    _continuousZoomDirection = 0;
    if (_sdk) {
        disconnect(_sdk, nullptr, this, nullptr);
    }
    if (stopZoomRequired && _sdk) {
        // Destruction has no event-loop opportunity for the normal delayed
        // retry, so send the one bounded safety copy immediately, before any
        // potentially blocking local-media finalization.
        (void) _sdk->sendManualZoom(0);
        (void) _sdk->sendManualZoom(0);
    }
    _localPhotoTimer.stop();
    ++_localPhotoSequence;
    _localPhotoGrabLifetime.clear();
    _localPhotoPending = false;
    _localPhotoSaveThreadPool.waitForDone();
    shutdownLocalMedia(true);
}

bool Mt11ControlManager::enabled() const
{
    return _settings
        && _settings->mt11Enabled()->rawValue().toBool();
}

bool Mt11ControlManager::localMediaStorageEnabled() const
{
    return _settings
        && _settings->localMediaStorageEnabled()->rawValue().toBool();
}

double Mt11ControlManager::zoomStep() const
{
    if (!_settings) {
        return 1.0;
    }
    return qRound(qBound(0.1,
                         _settings->mt11ZoomStep()->rawValue().toDouble(),
                         kAbsoluteCommandMaximumZoom - kMinimumZoom) * 10.0) / 10.0;
}

bool Mt11ControlManager::zoomInAvailable() const
{
    return zoomInTapAvailable() || zoomInHoldAvailable();
}

bool Mt11ControlManager::zoomOutAvailable() const
{
    return zoomOutTapAvailable() || zoomOutHoldAvailable();
}

bool Mt11ControlManager::zoomInTapAvailable() const
{
    return _zoomTapAvailable(1);
}

bool Mt11ControlManager::zoomOutTapAvailable() const
{
    return _zoomTapAvailable(-1);
}

bool Mt11ControlManager::zoomInHoldAvailable() const
{
    return _zoomHoldAvailable(1);
}

bool Mt11ControlManager::zoomOutHoldAvailable() const
{
    return _zoomHoldAvailable(-1);
}

bool Mt11ControlManager::recordingSessionActive() const
{
    return _recordingSessionRequested || _recording
        || _localRecordingActive || _localRecordingStartPending
        || _localRecordingStopPending;
}

bool Mt11ControlManager::recordingSessionCapturing() const
{
    return (_recording && !_recordingCommandPending)
        || _localRecordingActive;
}

bool Mt11ControlManager::videoRecordingAvailable() const
{
    if (_recordingCommandPending || localRecordingPending()) {
        return false;
    }
    if (recordingSessionActive()) {
        return true;
    }
    return (enabled() && _cameraStatusKnown)
        || (localMediaStorageEnabled() && _receiverStreaming());
}

bool Mt11ControlManager::zoomIn()
{
    return _sendZoomStep(1);
}

bool Mt11ControlManager::zoomOut()
{
    return _sendZoomStep(-1);
}

bool Mt11ControlManager::setZoom(double zoomLevel)
{
    if (!_cameraCommandAvailable() || !zoomControlsUnlocked()
        || _continuousZoomActive || _postHoldZoomFeedbackPending
        || !qIsFinite(zoomLevel)) {
        return false;
    }
    // The measured position owns the protocol-domain gate. A rounded display
    // at 30.0x must never make an actual 30.1x+ position eligible for 0x0f.
    if (_measuredZoom > kAbsoluteCommandMaximumZoom + kZoomTolerance) {
        _setLastError(tr("MT11 zoom above 30x supports press-and-hold control only."));
        return false;
    }
    const double target = qRound(zoomLevel * 10.0) / 10.0;
    const double absoluteMaximum = qMin(
        _maximumZoom, kAbsoluteCommandMaximumZoom);
    if (target < kMinimumZoom || target > absoluteMaximum) {
        _setLastError(tr("The requested MT11 zoom is outside the supported range."));
        return false;
    }
    if (!Mt11ZoomPolicy::isDisplayTarget(target,
                                         zoomStep(),
                                         absoluteMaximum)) {
        _setLastError(tr("The requested MT11 zoom is not aligned to the configured zoom step."));
        return false;
    }
    // A retry belonging to the preceding hold must never arrive after 0x0f
    // and interrupt the new absolute movement. Direct callers can reach this
    // method during the short retry window, so serialize the safety copy now.
    if (!_flushContinuousZoomStopRetry()) {
        return false;
    }
    _configureSdkEndpoint();
    if (!_sdk->sendAbsoluteZoom(target)) {
        return false;
    }
    _pendingAbsoluteZoomTarget = target;
    _setZoomCommandPending(true);
    // Match the A8 contract: currentZoom is the published legal target. Raw
    // 0x18/0x05 observations stay private and only confirm physical arrival.
    _setCurrentZoom(target);
    _setZoomStatusKnown(true);
    _absoluteZoomPollTimer.start();
    _absoluteZoomConfirmationTimer.start();
    _setLastError({});
    return true;
}

bool Mt11ControlManager::startZoom(int direction)
{
    return startZoomWithPressDuration(direction, 420);
}

bool Mt11ControlManager::startZoomWithPressDuration(int direction,
                                                    int pressDurationMs)
{
    Q_UNUSED(pressDurationMs);
    const int normalizedDirection = direction > 0 ? 1 : (direction < 0 ? -1 : 0);
    if (!_cameraCommandAvailable() || !zoomControlsUnlocked()
        || normalizedDirection == 0 || _continuousZoomActive
        || _postHoldZoomFeedbackPending
        || (normalizedDirection > 0 && !zoomInHoldAvailable())
        || (normalizedDirection < 0 && !zoomOutHoldAvailable())) {
        return false;
    }
    // Finish the preceding gesture's bounded safety stop before the newer
    // direction packet. A delayed 0x05(0) must never stop this new hold.
    if (!_flushContinuousZoomStopRetry()) {
        return false;
    }
    _configureSdkEndpoint();
    if (!_sdk->sendManualZoom(static_cast<qint8>(normalizedDirection))) {
        return false;
    }
    // Native 0x05 is the newer explicit gesture and supersedes any outstanding
    // absolute target confirmation only after its start packet was accepted.
    // Re-anchor the published value to the latest measurement first: a rapid
    // tap followed by a hold must not advance from an unconfirmed old target.
    _setZoomCommandPending(false);
    _alignDisplayToMeasured(normalizedDirection);
    _continuousZoomActive = true;
    _continuousZoomDirection = normalizedDirection;
    _continuousZoomPollTimer.start();
    _continuousZoomWatchdog.start();
    emit continuousZoomActiveChanged();
    emit zoomAvailabilityChanged();
    _setLastError({});
    _pollContinuousZoom();
    return true;
}

bool Mt11ControlManager::stopZoom()
{
    if (!_continuousZoomActive) {
        // Release, cancel and an automatic boundary stop can converge here.
        // An already scheduled retry means the stop sequence is in progress;
        // do not create an unbounded series of additional UDP commands.
        return _continuousZoomStopRetryTimer.isActive();
    }
    // Clear local motion state before sending. A synchronous transport error
    // can then safely invalidate controls without recursively stopping again.
    _postHoldZoomFeedbackPending = true;
    _finishContinuousZoomState();
    _configureSdkEndpoint();
    const bool sent = _sdk->sendManualZoom(0);
    if (enabled()) {
        // UDP write success does not prove device receipt. Send exactly one
        // delayed safety copy, then query 0x18 for the settled position.
        _continuousZoomStopRetryTimer.start();
        emit zoomAvailabilityChanged();
    }
    return sent;
}

bool Mt11ControlManager::cancelZoom()
{
    return stopZoom();
}

bool Mt11ControlManager::takePhoto()
{
    const bool localStarted = localMediaStorageEnabled()
        ? _captureLocalVideoFrame() : false;
    bool cameraSent = false;
    if (_cameraCommandAvailable() && !_photoCommandPending) {
        _configureSdkEndpoint();
        cameraSent = _sdk->takePhoto();
        if (cameraSent) {
            _setPhotoCommandPending(true);
            _photoCommandTimer.start();
        }
    }
    return localStarted || cameraSent;
}

bool Mt11ControlManager::toggleVideoRecording()
{
    if (_recordingCommandPending || localRecordingPending()) {
        return false;
    }
    return recordingSessionActive()
        ? _stopRecordingSession() : _startRecordingSession();
}

bool Mt11ControlManager::requestCurrentZoom()
{
    if (!enabled() || _continuousZoomActive) {
        return false;
    }
    _configureSdkEndpoint();
    if (!_sdkResponseTimer.isActive()) {
        _sdkResponseTimer.start();
    }
    return _sdk->requestCurrentZoom();
}

bool Mt11ControlManager::requestCameraStatus()
{
    if (!enabled()) {
        return false;
    }
    _configureSdkEndpoint();
    if (!_sdkResponseTimer.isActive()) {
        _sdkResponseTimer.start();
    }
    return _sdk->requestCameraSystemStatus();
}

bool Mt11ControlManager::toggleThermalMode()
{
    if (!_cameraCommandAvailable() || _thermalCommandPending) {
        return false;
    }
    if (!_thermalModeKnown) {
        _configureSdkEndpoint();
        if (!_sdkResponseTimer.isActive()) {
            _sdkResponseTimer.start();
        }
        return _sdk->requestVideoMode();
    }

    if (_continuousZoomActive && !stopZoom()) {
        _setLastError(tr("Failed to stop MT11 zoom before changing the video mode."));
        return false;
    }
    if (_zoomCommandPending) {
        _configureSdkEndpoint();
        const bool stopped = _sdk->sendManualZoom(0);
        _setZoomCommandPending(false);
        if (!stopped || !_sdk->sendManualZoom(0)) {
            _setLastError(tr("Failed to stop MT11 zoom before changing the video mode."));
            return false;
        }
    }
    if (_continuousZoomStopRetryTimer.isActive()) {
        // Serialize the bounded safety copy ahead of the mode command. A
        // delayed 0x05(0) must not cross the main-lens switch boundary.
        _continuousZoomStopRetryTimer.stop();
        _configureSdkEndpoint();
        const bool retrySent = _sdk->sendManualZoom(0);
        emit zoomAvailabilityChanged();
        if (!retrySent) {
            return false;
        }
    }
    _thermalCommandTarget = !_thermalModeEnabled;
    _configureSdkEndpoint();
    if (!_sdk->setThermalMode(_thermalCommandTarget)) {
        return false;
    }
    _setThermalCommandPending(true);
    // 0x16 describes the current main lens. Do not expose capability or
    // position from the previous lens while the switch is pending.
    _invalidateZoomState();
    _thermalCommandTimer.start();
    return true;
}

void Mt11ControlManager::setVideoItem(QQuickItem* videoItem)
{
    if (_videoItem == videoItem) {
        return;
    }
    if (_localPhotoPending) {
        _handleLocalPhotoTimeout();
    }
    _videoItem = videoItem;
}

void Mt11ControlManager::setVideoReceiver(VideoReceiver* receiver)
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
            _setLocalMediaError(tr("The MT11 video receiver was detached during local recording."));
        }
    }
    _videoReceiver = receiver;
    _receiverStreamingActive = false;
    _receiverDecodingActive = false;
    _receiverRecordingActive = false;
    if (!_videoReceiver) {
        _videoItem.clear();
        _notifyRecordingSessionStateChanged();
        return;
    }

    connect(_videoReceiver, &VideoReceiver::streamingChanged,
            this, &Mt11ControlManager::_handleReceiverStreamingChanged);
    connect(_videoReceiver, &VideoReceiver::decodingChanged,
            this, &Mt11ControlManager::_handleReceiverDecodingChanged);
    connect(_videoReceiver, &VideoReceiver::recordingChanged,
            this, &Mt11ControlManager::_handleReceiverRecordingChanged);
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

void Mt11ControlManager::handleVideoRecordingStartResult(
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
        _setLocalMediaError(tr("Failed to start MT11 local video recording."));
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

void Mt11ControlManager::shutdownLocalMedia(bool waitForStop)
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
        connect(this, &Mt11ControlManager::localRecordingStateChanged,
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
            qCWarning(Mt11ControlLog)
                << "Timed out publishing MT11 media during shutdown";
        }
#endif
    }
    _shuttingDown = false;
}

void Mt11ControlManager::_settingsChanged()
{
    if (_restoringSettings) {
        return;
    }

    // Recording is a toggle-only command. Changing endpoint while a toggle is
    // awaiting status could either strand a newly-started recording on the old
    // camera or toggle a just-stopped recording back on. Keep the last applied
    // settings until the command is resolved and make the rejected edit visible
    // in both the Fact and QSettings so the user can safely retry it.
    if (_recordingCommandPending) {
        _restoringSettings = true;
        _settings->mt11Enabled()->setRawValue(_lastEnabled);
        _settings->mt11SdkHost()->setRawValue(_appliedSdkHost);
        _settings->mt11SdkPort()->setRawValue(_appliedSdkPort);
        _restoringSettings = false;
        _setLastError(tr("Wait for the pending MT11 recording command before changing its SDK settings."));
        return;
    }

    const bool hadCameraRecording = _recording;
    const bool hadRecordingSession = recordingSessionActive();

    const bool stopSequencePending = _zoomCommandPending
        || _continuousZoomActive
        || _continuousZoomStopRetryTimer.isActive();
    _continuousZoomStopRetryTimer.stop();
    if (_continuousZoomActive) {
        _finishContinuousZoomState();
    }
    if (_lastEnabled && _sdk) {
        // The Fact already contains its new value when this slot runs, while
        // Mt11Sdk still holds the previous endpoint. Stop directly against
        // that latched endpoint before any call to _configureSdkEndpoint().
        (void) _sdk->sendManualZoom(0);
        if (stopSequencePending) {
            // The delayed retry cannot safely survive an endpoint change, so
            // deliver its one bounded copy before switching the SDK endpoint.
            (void) _sdk->sendManualZoom(0);
        }
    }

    // Stop an already-confirmed SD-card recording against the old endpoint
    // before host/port/enable changes are applied. End the associated local
    // branch as part of the same user session instead of leaving a stale
    // requested state in the UI.
    if (hadCameraRecording && _sdk) {
        (void) _sdk->toggleVideoRecording();
    }
    if (hadRecordingSession) {
        _recordingSessionRequested = false;
        _localRecordingIntent = false;
        if (_localRecordingStartPending && !_localRecordingOwned) {
            // Preserve the unresolved generation. Its late success callback
            // sees the cleared intent and issues the compensating stop.
            _localRecordingStartTimer.stop();
        } else {
            _stopLocalRecording();
        }
        _notifyRecordingSessionStateChanged();
    }
    const bool nowEnabled = enabled();
    _pollTimer.stop();
    _sdkResponseTimer.stop();
    _maximumZoomFreshnessTimer.stop();
    _zoomStatusFreshnessTimer.stop();
    _continuousZoomPollTimer.stop();
    _continuousZoomWatchdog.stop();
    _continuousZoomStopRetryTimer.stop();
    _recordingStatusDelayTimer.stop();
    _recordingCommandTimer.stop();
    _thermalCommandTimer.stop();
    _photoCommandTimer.stop();
    _setSdkResponding(false);
    _invalidateZoomState();
    _setCameraStatusKnown(false);
    _setRecording(false);
    _setRecordingCommandPending(false);
    _setPhotoCommandPending(false);
    _setThermalModeKnown(false);
    _setThermalCommandPending(false);
    // A pure enable/disable change can keep the same UDP endpoint. Explicitly
    // retire all old request generations so their late ACKs cannot repopulate
    // state after this reset or be mistaken for the first poll after re-enable.
    _sdk->clearPendingRequests();
    _appliedSdkHost = _settings->mt11SdkHost()->rawValue().toString();
    _appliedSdkPort = _configuredSdkPort();
    _configureSdkEndpoint();

    if (_lastEnabled != nowEnabled) {
        _lastEnabled = nowEnabled;
        emit enabledChanged();
    }
    emit zoomAvailabilityChanged();
    if (nowEnabled) {
        _pollTimer.start();
        _pollSdk();
    }
}

void Mt11ControlManager::_zoomStepChanged()
{
    emit zoomStepChanged();
    // A new grid cannot inherit a target or free-running movement planned with
    // the previous step. Stop that generation first, then republish only the
    // nearest legal value on the new minimum-anchored grid.
    if (_continuousZoomActive) {
        (void) stopZoom();
    }
    if (_continuousZoomStopRetryTimer.isActive()) {
        (void) _flushContinuousZoomStopRetry();
    }
    if (_zoomCommandPending && _sdk) {
        _configureSdkEndpoint();
        _setZoomCommandPending(false);
        (void) _sdk->sendManualZoom(0);
        (void) _sdk->sendManualZoom(0);
    }
    if (_measuredZoomKnown) {
        _alignDisplayToMeasured();
    } else {
        _setZoomStatusKnown(false);
    }
    emit zoomAvailabilityChanged();
}

void Mt11ControlManager::_pollSdk()
{
    if (!enabled()) {
        return;
    }
    _configureSdkEndpoint();
    if (!_sdkResponseTimer.isActive()) {
        _sdkResponseTimer.start();
    }
    // Do not accept capabilities from the previous main lens while a video
    // mode change is still unconfirmed. The confirming 0x10/0x11 feedback
    // triggers a fresh 0x16/0x18 pair immediately.
    if (!_thermalCommandPending) {
        (void) _sdk->requestMaximumZoom();
        if (!_continuousZoomActive) {
            (void) _sdk->requestCurrentZoom();
        }
    }
    (void) _sdk->requestCameraSystemStatus();
    if (!_thermalCommandPending) {
        (void) _sdk->requestVideoMode();
    }
}

void Mt11ControlManager::_markSdkNotResponding()
{
    if (_continuousZoomActive) {
        (void) stopZoom();
    } else if (_zoomCommandPending && _sdk) {
        _setZoomCommandPending(false);
        (void) _sdk->sendManualZoom(0);
        (void) _sdk->sendManualZoom(0);
    }
    _setSdkResponding(false);
    _invalidateZoomState();
    _setLastError(tr("No response from the MT11 SDK endpoint."));
}

void Mt11ControlManager::_handleManualZoom(double zoomLevel)
{
    if (!enabled() || _thermalCommandPending) return;
    _observeZoomFeedback(zoomLevel);
}

void Mt11ControlManager::_handleAbsoluteZoomFeedback(bool accepted)
{
    if (!enabled() || !_zoomCommandPending) return;
    if (!accepted) {
        // Production packets use fixed sequence 0 and the ACK identifies only
        // command 0x0f. A delayed reject from any earlier tap can match the
        // newest SDK request window, so it must never roll back that target.
        // Only measured 0x18 arrival or the bounded 10-second timeout can
        // finish the current absolute generation reliably.
        (void) requestCurrentZoom();
        return;
    }
    // Acceptance also carries no reliable per-tap generation. Keep the newest
    // published target under measured confirmation; only 0x18 can complete it.
    (void) requestCurrentZoom();
}

void Mt11ControlManager::_handleMaximumZoom(double maximumZoom)
{
    if (!enabled() || _thermalCommandPending) return;
    _setMaximumZoom(maximumZoom);
    _setMaximumZoomKnown(true);
    _maximumZoomFreshnessTimer.start();
    if (!_zoomCommandPending && !_continuousZoomActive
        && _measuredZoomKnown) {
        _alignDisplayToMeasured();
    }
    _checkContinuousZoomBoundary();
}

void Mt11ControlManager::_handleCurrentZoom(double zoomLevel)
{
    if (!enabled() || _thermalCommandPending) return;
    _observeZoomFeedback(zoomLevel);
    if (_postHoldZoomFeedbackPending
        && _measuredZoomKnown
        && !_continuousZoomActive
        && !_continuousZoomStopRetryTimer.isActive()) {
        _postHoldZoomFeedbackPending = false;
        emit zoomAvailabilityChanged();
    }
}

void Mt11ControlManager::_handleCameraSystemStatus(
    quint8 hdrStatus,
    quint8 recordingStatus,
    quint8 gimbalMotionMode,
    quint8 gimbalMountingDirection,
    quint8 videoOutputStatus,
    quint8 zoomLinkage)
{
    if (!enabled()) return;
    Q_UNUSED(hdrStatus);
    Q_UNUSED(gimbalMotionMode);
    Q_UNUSED(gimbalMountingDirection);
    Q_UNUSED(videoOutputStatus);
    Q_UNUSED(zoomLinkage);

    if (recordingStatus == 0 || recordingStatus == 1) {
        _setCameraStatusKnown(true);
        _setRecording(recordingStatus == 1);
        if (_recordingCommandPending
            && _recording == _recordingCommandTarget) {
            _recordingCommandTimer.stop();
            _setRecordingCommandPending(false);
        }
    } else if (recordingStatus == 2) {
        _setCameraStatusKnown(true);
        _setRecording(false);
        _setRecordingCommandPending(false);
        _setLastError(tr("The MT11 camera has no storage card."));
    } else if (recordingStatus == 3) {
        _setCameraStatusKnown(false);
        _setRecording(false);
        _setRecordingCommandPending(false);
        _setLastError(tr("The MT11 camera reported recording data loss."));
    }
}

void Mt11ControlManager::_handleFunctionFeedback(quint8 infoType)
{
    if (!enabled()) return;
    switch (infoType) {
    case 0:
        if (!_photoCommandPending) {
            break;
        }
        _photoCommandTimer.stop();
        _setPhotoCommandPending(false);
        ++_photoCount;
        emit photoCountChanged();
        break;
    case 1:
        if (!_photoCommandPending) {
            break;
        }
        _photoCommandTimer.stop();
        _setPhotoCommandPending(false);
        _setLastError(tr("MT11 photo capture failed or its storage card is unavailable."));
        break;
    case 4:
        _recordingCommandTimer.stop();
        _setRecordingCommandPending(false);
        _setCameraStatusKnown(false);
        _setLastError(tr("MT11 video recording failed or its storage card is unavailable."));
        break;
    case 5:
        _setRecording(true);
        if (_recordingCommandTarget) {
            _recordingCommandTimer.stop();
            _setRecordingCommandPending(false);
        }
        break;
    case 6:
        _setRecording(false);
        if (!_recordingCommandTarget) {
            _recordingCommandTimer.stop();
            _setRecordingCommandPending(false);
        }
        break;
    default:
        break;
    }
}

void Mt11ControlManager::_handleVideoMode(quint8 mainStream, quint8 subStream)
{
    if (!enabled()) return;
    Q_UNUSED(subStream);
    const bool thermal = mainStream == Mt11Protocol::VideoSourceThermal;
    // The maximum/current zoom feedback belongs to the exact main lens, not
    // merely to the coarse RGB-versus-thermal UI state. Zoom, wide and
    // composite sources can all be non-thermal yet have different limits.
    const bool modeChanged = _thermalModeKnown
        && mainStream != _mainVideoSource;
    const bool commandConfirmed = _thermalCommandPending
        && thermal == _thermalCommandTarget;
    const bool refreshZoomState = modeChanged || commandConfirmed;

    if (refreshZoomState) {
        if (_continuousZoomActive) {
            // A locally started continuous movement must still receive one
            // immediate safety stop even when another controller changed the
            // main source first.
            (void) stopZoom();
        }
        if (_continuousZoomStopRetryTimer.isActive()) {
            // The main lens has already changed. Cancel the old generation's
            // delayed copy instead of sending it to the newly selected lens.
            _continuousZoomStopRetryTimer.stop();
            emit zoomAvailabilityChanged();
        }
        if (_zoomCommandPending) {
            // 0x0f is routed to the main lens. Once an external source change
            // is observed, its old target can no longer be confirmed safely;
            // retire it without issuing a cross-lens command. Our own mode
            // switch path flushes this movement before sending 0x11.
            _setZoomCommandPending(false);
        }
        // Retire any 0x16/0x18 requests issued for the prior main lens before
        // opening a fresh request generation below.
        _sdk->clearPendingRequests();
        _invalidateZoomState();
    }
    _mainVideoSource = mainStream;
    _setThermalModeKnown(true);
    _setThermalModeEnabled(thermal);
    if (commandConfirmed) {
        _thermalCommandTimer.stop();
        _setThermalCommandPending(false);
    }
    if (refreshZoomState && !_thermalCommandPending) {
        _requestZoomState();
    }
}

void Mt11ControlManager::_handleCommunicationError(const QString& message)
{
    if (_continuousZoomActive) {
        (void) stopZoom();
    } else if (_zoomCommandPending && _sdk) {
        // Clear first so a synchronous write error from the stop copies cannot
        // recurse into another stop sequence.
        _setZoomCommandPending(false);
        (void) _sdk->sendManualZoom(0);
        (void) _sdk->sendManualZoom(0);
    }
    _setSdkResponding(false);
    _invalidateZoomState();
    _setLastError(message);
}

void Mt11ControlManager::_handleReceiverStreamingChanged(bool active)
{
    _receiverStreamingActive = active;
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
            tr("MT11 local recording stopped because its video stream ended."));
    }
    _notifyRecordingSessionStateChanged();
}

void Mt11ControlManager::_handleReceiverDecodingChanged(bool active)
{
    _receiverDecodingActive = active;
}

void Mt11ControlManager::_handleReceiverRecordingChanged(bool active)
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

void Mt11ControlManager::_handleReceiverStopRecordingComplete(int status)
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

void Mt11ControlManager::_handleRecordingCommandTimeout()
{
    _setRecordingCommandPending(false);
    _setCameraStatusKnown(false);
    _setLastError(tr("Timed out confirming the MT11 recording state."));
}

void Mt11ControlManager::_handleThermalCommandTimeout()
{
    _setThermalCommandPending(false);
    _setThermalModeKnown(false);
    if (_continuousZoomActive) {
        (void) stopZoom();
    }
    _sdk->clearPendingRequests();
    _invalidateZoomState();
    // The set command may have reached the camera even though its ACK was
    // lost. Re-read both the actual mode and that lens' zoom state.
    _pollSdk();
    _setLastError(tr("Timed out confirming the MT11 video mode."));
}

void Mt11ControlManager::_handlePhotoCommandTimeout()
{
    _setPhotoCommandPending(false);
}

void Mt11ControlManager::_handleLocalPhotoTimeout()
{
    if (!_localPhotoPending) {
        return;
    }
    ++_localPhotoSequence;
    _localPhotoGrabLifetime.clear();
    _localPhotoPending = false;
    _setLocalMediaError(tr("Failed to capture the MT11 local video frame."));
}

void Mt11ControlManager::_handleLocalRecordingStartTimeout()
{
    if (!_localRecordingStartPending) {
        return;
    }
    // Keep the issued generation: a late success callback must still be
    // recognized and stopped rather than leaking a recorder session.
    _localRecordingStartPending = false;
    _localRecordingIntent = false;
    _setLocalMediaError(tr("Timed out starting MT11 local video recording."));
    emit localRecordingStateChanged();
    _notifyRecordingSessionStateChanged();
}

void Mt11ControlManager::_handleLocalRecordingStopTimeout()
{
    if (!_localRecordingStopPending) {
        return;
    }
    if (_videoReceiver && _localRecordingOwned && _receiverRecording()) {
        _videoReceiver->stopRecording();
        _localRecordingStopTimer.start();
        _setLocalMediaError(
            tr("Timed out stopping MT11 local video recording."));
        return;
    }
    _finishLocalRecording(_localRecordingOutputFile);
}

void Mt11ControlManager::_configureSdkEndpoint()
{
    if (!_sdk) {
        return;
    }
    _sdk->setEndpoint(_appliedSdkHost, _appliedSdkPort);
}

bool Mt11ControlManager::_cameraCommandAvailable()
{
    if (!enabled() || !_sdk) {
        _setLastError(tr("MT11 camera control is disabled."));
        return false;
    }
    return true;
}

bool Mt11ControlManager::_zoomTapAvailable(int direction) const
{
    if (!zoomControlsUnlocked() || _continuousZoomActive
        || _postHoldZoomFeedbackPending
        || _continuousZoomStopRetryTimer.isActive()
        || (direction != -1 && direction != 1)) {
        return false;
    }

    double target = _currentZoom;
    return Mt11ZoomPolicy::tapTarget(_measuredZoom,
                                     _currentZoom,
                                     zoomStep(),
                                     _maximumZoom,
                                     direction,
                                     &target);
}

bool Mt11ControlManager::_zoomHoldAvailable(int direction) const
{
    if (!zoomControlsUnlocked() || _continuousZoomActive
        || _postHoldZoomFeedbackPending
        || _continuousZoomStopRetryTimer.isActive()
        || (direction != -1 && direction != 1)) {
        return false;
    }
    return direction > 0
        ? _measuredZoom < _maximumZoom - kZoomTolerance
        : _measuredZoom > kMinimumZoom + kZoomTolerance;
}

bool Mt11ControlManager::_flushContinuousZoomStopRetry()
{
    if (!_continuousZoomStopRetryTimer.isActive()) {
        return true;
    }
    _continuousZoomStopRetryTimer.stop();
    _configureSdkEndpoint();
    const bool sent = _sdk && _sdk->sendManualZoom(0);
    emit zoomAvailabilityChanged();
    return sent;
}

void Mt11ControlManager::_observeZoomFeedback(double zoomLevel)
{
    if (!qIsFinite(zoomLevel)
        || zoomLevel < kMinimumZoom - kZoomTolerance
        || zoomLevel > kSupportedHybridMaximumZoom + kZoomTolerance) {
        _measuredZoomKnown = false;
        _setZoomStatusKnown(false);
        return;
    }

    const double measured = qRound(zoomLevel * 10.0) / 10.0;
    const bool measurementChanged = !_measuredZoomKnown
        || qAbs(_measuredZoom - measured) > 0.001;
    _measuredZoom = measured;
    _measuredZoomKnown = true;
    _zoomStatusFreshnessTimer.start();

    if (_zoomCommandPending) {
        if (qAbs(_measuredZoom - _pendingAbsoluteZoomTarget)
            <= kZoomTolerance) {
            _setZoomCommandPending(false);
        }
        _setZoomStatusKnown(true);
    } else if (_continuousZoomActive) {
        _advanceContinuousZoomDisplay();
        _setZoomStatusKnown(true);
    } else {
        _alignDisplayToMeasured();
    }

    _checkContinuousZoomBoundary();
    if (measurementChanged) {
        emit zoomAvailabilityChanged();
    }
}

void Mt11ControlManager::_alignDisplayToMeasured(int preferredDirection)
{
    if (!_measuredZoomKnown) {
        return;
    }
    const double displayMaximum = _maximumZoomKnown
        ? _maximumZoom : kSupportedHybridMaximumZoom;
    double alignedTarget = kMinimumZoom;
    if (!Mt11ZoomPolicy::alignedDisplayTarget(_measuredZoom,
                                              zoomStep(),
                                              displayMaximum,
                                              preferredDirection,
                                              &alignedTarget)) {
        _setZoomStatusKnown(false);
        return;
    }
    _setCurrentZoom(alignedTarget);
    _setZoomStatusKnown(true);
}

void Mt11ControlManager::_advanceContinuousZoomDisplay()
{
    if (!_continuousZoomActive || !_measuredZoomKnown
        || (_continuousZoomDirection != -1
            && _continuousZoomDirection != 1)) {
        return;
    }

    // Advance only across canonical legal stops actually reached by feedback.
    // Intermediate values such as 5.6x remain private observations and can
    // never leak into the value rendered by QML.
    double reachedTarget = _currentZoom;
    if (Mt11ZoomPolicy::heldProgressTarget(_currentZoom,
                                           _measuredZoom,
                                           zoomStep(),
                                           _maximumZoom,
                                           _continuousZoomDirection,
                                           &reachedTarget)) {
        _setCurrentZoom(reachedTarget);
        _setZoomStatusKnown(true);
    }
}

bool Mt11ControlManager::_sendZoomStep(int direction)
{
    if ((direction != -1 && direction != 1)
        || !_cameraCommandAvailable() || !zoomControlsUnlocked()
        || _continuousZoomActive || _postHoldZoomFeedbackPending) {
        return false;
    }
    double target = _currentZoom;
    if (!Mt11ZoomPolicy::tapTarget(_measuredZoom,
                                   _currentZoom,
                                   zoomStep(),
                                   _maximumZoom,
                                   direction,
                                   &target)) {
        // The documented 0x0f absolute command addresses only 1.0x-30.0x.
        // Above 30x both directions deliberately remain hold-only, avoiding a
        // short press which could jump a hybrid-zoom position back below 30x.
        if (_measuredZoom
            > kAbsoluteCommandMaximumZoom + kZoomTolerance) {
            _setLastError(tr("MT11 zoom above 30x supports press-and-hold control only."));
            return false;
        }
        if (direction > 0
            && _maximumZoom > kAbsoluteCommandMaximumZoom + kZoomTolerance
            && _currentZoom >= kAbsoluteCommandMaximumZoom - kZoomTolerance) {
            _setLastError(tr("MT11 zoom above 30x supports press-and-hold control only."));
        }
        return false;
    }
    return setZoom(target);
}

void Mt11ControlManager::_pollPendingAbsoluteZoom()
{
    if (!_zoomCommandPending || !enabled() || !_sdk
        || _continuousZoomActive || _thermalCommandPending) {
        _absoluteZoomPollTimer.stop();
        return;
    }
    _configureSdkEndpoint();
    if (!_sdkResponseTimer.isActive()) {
        _sdkResponseTimer.start();
    }
    (void) _sdk->requestCurrentZoom();
}

void Mt11ControlManager::_handleAbsoluteZoomConfirmationTimeout()
{
    if (!_zoomCommandPending) {
        return;
    }
    _absoluteZoomPollTimer.stop();
    _setZoomCommandPending(false);
    _setLastError(tr("Timed out confirming the MT11 zoom target."));
    if (enabled() && !_thermalCommandPending) {
        (void) requestCurrentZoom();
    }
}

void Mt11ControlManager::_pollContinuousZoom()
{
    if (!_continuousZoomActive || !enabled() || !_sdk
        || _thermalCommandPending) {
        return;
    }
    _configureSdkEndpoint();
    if (!_sdkResponseTimer.isActive()) {
        _sdkResponseTimer.start();
    }
    // 0x05 starts a camera-side continuous movement. 0x18 is the measured
    // feedback loop used by the UI and by the device-boundary stop below.
    (void) _sdk->requestCurrentZoom();
}

void Mt11ControlManager::_retryContinuousZoomStop()
{
    if (!enabled() || !_sdk) {
        emit zoomAvailabilityChanged();
        return;
    }
    _configureSdkEndpoint();
    (void) _sdk->sendManualZoom(0);
    if (!_thermalCommandPending) {
        if (!_sdkResponseTimer.isActive()) {
            _sdkResponseTimer.start();
        }
        (void) _sdk->requestCurrentZoom();
    }
    emit zoomAvailabilityChanged();
}

void Mt11ControlManager::_finishContinuousZoomState()
{
    _continuousZoomPollTimer.stop();
    _continuousZoomWatchdog.stop();
    if (!_continuousZoomActive && _continuousZoomDirection == 0) {
        return;
    }
    _continuousZoomActive = false;
    _continuousZoomDirection = 0;
    emit continuousZoomActiveChanged();
    emit zoomAvailabilityChanged();
}

void Mt11ControlManager::_checkContinuousZoomBoundary()
{
    if (!_continuousZoomActive || !_maximumZoomKnown
        || !_measuredZoomKnown) {
        return;
    }
    const bool reachedMaximum = _continuousZoomDirection > 0
        && _measuredZoom >= _maximumZoom - kZoomTolerance;
    const bool reachedMinimum = _continuousZoomDirection < 0
        && _measuredZoom <= kMinimumZoom + kZoomTolerance;
    if (reachedMaximum || reachedMinimum) {
        (void) stopZoom();
    }
}

void Mt11ControlManager::_invalidateZoomState()
{
    _maximumZoomFreshnessTimer.stop();
    _zoomStatusFreshnessTimer.stop();
    _absoluteZoomPollTimer.stop();
    _absoluteZoomConfirmationTimer.stop();
    _postHoldZoomFeedbackPending = false;
    _measuredZoomKnown = false;
    _setMaximumZoomKnown(false);
    _setZoomStatusKnown(false);
    _setZoomCommandPending(false);
    _setMaximumZoom(kAbsoluteCommandMaximumZoom);
}

void Mt11ControlManager::_requestZoomState()
{
    if (!enabled() || !_sdk || _thermalCommandPending) {
        return;
    }
    _configureSdkEndpoint();
    if (!_sdkResponseTimer.isActive()) {
        _sdkResponseTimer.start();
    }
    (void) _sdk->requestMaximumZoom();
    (void) _sdk->requestCurrentZoom();
}

bool Mt11ControlManager::_sendCameraRecordingToggle(bool targetRecording)
{
    if (!enabled() || !_cameraStatusKnown || _recordingCommandPending
        || _recording == targetRecording) {
        return false;
    }
    _configureSdkEndpoint();
    if (!_sdk->toggleVideoRecording()) {
        return false;
    }
    _recordingCommandTarget = targetRecording;
    _setRecordingCommandPending(true);
    _recordingStatusDelayTimer.start();
    _recordingCommandTimer.start();
    return true;
}

void Mt11ControlManager::_requestRecordingStatusAfterDelay()
{
    (void) requestCameraStatus();
}

void Mt11ControlManager::_setSdkResponding(bool responding)
{
    if (_sdkResponding == responding) return;
    _sdkResponding = responding;
    emit sdkRespondingChanged();
    emit zoomAvailabilityChanged();
}

void Mt11ControlManager::_setCurrentZoom(double zoomLevel)
{
    const double upperBound = _maximumZoomKnown
        ? _maximumZoom : kSupportedHybridMaximumZoom;
    zoomLevel = qRound(zoomLevel * 10.0) / 10.0;
    if (!Mt11ZoomPolicy::isDisplayTarget(zoomLevel,
                                         zoomStep(),
                                         upperBound)) {
        qCWarning(Mt11ControlLog)
            << "Rejected non-publishable MT11 zoom target" << zoomLevel
            << "step" << zoomStep() << "maximum" << upperBound;
        return;
    }
    if (qAbs(_currentZoom - zoomLevel) <= 0.001) return;
    _currentZoom = zoomLevel;
    emit currentZoomChanged();
    emit zoomAvailabilityChanged();
}

void Mt11ControlManager::_setMaximumZoom(double zoomLevel)
{
    zoomLevel = qBound(kMinimumZoom,
                       qRound(zoomLevel * 10.0) / 10.0,
                       kSupportedHybridMaximumZoom);
    if (qAbs(_maximumZoom - zoomLevel) <= 0.001) return;
    _maximumZoom = zoomLevel;
    emit maximumZoomChanged();
    emit zoomAvailabilityChanged();
}

void Mt11ControlManager::_setMaximumZoomKnown(bool known)
{
    if (_maximumZoomKnown == known) return;
    _maximumZoomKnown = known;
    emit zoomAvailabilityChanged();
}

void Mt11ControlManager::_setZoomStatusKnown(bool known)
{
    if (_zoomStatusKnown == known) return;
    _zoomStatusKnown = known;
    emit zoomStatusKnownChanged();
    emit zoomAvailabilityChanged();
}

void Mt11ControlManager::_setZoomCommandPending(bool pending)
{
    if (_zoomCommandPending == pending) return;
    _zoomCommandPending = pending;
    if (!pending) {
        _absoluteZoomPollTimer.stop();
        _absoluteZoomConfirmationTimer.stop();
    }
    emit zoomAvailabilityChanged();
}

void Mt11ControlManager::_setCameraStatusKnown(bool known)
{
    if (_cameraStatusKnown == known) return;
    _cameraStatusKnown = known;
    emit cameraStatusKnownChanged();
    _notifyRecordingSessionStateChanged();
}

void Mt11ControlManager::_setRecording(bool recording)
{
    if (_recording == recording) return;
    _recording = recording;
    emit recordingChanged();
    _notifyRecordingSessionStateChanged();
}

void Mt11ControlManager::_setRecordingCommandPending(bool pending)
{
    if (_recordingCommandPending == pending) return;
    _recordingCommandPending = pending;
    emit recordingCommandPendingChanged();
    _notifyRecordingSessionStateChanged();
}

void Mt11ControlManager::_setPhotoCommandPending(bool pending)
{
    if (_photoCommandPending == pending) return;
    _photoCommandPending = pending;
    emit photoCommandPendingChanged();
}

void Mt11ControlManager::_setThermalModeKnown(bool known)
{
    if (_thermalModeKnown == known) return;
    _thermalModeKnown = known;
    emit thermalModeKnownChanged();
}

void Mt11ControlManager::_setThermalModeEnabled(bool enabled)
{
    if (_thermalModeEnabled == enabled) return;
    _thermalModeEnabled = enabled;
    emit thermalModeEnabledChanged();
}

void Mt11ControlManager::_setThermalCommandPending(bool pending)
{
    if (_thermalCommandPending == pending) return;
    _thermalCommandPending = pending;
    emit thermalCommandPendingChanged();
    emit zoomAvailabilityChanged();
}

void Mt11ControlManager::_setLastError(const QString& message)
{
    if (_lastError == message) return;
    _lastError = message;
    emit lastErrorChanged();
}

void Mt11ControlManager::_setLocalMediaError(const QString& message)
{
    if (_localMediaError == message) return;
    _localMediaError = message;
    emit localMediaErrorChanged();
}

void Mt11ControlManager::_notifyRecordingSessionStateChanged()
{
    emit recordingSessionStateChanged();
}

bool Mt11ControlManager::_captureLocalVideoFrame()
{
    if (_localPhotoPending || _localPhotoGrabLifetimeCount > 0) {
        _setLocalMediaError(tr("An MT11 local photo is still being processed."));
        return false;
    }
    if (!_videoItem || !_receiverDecoding()
        || _videoItem->width() <= 0 || _videoItem->height() <= 0) {
        _setLocalMediaError(tr("No decoded MT11 video frame is available for a local photo."));
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
        QStringLiteral("MT11_%1_local_%2.jpg")
            .arg(QDateTime::currentDateTime().toString(
                     QStringLiteral("yyyy-MM-dd_hh.mm.ss.zzz")))
            .arg(sequence, 3, 10, QLatin1Char('0')));
    QQuickWindow* window = _videoItem->window();
    if (!window) {
        return false;
    }
    const qreal dpr = qMax<qreal>(1.0, window->effectiveDevicePixelRatio());
    const QSize sourceSize(qMax(1, qRound(_videoItem->width() * dpr)),
                           qMax(1, qRound(_videoItem->height() * dpr)));
    const auto geometry = GimbalPhotoCapturePolicy::captureGeometry(
        sourceSize, sourceSize, dpr);
    if (!geometry.isValid()) {
        return false;
    }

    const QSharedPointer<QQuickItemGrabResult> result =
        _videoItem->grabToImage(geometry.grabLogicalSize);
    if (!result) {
        return false;
    }
    auto* lifetime = new Mt11PhotoGrabLifetime(result, window);
    ++_localPhotoGrabLifetimeCount;
    connect(lifetime, &QObject::destroyed, this, [this]() {
        Q_ASSERT(_localPhotoGrabLifetimeCount > 0);
        --_localPhotoGrabLifetimeCount;
    });
    _localPhotoGrabLifetime = lifetime;
    _localPhotoPending = true;
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
                const QImage image = strongResult->image();
                if (image.isNull()) {
                    _localPhotoPending = false;
                    _setLocalMediaError(tr("Failed to capture the MT11 local video frame."));
                    return;
                }
                _localPhotoSaveThreadPool.start(
                    [this, image, geometry, filename, sequence]() mutable {
                        const PhotoSaveOutcome outcome =
                            savePhoto(std::move(image), geometry, filename);
                        (void) QMetaObject::invokeMethod(
                            this,
                            [this, outcome, filename, sequence]() {
                                if (sequence != _localPhotoSequence) return;
                                _localPhotoPending = false;
                                if (!outcome.success) {
                                    _setLocalMediaError(tr("Failed to save the MT11 local video frame."));
                                    return;
                                }
                                ++_localPhotoCount;
                                emit localPhotoCountChanged();
                                _setLocalMediaError({});
                                publishLocalMedia(filename, true);
                            },
                            Qt::QueuedConnection);
                    });
            }, Qt::SingleShotConnection);
    connect(result.data(), &QQuickItemGrabResult::ready, lifetime,
            [lifetime]() { lifetime->deleteLater(); },
            Qt::SingleShotConnection);
    _localPhotoTimer.start();
    return true;
}

bool Mt11ControlManager::_startRecordingSession()
{
    if (recordingSessionActive()) return false;
    const bool requestLocal = localMediaStorageEnabled() && _receiverStreaming();
    const bool requestCamera = enabled() && _cameraStatusKnown;
    if (!requestLocal && !requestCamera) return false;

    _recordingSessionRequested = true;
    if (requestLocal) {
        _localRecordingIntent = true;
        _startLocalRecording();
    }
    if (requestCamera && !_recording) {
        (void) _sendCameraRecordingToggle(true);
    }
    _notifyRecordingSessionStateChanged();
    return true;
}

bool Mt11ControlManager::_stopRecordingSession()
{
    if (!recordingSessionActive()) return false;
    _recordingSessionRequested = false;
    _localRecordingIntent = false;
    _stopLocalRecording();
    if (enabled() && _cameraStatusKnown && _recording) {
        (void) _sendCameraRecordingToggle(false);
    }
    _notifyRecordingSessionStateChanged();
    return true;
}

void Mt11ControlManager::_startLocalRecording()
{
    if (!_videoReceiver || !_videoReceiver->started() || !_receiverStreaming()
        || _localRecordingStartPending || _localRecordingActive
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
        QStringLiteral("MT11_%1_local_%2")
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

void Mt11ControlManager::_stopLocalRecording()
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

void Mt11ControlManager::_resetLocalRecordingState()
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

void Mt11ControlManager::_finishLocalRecording(const QString& outputFile)
{
    const bool shouldPublish = !outputFile.isEmpty()
        && QFileInfo(outputFile).isFile()
        && QFileInfo(outputFile).size() > 0;
    _resetLocalRecordingState();
    if (shouldPublish) {
        publishLocalMedia(outputFile, false);
    }
}

void Mt11ControlManager::finalizeDetachedLocalMedia()
{
    for (const QString& path : std::as_const(_detachedRecordingOutputs)) {
        const QFileInfo output(path);
        if (output.isFile() && output.size() > 0) {
            publishLocalMedia(output.absoluteFilePath(), false);
        }
    }
    _detachedRecordingOutputs.clear();
}

bool Mt11ControlManager::_receiverStreaming() const
{
    return _videoReceiver && _receiverStreamingActive;
}

bool Mt11ControlManager::_receiverDecoding() const
{
    return _videoReceiver && _receiverDecodingActive;
}

bool Mt11ControlManager::_receiverRecording() const
{
    return _videoReceiver && _receiverRecordingActive;
}

quint16 Mt11ControlManager::_configuredSdkPort() const
{
    if (!_settings) return 0;
    const uint port = _settings->mt11SdkPort()->rawValue().toUInt();
    return port <= 65535U ? static_cast<quint16>(port) : 0;
}
