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
    _zoomQueryTimeoutTimer.setInterval(1000);
    _sdkPollTimer.setInterval(2000);
    _continuousZoomWatchdog.setSingleShot(true);
    // 长按采用受控间隔的0x0f合法目标循环；60秒只兜底丢失的释放事件。
    _continuousZoomWatchdog.setInterval(60000);
    _continuousZoomStepTimer.setSingleShot(true);
    _continuousZoomStepTimer.setInterval(kConfirmedZoomStepDelayMs);
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
            &GimbalControlManager::_sendNextContinuousZoomStep);
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
        _stopContinuousZoom();
    }
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
    // 0x0f和0x18都只有一位小数精度。设置值也先量化到0.1x，保证所有
    // 目标都严格落在以1.0x为锚点的整数步长网格。
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
    return _queueZoomStep(1);
}

bool GimbalControlManager::zoomOut()
{
    return _queueZoomStep(-1);
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
        // Keep the controls visibly usable and queue the intent until that
        // reply is available instead of re-locking both buttons.
        return _maximumZoom > kMinZoom + kZoomComparisonTolerance;
    }

    for (const int queuedDirection : _queuedZoomDirections) {
        double queuedTarget = 0.0;
        if (A8MiniZoomPolicy::stepTarget(plannedZoom,
                                         zoomStep(),
                                         kMinZoom,
                                         _maximumZoom,
                                         queuedDirection,
                                         &queuedTarget)) {
            plannedZoom = queuedTarget;
        }
    }

    double targetZoom = 0.0;
    return A8MiniZoomPolicy::stepTarget(plannedZoom,
                                        zoomStep(),
                                        kMinZoom,
                                        _maximumZoom,
                                        direction,
                                        &targetZoom);
}

bool GimbalControlManager::_queueZoomStep(int direction)
{
    if (!_cameraCommandAvailable()) {
        return false;
    }
    if (!zoomControlsUnlocked()) {
        _setLastError(tr("Waiting for a supported pulled-video stream before controlling zoom."));
        return false;
    }
    if (_continuousZoomActive) {
        // A tap is a newer, explicit user intent. Recover from a missing
        // touch-release/cancel event instead of leaving apparently enabled
        // buttons rejecting every tap until the long watchdog expires.
        qCWarning(GimbalControlLog)
            << "Stopping residual continuous SIYI zoom before accepting tap direction"
            << direction;
        _stopContinuousZoom();
    }
    if (!_zoomDirectionAvailable(direction)) {
        _setLastError(direction > 0
                          ? tr("The SIYI camera is already at its valid zoom-in boundary.")
                          : tr("The SIYI camera is already at its valid zoom-out boundary."));
        return false;
    }
    if (_queuedZoomDirections.size() >= kMaximumQueuedZoomSteps) {
        _setLastError(tr("Too many SIYI zoom steps are queued."));
        return false;
    }

    _queuedZoomDirections.enqueue(direction);
    qCInfo(GimbalControlLog)
        << "Queued SIYI zoom direction" << direction
        << "depth" << _queuedZoomDirections.size()
        << "pending target" << (_absoluteZoomPending ? _requestedZoom : 0.0);
    emit zoomAvailabilityChanged();
    _setLastError(QString());
    _dispatchNextZoomStep();
    return true;
}

void GimbalControlManager::_clearQueuedZoomSteps()
{
    if (_queuedZoomDirections.isEmpty()) {
        return;
    }

    _queuedZoomDirections.clear();
    if (!_continuousZoomActive) {
        _continuousZoomStepTimer.stop();
    }
    emit zoomAvailabilityChanged();
}

void GimbalControlManager::_dispatchNextZoomStep()
{
    if (!zoomControlsUnlocked()
        || _stableZoomConfirmationPending) {
        return;
    }

    const bool replacePendingTarget = _absoluteZoomPending;
    if (replacePendingTarget) {
        if (_queuedZoomDirections.isEmpty() && !_continuousZoomActive) {
            return;
        }

        // The camera may report an intermediate value such as 1.6x for a
        // prolonged period. A newer user gesture must not remain hostage to
        // the old target's two-sample confirmation. Preserve a conservative
        // command/query isolation interval, then replace the old absolute
        // target with the next legal planned stop.
        const qint64 elapsedMs =
            _absoluteZoomElapsed.isValid() ? _absoluteZoomElapsed.elapsed() : 0;
        if (elapsedMs < kPendingZoomRetargetIntervalMs) {
            const int remainingMs = static_cast<int>(
                qMax<qint64>(1, kPendingZoomRetargetIntervalMs - elapsedMs));
            _continuousZoomStepTimer.start(remainingMs);
            qCDebug(GimbalControlLog)
                << "Deferred newer SIYI zoom intent for" << remainingMs
                << "ms while target" << _requestedZoom
                << "remains pending; queued depth"
                << _queuedZoomDirections.size()
                << "continuous" << _continuousZoomActive;
            return;
        }
    }

    if (!_queuedZoomDirections.isEmpty()) {
        double referenceZoom = kMinZoom;
        if (replacePendingTarget) {
            referenceZoom = _requestedZoom;
        } else if (!_zoomPlanningReference(&referenceZoom)) {
            _beginStableZoomConfirmation(true, 0);
            _scheduleZoomSync();
            return;
        }

        while (!_queuedZoomDirections.isEmpty()) {
            const int direction = _queuedZoomDirections.dequeue();
            emit zoomAvailabilityChanged();

            double targetZoom = 0.0;
            if (!A8MiniZoomPolicy::stepTarget(referenceZoom,
                                              zoomStep(),
                                              kMinZoom,
                                              _maximumZoom,
                                              direction,
                                              &targetZoom)) {
                continue;
            }

            if (!_sendAbsoluteZoomTarget(targetZoom,
                                         false,
                                         replacePendingTarget)) {
                _clearQueuedZoomSteps();
            }
            return;
        }
    }

    if (!_continuousZoomActive) {
        return;
    }

    double referenceZoom = kMinZoom;
    if (replacePendingTarget) {
        referenceZoom = _requestedZoom;
    } else if (!_zoomPlanningReference(&referenceZoom)) {
        _beginStableZoomConfirmation(true, 0);
        _scheduleZoomSync();
        return;
    }

    double targetZoom = 0.0;
    if (!A8MiniZoomPolicy::stepTarget(referenceZoom,
                                      zoomStep(),
                                      kMinZoom,
                                      _maximumZoom,
                                      _continuousZoomDirection,
                                      &targetZoom)) {
        _stopContinuousZoom();
        _setLastError(QString());
        return;
    }

    if (!_sendAbsoluteZoomTarget(targetZoom,
                                 false,
                                 replacePendingTarget)) {
        _stopContinuousZoom();
    }
}

GimbalControlManager::AlignmentAttemptResult
GimbalControlManager::_dispatchPendingZoomIntentFrom(double referenceZoom)
{
    if (!qIsFinite(referenceZoom)) {
        return AlignmentAttemptResult::SendFailed;
    }

    // A tap may be queued before the first stable 0x18 value is known. Apply
    // that intent directly from the twice-confirmed raw value. Otherwise an
    // off-grid 1.6x startup value would first be normalized to 2.0x and the
    // still-queued tap would incorrectly send a second step to 3.0x.
    while (!_queuedZoomDirections.isEmpty()) {
        const int direction = _queuedZoomDirections.dequeue();
        emit zoomAvailabilityChanged();

        double targetZoom = 0.0;
        if (!A8MiniZoomPolicy::stepTarget(referenceZoom,
                                          zoomStep(),
                                          kMinZoom,
                                          _maximumZoom,
                                          direction,
                                          &targetZoom)) {
            continue;
        }

        if (_sendAbsoluteZoomTarget(targetZoom)) {
            return AlignmentAttemptResult::CommandSent;
        }
        _clearQueuedZoomSteps();
        return AlignmentAttemptResult::SendFailed;
    }

    if (!_continuousZoomActive) {
        return AlignmentAttemptResult::NotNeeded;
    }

    double targetZoom = 0.0;
    if (!A8MiniZoomPolicy::stepTarget(referenceZoom,
                                      zoomStep(),
                                      kMinZoom,
                                      _maximumZoom,
                                      _continuousZoomDirection,
                                      &targetZoom)) {
        _stopContinuousZoom();
        return AlignmentAttemptResult::NotNeeded;
    }

    if (_sendAbsoluteZoomTarget(targetZoom)) {
        return AlignmentAttemptResult::CommandSent;
    }
    _stopContinuousZoom();
    return AlignmentAttemptResult::SendFailed;
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
    if (_absoluteZoomPending) {
        _setLastError(tr("Waiting for the previous SIYI zoom step to finish."));
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

    // 绝对倍率是新的镜头运动命令。若长按正处在两个合法目标之间的节流
    // 间隔，只需停止后续重复，再发送这个唯一的新目标。
    if (_continuousZoomActive && !_stopContinuousZoom()) {
        return false;
    }
    _clearQueuedZoomSteps();
    if (!_sendAbsoluteZoomTarget(boundedTargetZoom)) {
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

    // 长按不发送自由运行的0x05，而是按受控间隔重复完整0x0f分度。
    // 若上一目标尚未确认，新目标从上一合法请求继续计划，绝不会把
    // 1.8x等中间raw值作为锚点产生2.8x之类的漂移网格。
    if (!zoomControlsUnlocked()) {
        _setLastError(tr("Waiting for a supported pulled-video stream before controlling zoom."));
        return false;
    }
    if (!_zoomDirectionAvailable(normalizedDirection)) {
        _setLastError(tr("No further SIYI zoom target remains in this direction."));
        return false;
    }

    _setContinuousZoomState(true, normalizedDirection);
    _continuousZoomWatchdog.start();
    _setLastError(QString());
    _dispatchNextZoomStep();
    return true;
}

bool GimbalControlManager::stopZoom()
{
    // 长按是有节流的绝对步进，不存在自由运动命令；释放只停止后续步进，
    // 已经发出的当前0x0f仍等待0x18确认，不能发送会干扰它的0x05停止包。
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
    // endpoint或启用状态变更时，先终止可能仍在排队的长按重复步骤。
    if (_continuousZoomActive) {
        _stopContinuousZoom();
    }

    _continuousZoomWatchdog.stop();
    _zoomSyncTimer.stop();
    _zoomOperationTimer.stop();
    _cancelOutstandingZoomQuery();
    _zoomResponseBlocked = false;
    _absoluteZoomPending = false;
    _absoluteZoomTracker.clear();
    _clearQueuedZoomSteps();
    _latestActualZoomKnown = false;
    _alignmentAttemptCount = 0;
    _clearAutomaticAlignmentSuppression();
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

    if (_continuousZoomActive) {
        _stopContinuousZoom();
    }
    _clearQueuedZoomSteps();
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
        // cancels every sent/queued gesture and requires a fresh stream check.
        _pulledVideoFallbackTimer.stop();
        _videoManagerFallbackCandidate = QSize();
        _negotiatedPulledVideoSize = QSize();
        _setVideoStreamAvailable(false);
        _clearQueuedZoomSteps();
        _stopContinuousZoom();
        _zoomSyncTimer.stop();
        _zoomOperationTimer.stop();
        _cancelOutstandingZoomQuery();
        _zoomResponseBlocked = false;
        _absoluteZoomPending = false;
        _absoluteZoomTracker.clear();
        _clearStableZoomConfirmation();
        _latestActualZoomKnown = false;
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
    _stopContinuousZoom();
    _clearQueuedZoomSteps();
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
    // 用户发出的绝对目标同样要求两次命中，不能用命令目标更新显示。
    _stopContinuousZoom();
    _clearQueuedZoomSteps();
    _sdkResponseTimer.stop();
    _zoomSyncTimer.stop();
    _zoomOperationTimer.stop();
    _cancelOutstandingZoomQuery();
    _zoomResponseBlocked = false;
    _absoluteZoomPending = false;
    _absoluteZoomTracker.clear();
    _alignmentAttemptCount = 0;
    _clearAutomaticAlignmentSuppression();
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
        // Do not let the unchanged-capability fast path leave the UI locked.
        _lastRejectedPulledVideoSize = QSize();
        _pulledVideoFallbackTimer.stop();
        _videoManagerFallbackCandidate = QSize();
        _setVideoStreamAvailable(true);
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

    if (_absoluteZoomPending) {
        const A8MiniZoomPolicy::TargetObservation observation =
            _absoluteZoomTracker.observe(zoomLevel);
        if (observation == A8MiniZoomPolicy::TargetObservation::Waiting) {
            // 目标值仍需两次命中；正常运动值可以变化或短时重复。
            // 非目标样本先满足五份稳定证据再进入分级年龄门。
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

        // Publish the exact legal target, never a merely tolerated raw sample.
        _finalizeConfirmedZoom(_requestedZoom);
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
        // 回有效网格上限；确认期间用uncertain隐藏旧数字，不能冒充新实际值。
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
        const AlignmentAttemptResult intentResult =
            _dispatchPendingZoomIntentFrom(zoomLevel);
        if (intentResult == AlignmentAttemptResult::CommandSent
            || intentResult == AlignmentAttemptResult::SendFailed) {
            return;
        }
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
        const AlignmentAttemptResult intentResult =
            _dispatchPendingZoomIntentFrom(zoomLevel);
        if (intentResult == AlignmentAttemptResult::CommandSent
            || intentResult == AlignmentAttemptResult::SendFailed) {
            return;
        }
        if (normalizeToStepGrid) {
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
        // 初次同步或已有值重新核对都显示--，不把旧停点冒充新的实际值；
        // 这不改变视频会话解锁状态。
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
    if (_continuousZoomActive) {
        // 长按只排队绝对倍率步骤；通信失败时取消尚未发送的后续步骤。
        _stopContinuousZoom();
    }
    _sdkResponseTimer.stop();
    _zoomSyncTimer.stop();
    _zoomOperationTimer.stop();
    _cancelOutstandingZoomQuery();
    _zoomResponseBlocked = false;
    _absoluteZoomPending = false;
    _absoluteZoomTracker.clear();
    _clearQueuedZoomSteps();
    _alignmentAttemptCount = 0;
    _clearAutomaticAlignmentSuppression();
    _clearStableZoomConfirmation();
    _resetMaximumZoomCapability();
    _photoFeedbackTimer.stop();
    _photoCommandPending = false;
    _finishRecordingCommand();
    _setSdkResponding(false);
    _setZoomStatusKnown(false);
    _setCameraStatusKnown(false);
    _setRecording(false);
    _setLastError(message);
}

void GimbalControlManager::_markSdkNotResponding()
{
    if (_continuousZoomActive) {
        _stopContinuousZoom();
    }
    _continuousZoomWatchdog.stop();
    _zoomSyncTimer.stop();
    _zoomOperationTimer.stop();
    _cancelOutstandingZoomQuery();
    _zoomResponseBlocked = false;
    _absoluteZoomPending = false;
    _absoluteZoomTracker.clear();
    _clearQueuedZoomSteps();
    _alignmentAttemptCount = 0;
    _clearStableZoomConfirmation();
    _resetMaximumZoomCapability();
    _photoFeedbackTimer.stop();
    _photoCommandPending = false;
    _finishRecordingCommand();
    _setSdkResponding(false);
    _setZoomStatusKnown(false);
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
    const double timedOutRequestedZoom = _requestedZoom;
    const bool hasNewerUserIntent =
        !_queuedZoomDirections.isEmpty() || _continuousZoomActive;
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
        // 命令总超时不能抹掉操作前已经确认的实际倍率。先恢复控件，再立即
        // 后台读取实际值；问号明确提示该值在重新核对，不能冒充当前真值。
        _setZoomValueUncertain(true);
    }
    // uncertain可能在上一次超时后已经为true；仍需显式通知absolute/stable
    // pending已经清除，通知QML刷新计划边界；它不参与会话解锁。
    emit zoomAvailabilityChanged();
    _setLastError(tr("Timed out waiting for the SIYI zoom target; hiding the zoom value while rechecking the camera."));

    // A timeout belongs to the old command generation. Do not silently erase
    // taps or a hold which arrived after that command was sent. Continue from
    // the old requested stop so each accepted direction still represents one
    // configured step, even if the most recent raw sample was 1.6x.
    if (hasNewerUserIntent && zoomControlsUnlocked()) {
        const AlignmentAttemptResult intentResult =
            _dispatchPendingZoomIntentFrom(timedOutRequestedZoom);
        if (intentResult == AlignmentAttemptResult::CommandSent) {
            qCWarning(GimbalControlLog)
                << "Timed-out SIYI target" << timedOutRequestedZoom
                << "was replaced by a newer queued/held user intent";
            _setLastError(QString());
            return;
        }
        if (intentResult == AlignmentAttemptResult::SendFailed) {
            return;
        }
    }

    _requestedZoom = _currentZoom;
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
            && _zoomStatusKnown)) {
        return;
    }
    if (_zoomQueryOutstanding) {
        return;
    }

    _sendCurrentZoomQuery(false);
}

void GimbalControlManager::_sendNextContinuousZoomStep()
{
    _dispatchNextZoomStep();
}

void GimbalControlManager::_stopContinuousZoomForSafety()
{
    if (!_continuousZoomActive) {
        return;
    }

    _stopContinuousZoom();
    _setLastError(tr("Held zoom repetition was stopped by the safety timeout."));
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
    _zoomQueryTimeoutTimer.start();
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
                                                   bool replacePendingTarget)
{
    const bool replacingPendingTarget = _absoluteZoomPending;
    if (!_sdk
        || !_maximumZoomKnown
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

    _clearStableZoomConfirmation();
    _cancelOutstandingZoomQuery();
    _zoomSyncTimer.stop();
    _configureSdkEndpoint();
    if (!_sdk->sendAbsoluteZoom(targetZoom)) {
        // A failed replacement send must not strand the old transaction after
        // its query was canceled above. Keep confirming the old target.
        if (replacingPendingTarget && _absoluteZoomPending) {
            _scheduleZoomSync();
        }
        return false;
    }
    if (!alignmentCorrection) {
        // A newer explicit target owns a fresh bounded confirmation window.
        // Stop only after the replacement datagram was accepted locally so a
        // failed send cannot remove the old transaction's deadline.
        _zoomOperationTimer.stop();
    }

    // 全系统同一时刻只允许一个0x0f目标在途。ACK仅表示受理，最终状态必须
    // 等主动查询的0x18确认。保留上一份合法值供恢复/规划，但用uncertain
    // 让QML在运动期间显示--，既不冒充requested目标也不冒充旧实际倍率。
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
    // QElapsedTimer is invalid before the first command. start() is valid for
    // both the first target and each replacement and resets the target age.
    _absoluteZoomElapsed.start();
    if (_zoomStatusKnown) {
        // The lens may already be between legal stops. Hide the previous
        // number while moving rather than presenting a stale value as the
        // actual zoom. Only a twice-confirmed legal 0x18 value restores it.
        _setZoomValueUncertain(true);
    }
    emit zoomAvailabilityChanged();
    if (!_zoomOperationTimer.isActive()) {
        _zoomOperationTimer.start();
    }
    _sdkResponseTimer.start();
    _scheduleZoomSync();
    if (!_queuedZoomDirections.isEmpty() || _continuousZoomActive) {
        _continuousZoomStepTimer.start(kPendingZoomRetargetIntervalMs);
    }
    qCInfo(GimbalControlLog)
        << "Started SIYI absolute zoom target" << targetZoom
        << (replacingPendingTarget ? "replacing pending target" : "new target")
        << "alignment attempt" << _alignmentAttemptCount;
    return true;
}

bool GimbalControlManager::_sendAlignmentCorrection(double targetZoom,
                                                    double sourceZoom)
{
    _alignmentSourceZoom = qRound(sourceZoom * 10.0) / 10.0;
    _alignmentSourceZoomValid = true;
    const bool sent = _sendAbsoluteZoomTarget(targetZoom,
                                              true,
                                              false);
    if (!sent) {
        _alignmentSourceZoomValid = false;
    }
    return sent;
}

bool GimbalControlManager::_stopContinuousZoom()
{
    if (!_continuousZoomActive) {
        return true;
    }

    _continuousZoomWatchdog.stop();
    if (_queuedZoomDirections.isEmpty()) {
        _continuousZoomStepTimer.stop();
    }
    _setContinuousZoomState(false);
    return true;
}

void GimbalControlManager::_handleStableUnexpectedZoom(double zoomLevel)
{
    const double requestedZoom = _requestedZoom;
    const bool hasNewerUserIntent =
        !_queuedZoomDirections.isEmpty() || _continuousZoomActive;
    _absoluteZoomPending = false;
    _absoluteZoomTracker.clear();
    emit zoomAvailabilityChanged();

    // The stable-different result completes only the old target. A later tap
    // or an active hold is a newer command generation and takes priority over
    // automatic recovery of that old target. Plan from requestedZoom so a
    // second '+' after requested 2.0x still means 3.0x even if raw is 1.6x.
    if (hasNewerUserIntent) {
        const AlignmentAttemptResult intentResult =
            _dispatchPendingZoomIntentFrom(requestedZoom);
        if (intentResult == AlignmentAttemptResult::CommandSent) {
            qCWarning(GimbalControlLog)
                << "Stable-different SIYI target" << requestedZoom
                << "reported" << zoomLevel
                << "and was replaced by a newer queued/held user intent";
            _setLastError(QString());
            return;
        }
        if (intentResult == AlignmentAttemptResult::SendFailed) {
            return;
        }
    }

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

    // A stable off-grid value is not a valid final state. Try a bounded
    // correction to a legal stop in the original requested direction. The
    // shared attempt limit prevents a permanently rejecting/stalled camera
    // from creating an automatic loop.
    // Recover in the user's original target direction. For example, if a
    // zoom-out target 1.0x stalls at 1.6x, the correction must be 1.0x rather
    // than the numerically nearest 2.0x.
    const int recoveryDirection =
        requestedZoom > zoomLevel + kZoomComparisonTolerance
        ? 1
        : (requestedZoom < zoomLevel - kZoomComparisonTolerance ? -1 : 0);
    const AlignmentAttemptResult alignmentResult =
        _tryRealignStableZoom(zoomLevel, recoveryDirection);
    if (alignmentResult == AlignmentAttemptResult::CommandSent) {
        _setLastError(
            tr("The SIYI zoom target %1x did not settle at a legal stop (reported %2x); applying a bounded "
               "correction in the requested direction.")
                .arg(requestedZoom, 0, 'f', 1)
                .arg(zoomLevel, 0, 'f', 1));
        return;
    }
    if (alignmentResult == AlignmentAttemptResult::SendFailed) {
        return;
    }

    // After the bounded attempts are exhausted, never publish the off-grid
    // sample. Keep the last legal value as uncertain and leave the
    // stream-session controls available for an explicit recovery gesture.
    _zoomOperationTimer.stop();
    _requestedZoom = _currentZoom;
    if (_zoomStatusKnown) {
        _setZoomValueUncertain(true);
    } else {
        _setZoomStatusKnown(false);
    }
    _suppressAutomaticAlignment(zoomLevel);
    emit zoomAvailabilityChanged();
    _setLastError(
        tr("The SIYI camera remained at an off-grid value after bounded recovery; "
           "the last legal value is retained and the controls remain available."));
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
    if (_zoomStatusKnown) {
        _setZoomValueUncertain(true);
    }
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
        _absoluteZoomElapsed.invalidate();
        _alignmentAttemptCount = 0;
        _clearStableZoomConfirmation();
        _requestedZoom = _currentZoom;
        if (hadConfirmedZoom) {
            _setZoomValueUncertain(true);
        } else {
            _setZoomStatusKnown(false);
        }
        emit zoomAvailabilityChanged();
        if (!_queuedZoomDirections.isEmpty() || _continuousZoomActive) {
            _continuousZoomStepTimer.start(kConfirmedZoomStepDelayMs);
        }
        return;
    }

    const bool controlsWereWaiting = !_zoomStatusKnown;
    _zoomSyncTimer.stop();
    _zoomOperationTimer.stop();
    _cancelOutstandingZoomQuery();
    _zoomResponseBlocked = false;
    _absoluteZoomPending = false;
    _absoluteZoomTracker.clear();
    _absoluteZoomElapsed.invalidate();
    _alignmentAttemptCount = 0;
    _clearAutomaticAlignmentSuppression();
    _clearStableZoomConfirmation();
    _requestedZoom = normalizedZoom;
    _latestActualZoom = normalizedZoom;
    _latestActualZoomKnown = true;
    _setCurrentZoom(normalizedZoom);
    _setZoomValueUncertain(false);
    _setZoomStatusKnown(true);
    emit zoomAvailabilityChanged();
    _setLastError(QString());

    if (completedAbsoluteTarget) {
        qCInfo(GimbalControlLog)
            << "Confirmed SIYI absolute zoom target" << completedRequestedZoom
            << "at actual" << normalizedZoom
            << "queued depth" << _queuedZoomDirections.size()
            << "continuous" << _continuousZoomActive;
    } else if (controlsWereWaiting) {
        qCInfo(GimbalControlLog)
            << "Confirmed stable SIYI zoom" << zoomLevel
            << "- zoom controls are ready";
    }

    if (!_queuedZoomDirections.isEmpty() || _continuousZoomActive) {
        _continuousZoomStepTimer.start(kConfirmedZoomStepDelayMs);
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
