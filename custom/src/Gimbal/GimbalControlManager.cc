/****************************************************************************
 *
 * 思翼云台相机控制管理器。
 *
 ****************************************************************************/

#include "GimbalControlManager.h"

#include "A8MiniZoomPolicy.h"
#include "GimbalControlSettings.h"
#include "QGCLoggingCategory.h"
#include "SiyiSdk.h"
#include "Fact.h"
#include "VideoManager.h"

#include <QtCore/QtMath>

#include <limits>

namespace {

QGC_LOGGING_CATEGORY(GimbalControlLog, "gcs.custom.gimbal.control")

static constexpr double kZoomComparisonTolerance = 0.051;
static constexpr qint64 kLegalStableRecoveryMs = 3000;
static constexpr qint64 kOffGridStableRecoveryMs = 5000;
static constexpr int kAbsoluteTargetBucketConfirmationCount = 2;

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
    _continuousZoomWatchdog.setSingleShot(true);
    // 长按只发送一次原生0x05方向命令；60秒仅兜底丢失的释放事件。
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

    connect(_sdk,
            &SiyiSdk::absoluteZoomFeedbackReceived,
            this,
            &GimbalControlManager::_handleAbsoluteZoomFeedback);
    connect(_sdk,
            &SiyiSdk::manualZoomReceived,
            this,
            &GimbalControlManager::_handleManualZoomFeedback);
    connect(_sdk, &SiyiSdk::maximumZoomReceived, this, &GimbalControlManager::_handleMaximumZoom);
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

    if (_videoManager) {
        connect(_videoManager,
                &VideoManager::videoSizeChanged,
                this,
                &GimbalControlManager::_handlePulledVideoSize);
        connect(_videoManager,
                &VideoManager::decodingChanged,
                this,
                &GimbalControlManager::_handleVideoDecodingChanged);
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

    _lastEnabled = enabled();
    _configureSdkEndpoint();
    if (_lastEnabled) {
        _sdkPollTimer.start();
        _pollSdk();
    }
}

GimbalControlManager::~GimbalControlManager()
{
    if (_continuousZoomActive && _sdk) {
        _stopContinuousZoom(false);
    }
    // The QObject event loop is no longer available during destruction, so
    // flush the bounded safety stop synchronously instead of leaving its timer.
    (void) _flushPendingManualZoomStop();
}

bool GimbalControlManager::enabled() const
{
    return _settings && _settings->enabled()->rawValue().toBool();
}

double GimbalControlManager::zoomStep() const
{
    if (!_settings) {
        return 1.0;
    }
    // 0x0f和0x18都只有一位小数精度。设置值也先量化到0.1x，保证最小值
    // 锚定的放大网格和分辨率上限锚定的缩小网格都可精确表示。
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
        _setLastError(tr("Waiting for a supported pulled-video stream before controlling zoom."));
        return false;
    }
    if (_continuousZoomActive) {
        // A tap is a newer explicit gesture. Stop native continuous zoom
        // immediately; never leave a held command running behind the tap.
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
    _suppressIdleAlignmentUntilExplicitZoom = false;
    if (!_sendAbsoluteZoomTarget(targetZoom,
                                 false,
                                 replacePendingTarget)) {
        return false;
    }
    qCInfo(GimbalControlLog)
        << "Applied immediate SIYI tap direction" << direction
        << "reference" << referenceZoom
        << "target" << targetZoom
        << "replaced pending target" << replacePendingTarget;
    _setLastError(QString());
    return true;
}

bool GimbalControlManager::setZoom(double zoomLevel)
{
    if (!_cameraCommandAvailable()) {
        return false;
    }
    if (!zoomControlsUnlocked()) {
        _setLastError(tr("Waiting for a supported pulled-video stream before controlling zoom."));
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
        _setLastError(tr("The requested SIYI camera zoom is outside the latched pulled-video resolution limit."));
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
    _suppressIdleAlignmentUntilExplicitZoom = false;
    if (!_sendAbsoluteZoomTarget(boundedTargetZoom,
                                 false,
                                 _absoluteZoomPending)) {
        return false;
    }

    _setLastError(QString());
    return true;
}

bool GimbalControlManager::startZoom(int direction)
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
        _setLastError(tr("Waiting for a supported pulled-video stream before controlling zoom."));
        return false;
    }

    const bool isolateLateZoomResponse = _manualZoomFinalizePending
        || _zoomQueryOutstanding
        || _zoomResponseBlocked
        || _absoluteZoomPending
        || _stableZoomConfirmationPending;
    // This gesture supersedes any release finalization. Complete a pending
    // safety-stop copy synchronously so it cannot stop the new gesture later.
    if (!_flushPendingManualZoomStop()) {
        return false;
    }
    _cancelManualZoomFinalize();
    if (!_zoomDirectionAvailable(normalizedDirection)) {
        _setLastError(tr("No further SIYI zoom target remains in this direction."));
        return false;
    }

    double referenceZoom = kMinZoom;
    double nextTarget = kMinZoom;
    if (!_zoomPlanningReference(&referenceZoom)
        || !A8MiniZoomPolicy::stepTarget(referenceZoom,
                                         zoomStep(),
                                         kMinZoom,
                                         _maximumZoom,
                                         normalizedDirection,
                                         &nextTarget)) {
        _setLastError(tr("Waiting for a valid SIYI zoom planning reference."));
        return false;
    }

    _suppressIdleAlignmentUntilExplicitZoom = false;
    // Both stream-derived endpoints are exact terminal stops. When either is
    // the very next stop, use 0x0f directly instead of starting a native 0x05
    // run for only the shortened endpoint interval.
    const bool nextTargetIsTerminal =
        (normalizedDirection > 0
         && qAbs(nextTarget - _maximumZoom) <= kZoomComparisonTolerance)
        || (normalizedDirection < 0
            && qAbs(nextTarget - kMinZoom) <= kZoomComparisonTolerance);
    if (nextTargetIsTerminal) {
        const bool sent = _sendAbsoluteZoomTarget(
            nextTarget,
            false,
            _absoluteZoomPending);
        if (sent) {
            _setLastError(QString());
            qCInfo(GimbalControlLog)
                << "Handed held SIYI zoom directly to terminal target"
                << nextTarget << "from" << referenceZoom;
        }
        return sent;
    }

    // Native 0x05 is the camera's continuous zoom command. It starts once and
    // is stopped explicitly on release; no absolute-step FIFO is involved.
    _continuousZoomStepTimer.stop();
    _zoomSyncTimer.stop();
    _zoomOperationTimer.stop();
    _cancelOutstandingZoomQuery();
    _zoomResponseBlocked = false;
    _absoluteZoomPending = false;
    _absoluteTargetBucketMatchCount = 0;
    _absoluteZoomTracker.clear();
    _absoluteZoomElapsed.invalidate();
    _alignmentAttemptCount = 0;
    _clearStableZoomConfirmation();

    _manualZoomStopRetryTimer.stop();
    _manualZoomStopRetryAttemptsRemaining = 0;
    _manualZoomSessionHost =
        _settings->sdkHost()->rawValue().toString().trimmed();
    _manualZoomSessionPort = _sdkPort();
    if (!_sdk->sendManualZoomTo(static_cast<qint8>(normalizedDirection),
                                _manualZoomSessionHost,
                                _manualZoomSessionPort)) {
        _manualZoomSessionHost.clear();
        _manualZoomSessionPort = 0;
        return false;
    }

    _setContinuousZoomState(true, normalizedDirection);
    // Keep the last feedback-confirmed logical stop visible while the lens is
    // between stops. Solicited 0x18 samples advance it only after the raw value
    // crosses the midpoint to the next legal stop.
    _continuousZoomWatchdog.start();
    _continuousZoomStepTimer.start(isolateLateZoomResponse
                                       ? kManualZoomStopQueryDelayMs
                                       : kManualZoomPollIntervalMs);
    _sdkResponseTimer.start();
    _setLastError(QString());
    qCInfo(GimbalControlLog)
        << "Started native continuous SIYI zoom direction"
        << normalizedDirection;
    return true;
}

bool GimbalControlManager::stopZoom()
{
    // Release always sends native manual-stop immediately. A short bounded
    // settle/query window may snap the stopped raw value to a legal grid point.
    return _stopContinuousZoom();
}

bool GimbalControlManager::takePhoto()
{
    if (!_cameraCommandAvailable()) {
        return false;
    }
    if (_photoCommandPending) {
        return false;
    }

    _configureSdkEndpoint();
    const bool sent = _sdk->takePhoto();
    if (sent) {
        _photoCommandPending = true;
        _photoFeedbackTimer.start();
        _setLastError(QString());
    }
    return sent;
}

bool GimbalControlManager::toggleVideoRecording()
{
    if (!_cameraCommandAvailable()) {
        return false;
    }
    if (!_cameraStatusKnown) {
        _setLastError(tr("Waiting for the SIYI camera recording status."));
        return false;
    }
    if (_recordingCommandPending) {
        _setLastError(tr("A SIYI camera recording command is already pending."));
        return false;
    }

    _configureSdkEndpoint();
    if (!_sdk->toggleVideoRecording()) {
        return false;
    }

    // 0x0c 没有直接 ACK。先乐观更新界面，忽略命令前在途的 0x0a，
    // 400ms 后主动查询一次状态；只有与本次目标一致的 0x0a 才确认成功。
    _recordingCommandTarget = !_recording;
    _recordingStatusResponseAllowed = false;
    _recordingStatusDelayTimer.start();
    _recordingCommandTimeoutTimer.start();
    // 必须先建立 C++ 门控，再发出 recordingChanged，避免直接信号处理器重入后重复切换。
    _setRecordingCommandPending(true);
    _setRecording(_recordingCommandTarget);
    _setLastError(QString());
    return true;
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
    // Stop native continuous zoom at the endpoint where it started before
    // applying a changed endpoint or enabled state.
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

    const bool nowEnabled = enabled();
    if (_lastEnabled != nowEnabled) {
        _lastEnabled = nowEnabled;
        emit enabledChanged();
        emit zoomAvailabilityChanged();
    }

    if (nowEnabled) {
        _sdkPollTimer.start();
        _pollSdk();
    }
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
        _refreshMaximumZoomCapability("configured zoom step changed");
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
    // heartbeat/diagnostic evidence only. Only a solicited 0x18 response may
    // drive display, boundary protection, planning, or release finalization.
    qCDebug(GimbalControlLog)
        << "Observed unowned SIYI manual-zoom ACK" << zoomLevel;
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

    // Keep 0x16 for protocol/device diagnostics only. The decoded stream
    // resolution is the sole zoom-capability source, so this reply must never
    // activate, shrink, reset or lock the controls.
    _deviceMaximumZoom =
        qRound(qBound(kMinZoom, maximumZoom, kProtocolMaxZoom) * 10.0) / 10.0;
    _deviceMaximumZoomKnown = true;
    qCDebug(GimbalControlLog)
        << "Recorded diagnostic SIYI 0x16 maximum zoom" << _deviceMaximumZoom
        << "without changing the pulled-video zoom capability";
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
    qCInfo(GimbalControlLog)
        << "Observed negotiated main pulled-video resolution:"
        << videoSize.width() << "x" << videoSize.height();

    if (!_confirmPulledVideoResolution(_negotiatedPulledVideoSize,
                                       "negotiated main video sink")) {
        _invalidatePulledVideoResolutionCapability(
            _negotiatedPulledVideoSize,
            "unsupported negotiated main video sink");
    }
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
    double maximumZoom = 0.0;
    const bool supported = videoSize.isValid()
        && !videoSize.isEmpty()
        && videoSize.width() <= std::numeric_limits<quint16>::max()
        && videoSize.height() <= std::numeric_limits<quint16>::max()
        && A8MiniZoomPolicy::maximumZoomForVideoResolution(
            static_cast<quint16>(videoSize.width()),
            static_cast<quint16>(videoSize.height()),
            &maximumZoom);
    if (!supported) {
        _pulledVideoFallbackTimer.stop();
        _videoManagerFallbackCandidate = QSize();
        return;
    }
    if (_videoStreamAvailable
        && _pulledVideoResolutionConfirmed
        && qAbs(_pulledVideoMaximumZoom - maximumZoom) <= kZoomComparisonTolerance) {
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
    qCInfo(GimbalControlLog)
        << "Waiting for stable VideoManager pulled-video resolution fallback:"
        << videoSize.width() << "x" << videoSize.height();
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
        qCWarning(GimbalControlLog)
            << "Ignored unsupported transient pulled-video resolution from"
            << sourceDescription << videoSize.width() << "x" << videoSize.height()
            << "- retaining the unlocked stream ceiling" << _maximumZoom;
        _schedulePulledVideoResolutionFallback();
        return;
    }

    _setVideoStreamAvailable(false);
    if (!_pulledVideoResolutionConfirmed) {
        _schedulePulledVideoResolutionFallback();
        return;
    }

    // A decoded but unsupported size cannot safely reuse a stale
    // resolution-derived ceiling. Keep the UI locked until a supported
    // pulled-video size is confirmed.
    _pulledVideoResolutionConfirmed = false;
    _stopContinuousZoom(false);
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
    _resetMaximumZoomCapability();
    _setZoomStatusKnown(false);
    qCWarning(GimbalControlLog)
        << "Released stale pulled-video zoom capability after" << sourceDescription
        << videoSize.width() << "x" << videoSize.height()
        << "- requesting SIYI current supported zoom range";
    if (enabled()) {
        _configureSdkEndpoint();
        (void) _sdk->requestMaximumZoom();
    }
    _schedulePulledVideoResolutionFallback();
}

void GimbalControlManager::_applyMaximumZoomCapability(
    double maximumZoom,
    const char* sourceDescription)
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

    // 能力来源恢复或上限改变后，先取得两份一致的合法0x18实际倍率。
    // 用户绝对目标只由主动0x18反馈更新，不能用命令本身或ACK更新显示。
    _stopContinuousZoom(false);
    _cancelManualZoomFinalize();
    _sdkResponseTimer.stop();
    _zoomSyncTimer.stop();
    _zoomOperationTimer.stop();
    _cancelOutstandingZoomQuery();
    _zoomResponseBlocked = false;
    if (_absoluteZoomPending
        && _absoluteTargetBucketMatchCount > 0
        && A8MiniZoomPolicy::isAlignedZoom(_absoluteZoomFeedbackSource,
                                            zoomStep(),
                                            kMinZoom,
                                            _maximumZoom)) {
        // A stream-capability change cannot promote a one-sample provisional
        // target before the new ceiling is resynchronized.
        _currentZoomFeedbackSource = _absoluteZoomFeedbackSource;
        _setCurrentZoom(_absoluteZoomFeedbackSource);
    }
    _absoluteZoomPending = false;
    _absoluteTargetBucketMatchCount = 0;
    _absoluteZoomTracker.clear();
    _alignmentAttemptCount = 0;
    _clearAutomaticAlignmentSuppression();
    _suppressIdleAlignmentUntilExplicitZoom = false;
    _requestedZoom = _currentZoom;
    _beginStableZoomConfirmation(true, 0);
    _setZoomStatusKnown(false);
    qCInfo(GimbalControlLog)
        << "Activated SIYI zoom capability from" << sourceDescription
        << "raw maximum" << normalizedCapabilityMaximum
        << "terminal maximum" << alignedMaximum
        << "step" << zoomStep();
    if (enabled()) {
        _cancelOutstandingZoomQuery();
        _zoomOperationTimer.start();
        _scheduleZoomSync();
    }
}

void GimbalControlManager::_refreshMaximumZoomCapability(
    const char* sourceDescription)
{
    if (!_pulledVideoResolutionConfirmed) {
        _resetMaximumZoomCapability();
        return;
    }

    _applyMaximumZoomCapability(_pulledVideoMaximumZoom, sourceDescription);
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

    double maximumZoom = 0.0;
    if (!A8MiniZoomPolicy::maximumZoomForVideoResolution(width,
                                                         height,
                                                         &maximumZoom)) {
        if (_lastRejectedPulledVideoSize != videoSize) {
            _lastRejectedPulledVideoSize = videoSize;
            qCWarning(GimbalControlLog)
                << "Ignoring unsupported pulled video resolution"
                << width << "x" << height
                << "from" << sourceDescription
                << "- waiting for negotiated 1280x720/736, 1920x1080/1088,"
                   " 2560x1440, or 3840/4096x2160 video";
        }
        return false;
    }

    const bool capabilityChanged = !_pulledVideoResolutionConfirmed
        || qAbs(_pulledVideoMaximumZoom - maximumZoom) > kZoomComparisonTolerance;
    if (!capabilityChanged) {
        // Re-decoding the same resolution starts a new usable stream session.
        // Reuse its ceiling, but immediately rebuild the actual-zoom reference
        // which the disconnect path intentionally discarded. Otherwise the
        // buttons look usable while the first tap is rejected until the next
        // two-second background poll.
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
            qCInfo(GimbalControlLog)
                << "Immediately resynchronizing SIYI zoom after same-resolution stream reconnect";
        }
        return true;
    }

    const bool replacingConfirmedResolution = _pulledVideoResolutionConfirmed;
    _lastRejectedPulledVideoSize = QSize();
    _pulledVideoFallbackTimer.stop();
    _videoManagerFallbackCandidate = QSize();
    _pulledVideoResolutionConfirmed = true;
    _pulledVideoMaximumZoom = maximumZoom;
    _refreshMaximumZoomCapability(sourceDescription);
    _setVideoStreamAvailable(true);
    qCInfo(GimbalControlLog)
        << (replacingConfirmedResolution
                ? "Updated pulled video resolution:"
                : "Latched pulled video resolution:")
        << width << "x" << height
        << "source" << sourceDescription
        << "pulled-video maximum zoom" << maximumZoom
        << "effective terminal maximum zoom" << _maximumZoom;
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

    if (_continuousZoomActive) {
        _handleContinuousZoomSample(normalizedActualZoom);
        if (_continuousZoomActive) {
            _continuousZoomStepTimer.start(kManualZoomPollIntervalMs);
        }
        return;
    }

    if (_manualZoomFinalizePending) {
        _finishManualZoomStop(normalizedActualZoom);
        return;
    }

    if (_absoluteZoomPending) {
        _publishZoomProgress(normalizedActualZoom);
        // Some A8 firmware reports a continuous raw value (for example 1.6)
        // after accepting the legal 2.0 stop. Treat a solicited sample as
        // confirmation once it has crossed the midpoint from this
        // transaction's source stop to its target. The published value remains
        // the legal target; the raw value stays in _latestActualZoom for
        // boundary checks and diagnostics.
        const bool exactTargetSample =
            qAbs(normalizedActualZoom - _requestedZoom)
            <= kZoomComparisonTolerance;
        const bool targetBucketSample =
            A8MiniZoomPolicy::feedbackReachesTarget(
                normalizedActualZoom,
                _absoluteZoomFeedbackSource,
                _requestedZoom);
        if (targetBucketSample) {
            // A target-bucket sample interrupts any stable-different run.
            // Target confirmation below owns these samples, so an older
            // alternate candidate must never survive across this boundary.
            _absoluteZoomTracker.reset(_requestedZoom);
            ++_absoluteTargetBucketMatchCount;
            qCInfo(GimbalControlLog)
                << "Mapped SIYI raw zoom" << normalizedActualZoom
                << "from logical source" << _absoluteZoomFeedbackSource
                << "to legal target" << _requestedZoom
                << "bucket confirmations"
                << _absoluteTargetBucketMatchCount;
            if (exactTargetSample
                || _absoluteTargetBucketMatchCount
                    >= kAbsoluteTargetBucketConfirmationCount) {
                _finalizeConfirmedZoom(_requestedZoom);
            } else {
                _scheduleZoomSync();
            }
            return;
        }
        if (_absoluteTargetBucketMatchCount > 0
            && A8MiniZoomPolicy::isAlignedZoom(
                _absoluteZoomFeedbackSource,
                zoomStep(),
                kMinZoom,
                _maximumZoom)) {
            // A single crossing followed by a sample outside the target bucket
            // was transient. Roll the logical display back to the transaction
            // source instead of leaving an unconfirmed target visible.
            _currentZoomFeedbackSource = _absoluteZoomFeedbackSource;
            _setCurrentZoom(_absoluteZoomFeedbackSource);
        }
        _absoluteTargetBucketMatchCount = 0;

        const A8MiniZoomPolicy::TargetObservation observation =
            _absoluteZoomTracker.observe(zoomLevel);
        if (observation == A8MiniZoomPolicy::TargetObservation::Waiting) {
            // 精确目标值会立即确认，非精确目标档由上面的两份连续归属
            // 确认；其余值仍先满足五份稳定证据再进入分级年龄门。
            _scheduleZoomSync();
            return;
        }
        if (observation == A8MiniZoomPolicy::TargetObservation::StableDifferent) {
            // Five repeated samples are only the first evidence layer.
            // Give legal alternate stops at least 3s and off-grid motion
            // samples such as 1.6x at least 5s before bounded recovery.
            // This avoids both the old ~1s false settle and an unnecessary
            // full 10s wait when the camera really stopped elsewhere.
            const double stableZoom =
                _absoluteZoomTracker.stableDifferentZoom();
            const bool stableZoomIsLegal =
                A8MiniZoomPolicy::isAlignedZoom(stableZoom,
                                                zoomStep(),
                                                kMinZoom,
                                                _maximumZoom);
            const qint64 minimumRecoveryAgeMs = stableZoomIsLegal
                ? kLegalStableRecoveryMs
                : kOffGridStableRecoveryMs;
            const qint64 elapsedMs = _absoluteZoomElapsed.isValid()
                ? _absoluteZoomElapsed.elapsed()
                : 0;
            if (elapsedMs < minimumRecoveryAgeMs) {
                qCDebug(GimbalControlLog)
                    << "Retaining SIYI zoom target" << _requestedZoom
                    << "while stable-different candidate" << stableZoom
                    << "awaits recovery age" << minimumRecoveryAgeMs
                    << "elapsed ms" << elapsedMs;
                _scheduleZoomSync();
                return;
            }
            _handleStableUnexpectedZoom(
                _absoluteZoomTracker.stableDifferentZoom());
            return;
        }

        if (!A8MiniZoomPolicy::isAlignedZoom(zoomLevel,
                                             zoomStep(),
                                             kMinZoom,
                                             _maximumZoom)) {
            // 用户可能在镜头运动期间修改zoomStep。旧目标即使已到达，也不能
            // 以新步长的非网格值结束；转入与其他归整共用的有界恢复路径。
            _absoluteZoomPending = false;
            _absoluteZoomTracker.clear();
            const AlignmentAttemptResult alignmentResult =
                _tryRealignStableZoom(zoomLevel);
            if (alignmentResult == AlignmentAttemptResult::CommandSent) {
                return;
            }
            if (alignmentResult == AlignmentAttemptResult::SendFailed) {
                if (_lastError.isEmpty()) {
                    _setLastError(tr("Failed to send the realigned SIYI camera zoom step."));
                }
                return;
            }
            _finalizeConfirmedZoom(zoomLevel);
            _setLastError(tr("The SIYI camera could not be aligned after the zoom step changed; "
                             "the last legal value is retained and the controls remain available."));
            return;
        }

        // Exact target confirmation remains as a defensive fallback for policy
        // implementations which distinguish it from midpoint ownership.
        _finalizeConfirmedZoom(_requestedZoom);
        return;
    }

    if (_zoomStatusKnown
        && normalizedActualZoom
            <= _maximumZoom + kZoomComparisonTolerance
        && A8MiniZoomPolicy::feedbackReachesTarget(
            normalizedActualZoom,
            _currentZoomFeedbackSource,
            _currentZoom)) {
        // The raw firmware value still belongs to the currently displayed
        // logical bucket. Do not let ordinary polling reinterpret 1.6 as a new
        // external zoom and auto-correct a confirmed 2.0 back to 1.5.
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

        // 拉流上限降低时镜头可能仍在更高倍率。先确认两次相同超限值，再命令
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
            << "above pulled-video limit" << _maximumZoom
            << "- commanding the exact pulled-video terminal maximum";
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
                // Keep the latest feedback-quantized legal bucket visible.
                // This expired gesture owns no future movement, so the raw
                // off-grid value is diagnostic only.
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
        _setCameraStatusKnown(true);
        _setRecording(true);
        _setLastError(QString());
        break;
    case 2:
        _setCameraStatusKnown(true);
        _setRecording(false);
        _setLastError(tr("The SIYI camera has no storage card."));
        break;
    case 3:
        _setCameraStatusKnown(false);
        _setLastError(tr("The SIYI camera reported video data loss."));
        break;
    default:
        statusHandled = false;
        break;
    }

    if (statusHandled && _recordingCommandPending) {
        _finishRecordingCommand();
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
        _finishRecordingCommand();
        _setCameraStatusKnown(false);
        _setLastError(tr("Video recording failed or the storage card is unavailable."));
        _syncCameraStatus();
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
    const bool hadSingleProvisionalTarget =
        _absoluteZoomPending && _absoluteTargetBucketMatchCount > 0;
    const bool provisionalSourceIsLegal =
        hadSingleProvisionalTarget
        && A8MiniZoomPolicy::isAlignedZoom(_absoluteZoomFeedbackSource,
                                            zoomStep(),
                                            kMinZoom,
                                            _maximumZoom);
    if (provisionalSourceIsLegal) {
        // A local transport failure interrupts the required pair of
        // non-exact target samples. Never preserve the provisional target as
        // the planning reference after the transaction is discarded.
        _currentZoomFeedbackSource = _absoluteZoomFeedbackSource;
        _setCurrentZoom(_absoluteZoomFeedbackSource);
    }
    _absoluteZoomPending = false;
    _absoluteTargetBucketMatchCount = 0;
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
    _finishRecordingCommand();
    _setSdkResponding(false);
    if (hadSingleProvisionalTarget && !provisionalSourceIsLegal) {
        _setZoomStatusKnown(false);
    }
    if (enabled()
        && _videoStreamAvailable
        && _maximumZoomKnown
        && _zoomStatusKnown
        && A8MiniZoomPolicy::isAlignedZoom(_currentZoom,
                                            zoomStep(),
                                            kMinZoom,
                                            _maximumZoom)) {
        // SDK reachability is diagnostic and may briefly flap while the
        // decoded-video session remains healthy. Keep the last feedback-owned
        // legal bucket visible and usable; no old movement is replayed.
        _requestedZoom = _currentZoom;
        _setZoomValueUncertain(false);
        emit zoomAvailabilityChanged();
    } else {
        _setZoomStatusKnown(false);
    }
    _setCameraStatusKnown(false);
    _setRecording(false);
    _setLastError(message);
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
    const bool hadSingleProvisionalTarget =
        _absoluteZoomPending && _absoluteTargetBucketMatchCount > 0;
    const bool provisionalSourceIsLegal =
        hadSingleProvisionalTarget
        && A8MiniZoomPolicy::isAlignedZoom(_absoluteZoomFeedbackSource,
                                            zoomStep(),
                                            kMinZoom,
                                            _maximumZoom);
    if (provisionalSourceIsLegal) {
        // Heartbeat loss interrupts the required pair of non-exact target
        // samples. Keep only the previous confirmed logical stop.
        _currentZoomFeedbackSource = _absoluteZoomFeedbackSource;
        _setCurrentZoom(_absoluteZoomFeedbackSource);
    }
    _absoluteZoomPending = false;
    _absoluteTargetBucketMatchCount = 0;
    _absoluteZoomTracker.clear();
    _alignmentAttemptCount = 0;
    _clearStableZoomConfirmation();
    if (zoomMovementMayBeUnconfirmed) {
        _suppressIdleAlignmentUntilExplicitZoom = true;
    }
    _resetMaximumZoomCapability();
    _photoFeedbackTimer.stop();
    _photoCommandPending = false;
    _finishRecordingCommand();
    _setSdkResponding(false);
    if (hadSingleProvisionalTarget && !provisionalSourceIsLegal) {
        _setZoomStatusKnown(false);
    }
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

    // Keep polling 0x16 for diagnostics. Once the pulled-video resolution is
    // confirmed it no longer changes the UI ceiling.
    _configureSdkEndpoint();
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
    const bool hadSingleUnconfirmedTargetBucket =
        _absoluteZoomPending && _absoluteTargetBucketMatchCount > 0;
    const bool timedOutFeedbackSourceIsLegal =
        hadSingleUnconfirmedTargetBucket
        && A8MiniZoomPolicy::isAlignedZoom(_absoluteZoomFeedbackSource,
                                            zoomStep(),
                                            kMinZoom,
                                            _maximumZoom);
    const bool timedOutDuringAlignment =
        _alignmentAttemptCount > 0 && _alignmentSourceZoomValid;
    const double timedOutAlignmentSource = _alignmentSourceZoom;
    _zoomSyncTimer.stop();
    _zoomOperationTimer.stop();
    _cancelOutstandingZoomQuery();
    _zoomResponseBlocked = false;
    if (timedOutFeedbackSourceIsLegal) {
        // A non-exact target needs two consecutive ownership samples. If the
        // transaction expires after only one, discard its provisional display
        // and return to the last confirmed logical stop.
        _currentZoomFeedbackSource = _absoluteZoomFeedbackSource;
        _setCurrentZoom(_absoluteZoomFeedbackSource);
    }
    _absoluteZoomPending = false;
    _absoluteTargetBucketMatchCount = 0;
    _absoluteZoomTracker.clear();
    if (timedOutDuringAlignment) {
        // 自动归整已发出但没有得到可判定结果时，不允许下一次空闲轮询
        // 立即重启同一轮归整。实际值、步长、能力或用户操作变化后才重试。
        _suppressAutomaticAlignment(timedOutAlignmentSource);
    }
    _alignmentAttemptCount = 0;
    _clearStableZoomConfirmation();
    if (!hadConfirmedZoom
        || (hadSingleUnconfirmedTargetBucket
            && !timedOutFeedbackSourceIsLegal)) {
        _setZoomStatusKnown(false);
    } else {
        // Retain the last feedback-quantized legal bucket visibly. The expired
        // command is discarded below and cannot be replayed; a later explicit
        // gesture starts from this deterministic logical stop.
        _setZoomValueUncertain(false);
    }
    // 即使显示状态未改变，仍需显式通知absolute/stable pending已经清除，
    // 让QML刷新计划边界；pending不参与视频会话解锁。
    emit zoomAvailabilityChanged();
    _setLastError(tr("Timed out waiting for the SIYI zoom target; the previous legal value is retained internally and no old input will be replayed."));

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
        // Continuous 0x05 is allowed to run only while 0x18 feedback remains
        // timely. On a lost progress query, stop now instead of blind-running
        // toward or past the resolution-derived ceiling.
        (void) _stopContinuousZoom();
        _setLastError(
            tr("Held SIYI zoom was stopped because current-zoom feedback timed out."));
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

    if (_absoluteZoomPending && _absoluteTargetBucketMatchCount > 0) {
        // The non-exact target rule requires consecutive responses. A lost
        // query breaks that evidence run; keep the transaction alive for a
        // fresh pair, but discard its provisional target display now.
        if (A8MiniZoomPolicy::isAlignedZoom(_absoluteZoomFeedbackSource,
                                            zoomStep(),
                                            kMinZoom,
                                            _maximumZoom)) {
            _currentZoomFeedbackSource = _absoluteZoomFeedbackSource;
            _setCurrentZoom(_absoluteZoomFeedbackSource);
        } else {
            _setZoomStatusKnown(false);
        }
        _absoluteTargetBucketMatchCount = 0;
        _absoluteZoomTracker.reset(_requestedZoom);
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
    if (!enabled()
        || !_maximumZoomKnown
        || (!_continuousZoomActive && !_manualZoomFinalizePending)
        || _zoomResponseBlocked
        || _zoomQueryOutstanding) {
        return;
    }

    // Manual 0x05 does not stream progress ACKs. Use one serialized 0x18
    // query at a time for timely stop and resolution-limit protection.
    if (!_sendCurrentZoomQuery(false)) {
        if (_continuousZoomActive) {
            (void) _stopContinuousZoom();
            _setLastError(
                tr("Held SIYI zoom was stopped because its progress query could not be sent."));
        } else if (_manualZoomFinalizePending) {
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

    _stopContinuousZoom();
    _setLastError(tr("Held SIYI continuous zoom was stopped by the safety timeout."));
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

    _finishRecordingCommand();
    _setCameraStatusKnown(false);
    _setLastError(tr("Timed out waiting for the SIYI camera recording status."));
    _syncCameraStatus();
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
                                                   bool manualFinalizeCorrection,
                                                   double feedbackSourceZoom)
{
    const bool replacingPendingTarget = _absoluteZoomPending;
    if (!_sdk
        || !_maximumZoomKnown
        || _continuousZoomActive
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

    double resolvedFeedbackSource = feedbackSourceZoom;
    if (!qIsFinite(resolvedFeedbackSource)
        || resolvedFeedbackSource < kMinZoom - kZoomComparisonTolerance
        || resolvedFeedbackSource > _maximumZoom + kZoomComparisonTolerance) {
        if (replacingPendingTarget) {
            resolvedFeedbackSource = _requestedZoom;
        } else if (_zoomStatusKnown) {
            resolvedFeedbackSource = _currentZoom;
        } else if (_latestActualZoomKnown) {
            resolvedFeedbackSource = _latestActualZoom;
        } else {
            resolvedFeedbackSource = targetZoom;
        }
    }
    resolvedFeedbackSource =
        qRound(qBound(kMinZoom, resolvedFeedbackSource, _maximumZoom) * 10.0)
        / 10.0;
    if (!A8MiniZoomPolicy::isAlignedZoom(resolvedFeedbackSource,
                                          zoomStep(),
                                          kMinZoom,
                                          _maximumZoom)
        && qAbs(targetZoom - resolvedFeedbackSource)
            > kZoomComparisonTolerance) {
        const int direction =
            targetZoom > resolvedFeedbackSource ? 1 : -1;
        double previousLegalStop = 0.0;
        bool previousStopResolved = false;
        if ((direction > 0
             && qAbs(targetZoom - _maximumZoom)
                 <= kZoomComparisonTolerance)
            || (direction < 0
                && qAbs(targetZoom - kMinZoom)
                    <= kZoomComparisonTolerance)) {
            previousStopResolved =
                A8MiniZoomPolicy::terminalHandoffStop(
                    zoomStep(),
                    kMinZoom,
                    _maximumZoom,
                    direction,
                    &previousLegalStop);
        } else {
            previousStopResolved =
                A8MiniZoomPolicy::stepTarget(targetZoom,
                                             zoomStep(),
                                             kMinZoom,
                                             _maximumZoom,
                                             -direction,
                                             &previousLegalStop);
        }
        if (previousStopResolved) {
            resolvedFeedbackSource = previousLegalStop;
        }
    }
    if (qAbs(targetZoom - resolvedFeedbackSource)
        > zoomStep() + kZoomComparisonTolerance) {
        const int direction = targetZoom > resolvedFeedbackSource ? 1 : -1;
        double adjacentZoom =
            targetZoom - direction * zoomStep();
        const bool targetIsDirectionalTerminal =
            (direction > 0
             && qAbs(targetZoom - _maximumZoom)
                 <= kZoomComparisonTolerance)
            || (direction < 0
                && qAbs(targetZoom - kMinZoom)
                    <= kZoomComparisonTolerance);
        const bool adjacentResolved =
            targetIsDirectionalTerminal
            ? A8MiniZoomPolicy::terminalHandoffStop(
                  zoomStep(),
                  kMinZoom,
                  _maximumZoom,
                  direction,
                  &adjacentZoom)
            : A8MiniZoomPolicy::isAlignedZoom(
                  adjacentZoom,
                  zoomStep(),
                  kMinZoom,
                  _maximumZoom);
        if (adjacentResolved) {
            resolvedFeedbackSource = qRound(adjacentZoom * 10.0) / 10.0;
        }
    }

    // Every physical movement shares the same stop barrier. This also covers
    // automatic grid alignment: an event-loop delay must never let a retained
    // 0x05 stop packet arrive behind a newer 0x0f command.
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

    // 全系统同一时刻只允许一个0x0f目标在途。ACK仅表示受理，最终状态必须
    // 等主动查询的0x18确认。期间保留上一合法档；raw跨过相邻档中点后才
    // 更新显示，非精确目标档还需第二份连续归属反馈才能结束事务。
    if (alignmentCorrection) {
        ++_alignmentAttemptCount;
    } else {
        // 用户明确发起的新目标可以重新尝试此前被硬件拒绝的网格点。
        _clearAutomaticAlignmentSuppression();
        _alignmentAttemptCount = 0;
    }
    _requestedZoom = targetZoom;
    _absoluteZoomFeedbackSource = resolvedFeedbackSource;
    _absoluteTargetBucketMatchCount = 0;
    _absoluteZoomPending = true;
    _absoluteZoomTracker.reset(targetZoom);
    // QElapsedTimer is invalid before the first command. start() is valid for
    // both the first target and each replacement and resets the target age.
    _absoluteZoomElapsed.start();
    // Keep the last feedback-confirmed logical bucket visible. The requested
    // target is never published on send/ACK; a solicited 0x18 must first cross
    // the source-to-target midpoint.
    emit zoomAvailabilityChanged();
    if (!_zoomOperationTimer.isActive()) {
        _zoomOperationTimer.start();
    }
    _sdkResponseTimer.start();
    _scheduleZoomSync();
    qCInfo(GimbalControlLog)
        << "Started SIYI absolute zoom target" << targetZoom
        << "feedback source" << _absoluteZoomFeedbackSource
        << (replacingPendingTarget ? "replacing pending target" : "new target")
        << "alignment attempt" << _alignmentAttemptCount;
    return true;
}

bool GimbalControlManager::_sendAlignmentCorrection(double targetZoom,
                                                    double sourceZoom)
{
    _alignmentSourceZoom = qRound(sourceZoom * 10.0) / 10.0;
    _alignmentSourceZoomValid = true;
    const int correctionDirection =
        targetZoom > sourceZoom + kZoomComparisonTolerance
            ? 1
            : (targetZoom < sourceZoom - kZoomComparisonTolerance ? -1 : 0);
    double feedbackSourceZoom = _currentZoom;
    double previousLegalStop = 0.0;
    bool previousStopResolved = false;
    if (correctionDirection != 0
        && ((correctionDirection > 0
             && qAbs(targetZoom - _maximumZoom)
                 <= kZoomComparisonTolerance)
            || (correctionDirection < 0
                && qAbs(targetZoom - kMinZoom)
                    <= kZoomComparisonTolerance))) {
        previousStopResolved =
            A8MiniZoomPolicy::terminalHandoffStop(
                zoomStep(),
                kMinZoom,
                _maximumZoom,
                correctionDirection,
                &previousLegalStop);
    } else if (correctionDirection != 0) {
        previousStopResolved =
            A8MiniZoomPolicy::stepTarget(targetZoom,
                                         zoomStep(),
                                         kMinZoom,
                                         _maximumZoom,
                                         -correctionDirection,
                                         &previousLegalStop);
    }
    if (previousStopResolved) {
        // The physical raw source can itself be an off-grid firmware value.
        // Use the preceding logical stop so a stable raw such as 1.6 can own
        // the commanded 2.0 target instead of waiting forever for exact 2.0.
        feedbackSourceZoom = previousLegalStop;
    }
    const bool sent = _sendAbsoluteZoomTarget(targetZoom,
                                              true,
                                              false,
                                              false,
                                              feedbackSourceZoom);
    if (!sent) {
        _alignmentSourceZoomValid = false;
    }
    return sent;
}

bool GimbalControlManager::_stopContinuousZoom(bool finalizeAfterStop)
{
    if (!_continuousZoomActive) {
        if (!finalizeAfterStop) {
            _cancelManualZoomFinalize();
        }
        return true;
    }

    const int stoppedDirection = _continuousZoomDirection;
    const QString stopHost = _manualZoomSessionHost;
    const quint16 stopPort = _manualZoomSessionPort;

    // Clear the physical-running state before sending. sendManualZoomTo can
    // synchronously emit communicationError; this ordering keeps the error
    // path from recursively attempting another stop.
    _continuousZoomWatchdog.stop();
    _continuousZoomStepTimer.stop();
    _manualZoomStopRetryTimer.stop();
    _manualZoomStopRetryAttemptsRemaining = 0;
    _cancelOutstandingZoomQuery();
    _zoomResponseBlocked = false;
    _setContinuousZoomState(false);

    const bool shouldFinalize = finalizeAfterStop
        && enabled()
        && _videoStreamAvailable
        && _maximumZoomKnown;
    _manualZoomFinalizePending = shouldFinalize;
    _manualZoomFinalizeDirection = shouldFinalize ? stoppedDirection : 0;
    _manualZoomFinalCandidateValid = false;
    _manualZoomFinalCandidate = kMinZoom;
    _manualZoomFinalMatchCount = 0;
    if (shouldFinalize) {
        _manualZoomFinalizeElapsed.start();
        _manualZoomFinalizeTimer.start(kManualZoomFinalizeTimeoutMs);
    } else {
        _manualZoomFinalizeElapsed.invalidate();
        _manualZoomFinalizeTimer.stop();
    }

    const bool stopEndpointValid =
        _sdk && !stopHost.isEmpty() && stopPort != 0;
    _manualZoomSessionHost = stopEndpointValid ? stopHost : QString();
    _manualZoomSessionPort = stopEndpointValid ? stopPort : 0;
    _manualZoomStopRetryAttemptsRemaining =
        stopEndpointValid ? kManualZoomStopMaximumRetryAttempts : 0;
    const bool stopSent = _sdk
        && !stopHost.isEmpty()
        && stopPort != 0
        && _sdk->sendManualZoomTo(0, stopHost, stopPort);
    if (!stopSent) {
        // Even a local UDP write failure is not proof that the prior start
        // never reached the camera. Retain the latched endpoint for bounded
        // stop retries instead of abandoning a possibly moving lens.
        if (stopEndpointValid) {
            _manualZoomSessionHost = stopHost;
            _manualZoomSessionPort = stopPort;
            _manualZoomStopRetryAttemptsRemaining =
                kManualZoomStopMaximumRetryAttempts;
            _manualZoomStopRetryTimer.start(kManualZoomStopRetryMs);
        } else {
            _manualZoomSessionHost.clear();
            _manualZoomSessionPort = 0;
            _manualZoomStopRetryAttemptsRemaining = 0;
        }
        _manualZoomFinalizePending = false;
        _manualZoomFinalizeDirection = 0;
        _manualZoomFinalCandidateValid = false;
        _manualZoomFinalMatchCount = 0;
        _manualZoomFinalizeElapsed.invalidate();
        _manualZoomFinalizeTimer.stop();
        emit zoomAvailabilityChanged();
        return false;
    }

    // UDP has no delivery acknowledgement. Send one bounded safety duplicate
    // unless a newer zoom gesture cancels it first.
    _manualZoomSessionHost = stopHost;
    _manualZoomSessionPort = stopPort;
    _manualZoomStopRetryAttemptsRemaining =
        kManualZoomStopMaximumRetryAttempts;
    _manualZoomStopRetryTimer.start(kManualZoomStopRetryMs);
    if (shouldFinalize) {
        _continuousZoomStepTimer.start(kManualZoomStopQueryDelayMs);
    }
    emit zoomAvailabilityChanged();
    qCInfo(GimbalControlLog)
        << "Stopped native continuous SIYI zoom direction"
        << stoppedDirection
        << "finalize" << shouldFinalize;
    return true;
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

void GimbalControlManager::_publishLegalZoom(double zoomLevel)
{
    const double normalizedZoom = qRound(zoomLevel * 10.0) / 10.0;
    if (!A8MiniZoomPolicy::isAlignedZoom(normalizedZoom,
                                         zoomStep(),
                                         kMinZoom,
                                         _maximumZoom)) {
        return;
    }

    _requestedZoom = normalizedZoom;
    _currentZoomFeedbackSource = normalizedZoom;
    if (!_latestActualZoomKnown) {
        _latestActualZoom = normalizedZoom;
        _latestActualZoomKnown = true;
    }
    _setCurrentZoom(normalizedZoom);
    _setZoomValueUncertain(false);
    _setZoomStatusKnown(true);
    emit zoomAvailabilityChanged();
}

void GimbalControlManager::_handleContinuousZoomSample(double zoomLevel)
{
    if (!_continuousZoomActive || !qIsFinite(zoomLevel)) {
        return;
    }

    const double normalizedZoom = qRound(zoomLevel * 10.0) / 10.0;
    _publishZoomProgress(normalizedZoom);

    double boundaryTarget = 0.0;
    if (_continuousZoomDirection > 0) {
        if (normalizedZoom >= _maximumZoom - kZoomComparisonTolerance) {
            boundaryTarget = _maximumZoom;
        } else {
            // Switch from free-running 0x05 to the exact 0x0f ceiling at the
            // last minimum-anchored zoom-in stop. Do not derive this with a
            // reverse step: reverse travel is maximum-anchored (5.5 -> 4.5)
            // and therefore has a different phase.
            double previousMaximumStop = 0.0;
            if (A8MiniZoomPolicy::terminalHandoffStop(
                    zoomStep(),
                    kMinZoom,
                    _maximumZoom,
                    1,
                    &previousMaximumStop)
                && normalizedZoom
                    >= previousMaximumStop - kZoomComparisonTolerance) {
                boundaryTarget = _maximumZoom;
            }
        }
    } else if (_continuousZoomDirection < 0) {
        if (normalizedZoom <= kMinZoom + kZoomComparisonTolerance) {
            boundaryTarget = kMinZoom;
        } else {
            // Symmetric lower-end handoff: reverse travel uses the
            // maximum-anchored grid, so 1080P/1.0x step reaches 1.5 before the
            // exact 1.0 terminal stop.
            double previousMinimumStop = 0.0;
            if (A8MiniZoomPolicy::terminalHandoffStop(
                    zoomStep(),
                    kMinZoom,
                    _maximumZoom,
                    -1,
                    &previousMinimumStop)
                && normalizedZoom
                    <= previousMinimumStop + kZoomComparisonTolerance) {
                boundaryTarget = kMinZoom;
            }
        }
    }

    if (boundaryTarget <= 0.0) {
        return;
    }

    double boundaryFeedbackSource = 0.0;
    if (!A8MiniZoomPolicy::terminalHandoffStop(
            zoomStep(),
            kMinZoom,
            _maximumZoom,
            _continuousZoomDirection,
            &boundaryFeedbackSource)) {
        return;
    }

    const bool stopped = _stopContinuousZoom(false);
    // Complete the bounded duplicate stop synchronously before 0x0f. A delayed
    // stop must never arrive behind the exact boundary target.
    const bool duplicateStopSent = _flushPendingManualZoomStop();
    _cancelManualZoomFinalize();
    if (!stopped || !duplicateStopSent || !_maximumZoomKnown) {
        _suppressIdleAlignmentUntilExplicitZoom = true;
        return;
    }

    // A sample captured while 0x05 was running is never proof that the lens
    // remained at that value after stop. Always command and confirm the exact
    // legal boundary; do not publish the in-motion sample.
    if (!_sendAbsoluteZoomTarget(boundaryTarget,
                                 false,
                                 false,
                                 false,
                                 boundaryFeedbackSource)) {
        _suppressIdleAlignmentUntilExplicitZoom = true;
    }
}

void GimbalControlManager::_publishZoomProgress(double zoomLevel)
{
    int progressDirection = 0;
    if (_continuousZoomActive) {
        progressDirection = _continuousZoomDirection;
    } else if (_manualZoomFinalizePending) {
        progressDirection = _manualZoomFinalizeDirection;
    } else if (_absoluteZoomPending) {
        progressDirection =
            _requestedZoom > _absoluteZoomFeedbackSource
                ? 1
                : (_requestedZoom < _absoluteZoomFeedbackSource ? -1 : 0);
    }
    if ((progressDirection != -1 && progressDirection != 1)
        || !qIsFinite(zoomLevel)) {
        return;
    }

    double logicalZoom = _currentZoom;
    bool logicalZoomResolvedFromRaw = false;
    if (!_zoomStatusKnown
        || !A8MiniZoomPolicy::isAlignedZoom(logicalZoom,
                                             zoomStep(),
                                             kMinZoom,
                                             _maximumZoom)) {
        if (!A8MiniZoomPolicy::alignmentTarget(
                qBound(kMinZoom, zoomLevel, _maximumZoom),
                zoomStep(),
                kMinZoom,
                _maximumZoom,
                0,
                &logicalZoom)) {
            return;
        }
        logicalZoomResolvedFromRaw = true;
    }

    // A single 0x18 sample can skip more than one UI bucket while native 0x05
    // continues smoothly. Walk every crossed midpoint, but publish only the
    // final legal bucket so QML never sees raw values such as 1.6 or 1.8.
    double feedbackSourceZoom = logicalZoomResolvedFromRaw
        ? logicalZoom
        : _currentZoomFeedbackSource;
    for (int crossedStops = 0; crossedStops < 64; ++crossedStops) {
        double nextLogicalZoom = 0.0;
        if (!A8MiniZoomPolicy::stepTarget(logicalZoom,
                                          zoomStep(),
                                          kMinZoom,
                                          _maximumZoom,
                                          progressDirection,
                                          &nextLogicalZoom)) {
            break;
        }
        if (_absoluteZoomPending
            && ((progressDirection > 0
                 && nextLogicalZoom
                     > _requestedZoom + kZoomComparisonTolerance)
                || (progressDirection < 0
                    && nextLogicalZoom
                        < _requestedZoom - kZoomComparisonTolerance))) {
            break;
        }
        if (_absoluteZoomPending
            && qAbs(nextLogicalZoom - _requestedZoom)
                <= kZoomComparisonTolerance
            && !A8MiniZoomPolicy::feedbackReachesTarget(
                zoomLevel,
                _absoluteZoomFeedbackSource,
                _requestedZoom)) {
            // Intermediate buckets may be published while a replacement
            // target is still moving, but the target bucket itself requires
            // the transaction's bounded ownership interval. An overshoot such
            // as raw 2.6 for target 2.0 must not look completed.
            break;
        }

        const double midpoint = (logicalZoom + nextLogicalZoom) / 2.0;
        const bool crossedMidpoint = progressDirection > 0
            ? zoomLevel >= midpoint
            : zoomLevel <= midpoint;
        if (!crossedMidpoint) {
            break;
        }
        feedbackSourceZoom = logicalZoom;
        logicalZoom = nextLogicalZoom;
    }

    if (!A8MiniZoomPolicy::isAlignedZoom(logicalZoom,
                                         zoomStep(),
                                         kMinZoom,
                                         _maximumZoom)) {
        return;
    }

    if (!_absoluteZoomPending) {
        _requestedZoom = logicalZoom;
    }
    _currentZoomFeedbackSource = feedbackSourceZoom;
    _setCurrentZoom(logicalZoom);
    _setZoomStatusKnown(true);
    _setZoomValueUncertain(false);
}

void GimbalControlManager::_finishManualZoomStop(double zoomLevel)
{
    if (!_manualZoomFinalizePending || !qIsFinite(zoomLevel)) {
        return;
    }
    if (!_manualZoomFinalizeDeadlineOpen()) {
        _expireManualZoomFinalize();
        return;
    }

    const double normalizedZoom = qRound(zoomLevel * 10.0) / 10.0;
    _publishZoomProgress(normalizedZoom);
    if (!_manualZoomFinalCandidateValid
        || qAbs(normalizedZoom - _manualZoomFinalCandidate)
            > kZoomComparisonTolerance) {
        _manualZoomFinalCandidate = normalizedZoom;
        _manualZoomFinalCandidateValid = true;
        _manualZoomFinalMatchCount = 1;
        _continuousZoomStepTimer.start(kManualZoomPollIntervalMs);
        return;
    }

    ++_manualZoomFinalMatchCount;
    if (_manualZoomFinalMatchCount < kManualZoomFinalConfirmationCount) {
        _continuousZoomStepTimer.start(kManualZoomPollIntervalMs);
        return;
    }

    double alignedZoom = _currentZoom;
    if (!_zoomStatusKnown
        || !A8MiniZoomPolicy::isAlignedZoom(alignedZoom,
                                             zoomStep(),
                                             kMinZoom,
                                             _maximumZoom)) {
        if (!A8MiniZoomPolicy::alignmentTarget(
                qBound(kMinZoom, normalizedZoom, _maximumZoom),
                zoomStep(),
                kMinZoom,
                _maximumZoom,
                0,
                &alignedZoom)) {
            _cancelManualZoomFinalize();
            _suppressIdleAlignmentUntilExplicitZoom = true;
            _setLastError(tr("Failed to resolve the stopped SIYI zoom to a legal step."));
            return;
        }
    }

    if (qAbs(normalizedZoom - alignedZoom) <= kZoomComparisonTolerance) {
        _cancelManualZoomFinalize();
        _suppressIdleAlignmentUntilExplicitZoom = false;
        _publishLegalZoom(alignedZoom);
        _setLastError(QString());
        return;
    }

    // This is the only post-release movement allowed, and it is issued inside
    // the bounded finalize window. If that window expires, no correction is
    // sent later.
    if (!_manualZoomFinalizeDeadlineOpen()) {
        _expireManualZoomFinalize();
        return;
    }
    double feedbackSourceZoom = _currentZoom;
    double previousLegalStop = 0.0;
    const int correctionDirection =
        alignedZoom > normalizedZoom + kZoomComparisonTolerance
            ? 1
            : (alignedZoom
                       < normalizedZoom - kZoomComparisonTolerance
                   ? -1
                   : 0);
    bool previousStopResolved = false;
    if (correctionDirection != 0
        && ((correctionDirection > 0
             && qAbs(alignedZoom - _maximumZoom)
                 <= kZoomComparisonTolerance)
            || (correctionDirection < 0
                && qAbs(alignedZoom - kMinZoom)
                    <= kZoomComparisonTolerance))) {
        previousStopResolved =
            A8MiniZoomPolicy::terminalHandoffStop(
                zoomStep(),
                kMinZoom,
                _maximumZoom,
                correctionDirection,
                &previousLegalStop);
    } else if (correctionDirection != 0) {
        previousStopResolved =
            A8MiniZoomPolicy::stepTarget(alignedZoom,
                                         zoomStep(),
                                         kMinZoom,
                                         _maximumZoom,
                                         -correctionDirection,
                                         &previousLegalStop);
    }
    if (previousStopResolved) {
        feedbackSourceZoom = previousLegalStop;
    }
    _suppressIdleAlignmentUntilExplicitZoom = false;
    if (!_sendAbsoluteZoomTarget(alignedZoom,
                                 false,
                                 false,
                                 true,
                                 feedbackSourceZoom)) {
        if (_manualZoomFinalizePending) {
            _cancelManualZoomFinalize();
        }
        _suppressIdleAlignmentUntilExplicitZoom = true;
        _setZoomValueUncertain(false);
        if (_lastError.isEmpty()) {
            _setLastError(tr("Failed to align the stopped SIYI zoom to a legal step."));
        }
        return;
    }
    qCInfo(GimbalControlLog)
        << "Aligned native continuous SIYI zoom raw" << normalizedZoom
        << "to legal stop" << alignedZoom;
}

void GimbalControlManager::_handleStableUnexpectedZoom(double zoomLevel)
{
    const double requestedZoom = _requestedZoom;
    _absoluteZoomPending = false;
    _absoluteZoomTracker.clear();
    emit zoomAvailabilityChanged();

    // Refresh 0x16 for diagnostics only. It never changes a resolution-derived
    // UI ceiling.
    _configureSdkEndpoint();
    if (!_sdk->requestMaximumZoom()) {
        // 发送失败会同步触发communicationError并清理Manager状态；不能在
        // 返回后又用下面的恢复分支把离线状态覆盖成zoomStatusKnown。
        return;
    }

    if (A8MiniZoomPolicy::isAlignedZoom(zoomLevel,
                                        zoomStep(),
                                        kMinZoom,
                                        _maximumZoom)) {
        _finalizeConfirmedZoom(zoomLevel);
        _setLastError(
            tr("The SIYI zoom target %1x was not reached; the stable actual value %2x was restored.")
                .arg(requestedZoom, 0, 'f', 1)
                .arg(zoomLevel, 0, 'f', 1));
        qCWarning(GimbalControlLog)
            << "SIYI absolute zoom target" << requestedZoom
            << "did not settle;"
            << "restored stable actual zoom" << zoomLevel;
        return;
    }

    // The old tap has expired. Never turn it into a delayed correction several
    // seconds later. The off-grid raw sample remains private, while the last
    // feedback-quantized legal bucket remains visible and available as the
    // deterministic source for the next explicit gesture.
    _zoomOperationTimer.stop();
    _requestedZoom = _currentZoom;
    _suppressIdleAlignmentUntilExplicitZoom = true;
    if (!_zoomStatusKnown) {
        _setZoomStatusKnown(false);
    } else {
        _setZoomValueUncertain(false);
    }
    _suppressAutomaticAlignment(zoomLevel);
    emit zoomAvailabilityChanged();
    _setLastError(
        tr("The SIYI camera did not reach the requested legal zoom stop; "
           "the old tap was discarded and will not be replayed."));
}

GimbalControlManager::AlignmentAttemptResult
GimbalControlManager::_tryRealignStableZoom(double zoomLevel, int direction)
{
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
    // A stable-value check must not blank an already confirmed legal bucket.
    // It will atomically replace that bucket only after the new feedback has
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
    const double completedFeedbackSource = _absoluteZoomFeedbackSource;
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
        _absoluteZoomElapsed.invalidate();
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
    _absoluteTargetBucketMatchCount = 0;
    _absoluteZoomTracker.clear();
    _absoluteZoomElapsed.invalidate();
    _alignmentAttemptCount = 0;
    _clearAutomaticAlignmentSuppression();
    _clearStableZoomConfirmation();
    _requestedZoom = normalizedZoom;
    _currentZoomFeedbackSource = completedAbsoluteTarget
        ? completedFeedbackSource
        : normalizedZoom;
    // _handleCurrentZoom records the solicited raw sample before resolving it
    // to a legal logical bucket. Preserve that raw evidence (for example 1.6)
    // instead of replacing it with the displayed 2.0 target.
    if (!_latestActualZoomKnown) {
        _latestActualZoom = normalizedZoom;
        _latestActualZoomKnown = true;
    }
    _absoluteZoomFeedbackSource = normalizedZoom;
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
    // SDK timeout clears only the diagnostic 0x16 value. A confirmed stream
    // resolution keeps its exact ceiling and keeps the controls unlocked.
    _deviceMaximumZoomKnown = false;
    _deviceMaximumZoom = kProtocolMaxZoom;
    _clearAutomaticAlignmentSuppression();
    _setMaximumZoomKnown(_pulledVideoResolutionConfirmed);
    if (_pulledVideoResolutionConfirmed) {
        _capabilityMaximumZoom = _pulledVideoMaximumZoom;
        double alignedMaximum = kMinZoom;
        if (A8MiniZoomPolicy::alignedMaximumZoom(_capabilityMaximumZoom,
                                                  zoomStep(),
                                                  kMinZoom,
                                                  &alignedMaximum)) {
            _setMaximumZoom(alignedMaximum);
        }
    } else {
        _capabilityMaximumZoom = kDefaultMaxZoom;
        double alignedMaximum = kMinZoom;
        if (A8MiniZoomPolicy::alignedMaximumZoom(kDefaultMaxZoom,
                                                  zoomStep(),
                                                  kMinZoom,
                                                  &alignedMaximum)) {
            _setMaximumZoom(alignedMaximum);
        }
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
    // 这里只接收已经校验过的设备回读值；绝不把命令目标或非法设备回包
    // 冒充当前倍率。
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
    qCInfo(GimbalControlLog)
        << "SIYI zoom controls"
        << (zoomControlsUnlocked() ? "unlocked by pulled video stream"
                                   : "locked because pulled video stream is unavailable");
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
    }
}

void GimbalControlManager::_setRecording(bool recording)
{
    if (_recording != recording) {
        _recording = recording;
        emit recordingChanged();
    }
}

void GimbalControlManager::_setRecordingCommandPending(bool pending)
{
    if (_recordingCommandPending != pending) {
        _recordingCommandPending = pending;
        emit recordingCommandPendingChanged();
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
