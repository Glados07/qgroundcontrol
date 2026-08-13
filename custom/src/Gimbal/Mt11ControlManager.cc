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
    _sdk->setZoomRange(kMinimumZoom, kProtocolMaximumZoom);

    _pollTimer.setInterval(2000);
    _sdkResponseTimer.setSingleShot(true);
    // A complete 0x16/0x18/0x0a/0x10 poll batch repeats every two seconds.
    // Keep the reachability watchdog safely beyond one missed batch.
    _sdkResponseTimer.setInterval(6000);
    _continuousZoomWatchdog.setSingleShot(true);
    _continuousZoomWatchdog.setInterval(60000);
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
    connect(&_continuousZoomWatchdog, &QTimer::timeout, this, [this]() {
        if (_continuousZoomActive) {
            (void) stopZoom();
            _setLastError(
                tr("MT11 continuous zoom stopped after the safety timeout."));
        }
    });
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
    _localPhotoTimer.stop();
    ++_localPhotoSequence;
    _localPhotoGrabLifetime.clear();
    _localPhotoPending = false;
    _localPhotoSaveThreadPool.waitForDone();
    shutdownLocalMedia(true);
    if (_continuousZoomActive && _sdk) {
        (void) _sdk->sendManualZoom(0);
    }
    _continuousZoomWatchdog.stop();
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
                         kProtocolMaximumZoom - kMinimumZoom) * 10.0) / 10.0;
}

bool Mt11ControlManager::zoomInAvailable() const
{
    return zoomControlsUnlocked()
        && _currentZoom < _maximumZoom - kZoomTolerance;
}

bool Mt11ControlManager::zoomOutAvailable() const
{
    return zoomControlsUnlocked()
        && _currentZoom > kMinimumZoom + kZoomTolerance;
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
        || !qIsFinite(zoomLevel)) {
        return false;
    }
    const double target = qRound(zoomLevel * 10.0) / 10.0;
    if (target < kMinimumZoom || target > _maximumZoom) {
        _setLastError(tr("The requested MT11 zoom is outside the supported range."));
        return false;
    }
    if (_continuousZoomActive && !stopZoom()) {
        return false;
    }
    _configureSdkEndpoint();
    if (!_sdk->sendAbsoluteZoom(target)) {
        return false;
    }
    _zoomCommandPending = true;
    _setCurrentZoom(target);
    emit zoomAvailabilityChanged();
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
        || (normalizedDirection > 0 && !zoomInAvailable())
        || (normalizedDirection < 0 && !zoomOutAvailable())) {
        return false;
    }
    _configureSdkEndpoint();
    if (!_sdk->sendManualZoom(static_cast<qint8>(normalizedDirection))) {
        return false;
    }
    _continuousZoomActive = true;
    _continuousZoomDirection = normalizedDirection;
    _continuousZoomWatchdog.start();
    emit continuousZoomActiveChanged();
    emit zoomAvailabilityChanged();
    return true;
}

bool Mt11ControlManager::stopZoom()
{
    if (!_continuousZoomActive) {
        // A release/cancel can arrive after state was reset (camera switch,
        // disable or shutdown). Sending the documented zero command is
        // harmless and, more importantly, guarantees that an MT11 which saw
        // the preceding +/-1 command is not left zooming continuously.
        if (!enabled() || !_sdk) {
            return false;
        }
        _configureSdkEndpoint();
        return _sdk->sendManualZoom(0);
    }
    _configureSdkEndpoint();
    const bool sent = _sdk->sendManualZoom(0);
    _continuousZoomWatchdog.stop();
    _continuousZoomActive = false;
    _continuousZoomDirection = 0;
    emit continuousZoomActiveChanged();
    emit zoomAvailabilityChanged();
    if (sent) {
        (void) _sdk->requestCurrentZoom();
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

    _thermalCommandTarget = !_thermalModeEnabled;
    _configureSdkEndpoint();
    if (!_sdk->setThermalMode(_thermalCommandTarget)) {
        return false;
    }
    _setThermalCommandPending(true);
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

    if (_continuousZoomActive) {
        // The Fact already contains its new value when this slot runs, while
        // Mt11Sdk still holds the previous endpoint. Stop directly against
        // that latched endpoint before any call to _configureSdkEndpoint().
        (void) _sdk->sendManualZoom(0);
        _continuousZoomWatchdog.stop();
        _continuousZoomActive = false;
        _continuousZoomDirection = 0;
        emit continuousZoomActiveChanged();
        emit zoomAvailabilityChanged();
    } else if (_lastEnabled && _sdk) {
        // Stop against the old endpoint before applying a changed SDK host or
        // disabling the control. The MT11 manual-zoom protocol requires an
        // explicit zero command on release.
        (void) _sdk->sendManualZoom(0);
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
    _continuousZoomWatchdog.stop();
    _recordingStatusDelayTimer.stop();
    _recordingCommandTimer.stop();
    _thermalCommandTimer.stop();
    _photoCommandTimer.stop();
    _setSdkResponding(false);
    _setZoomStatusKnown(false);
    _maximumZoomKnown = false;
    _zoomCommandPending = false;
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
    (void) _sdk->requestMaximumZoom();
    if (!_continuousZoomActive) {
        (void) _sdk->requestCurrentZoom();
    }
    (void) _sdk->requestCameraSystemStatus();
    if (!_thermalCommandPending) {
        (void) _sdk->requestVideoMode();
    }
}

void Mt11ControlManager::_markSdkNotResponding()
{
    _setSdkResponding(false);
    _setLastError(tr("No response from the MT11 SDK endpoint."));
}

void Mt11ControlManager::_handleManualZoom(double zoomLevel)
{
    if (!enabled()) return;
    _setCurrentZoom(zoomLevel);
    _setZoomStatusKnown(true);
}

void Mt11ControlManager::_handleAbsoluteZoomFeedback(bool accepted)
{
    if (!enabled()) return;
    _zoomCommandPending = false;
    emit zoomAvailabilityChanged();
    if (!accepted) {
        _setZoomStatusKnown(false);
        _setLastError(tr("The MT11 camera rejected the zoom command."));
        return;
    }
    (void) requestCurrentZoom();
}

void Mt11ControlManager::_handleMaximumZoom(double maximumZoom)
{
    if (!enabled()) return;
    _setMaximumZoom(maximumZoom);
    if (!_maximumZoomKnown) {
        _maximumZoomKnown = true;
        emit zoomAvailabilityChanged();
    }
}

void Mt11ControlManager::_handleCurrentZoom(double zoomLevel)
{
    if (!enabled()) return;
    _zoomCommandPending = false;
    _setCurrentZoom(zoomLevel);
    _setZoomStatusKnown(true);
    emit zoomAvailabilityChanged();
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
    _setThermalModeKnown(true);
    _setThermalModeEnabled(thermal);
    if (_thermalCommandPending && thermal == _thermalCommandTarget) {
        _thermalCommandTimer.stop();
        _setThermalCommandPending(false);
    }
}

void Mt11ControlManager::_handleCommunicationError(const QString& message)
{
    _setSdkResponding(false);
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

bool Mt11ControlManager::_sendZoomStep(int direction)
{
    if (!zoomControlsUnlocked() || _zoomCommandPending
        || (direction > 0 && !zoomInAvailable())
        || (direction < 0 && !zoomOutAvailable())) {
        return false;
    }
    const double target = qBound(
        kMinimumZoom,
        qRound((_currentZoom + (direction > 0 ? zoomStep() : -zoomStep()))
               * 10.0) / 10.0,
        _maximumZoom);
    return setZoom(target);
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
}

void Mt11ControlManager::_setCurrentZoom(double zoomLevel)
{
    zoomLevel = qBound(kMinimumZoom,
                       qRound(zoomLevel * 10.0) / 10.0,
                       _maximumZoom);
    if (qAbs(_currentZoom - zoomLevel) <= 0.001) return;
    _currentZoom = zoomLevel;
    emit currentZoomChanged();
    emit zoomAvailabilityChanged();
}

void Mt11ControlManager::_setMaximumZoom(double zoomLevel)
{
    zoomLevel = qBound(kMinimumZoom, zoomLevel, kProtocolMaximumZoom);
    if (qAbs(_maximumZoom - zoomLevel) <= 0.001) return;
    _maximumZoom = zoomLevel;
    emit maximumZoomChanged();
    emit zoomAvailabilityChanged();
}

void Mt11ControlManager::_setZoomStatusKnown(bool known)
{
    if (_zoomStatusKnown == known) return;
    _zoomStatusKnown = known;
    emit zoomStatusKnownChanged();
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
