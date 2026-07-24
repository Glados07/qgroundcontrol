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
    // 长按采用逐步确认的0x0f循环；60秒只兜底丢失的释放事件。
    _continuousZoomWatchdog.setInterval(60000);
    _continuousZoomStepTimer.setSingleShot(true);
    _continuousZoomStepTimer.setInterval(180);
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
    // 不按当前分辨率范围缩小用户配置值。中间倍率始终使用完整步长；
    // 只有最后一次进入真实上限、或从非网格上限返回时允许边界吸附。
    return qBound(0.1,
                  _settings->zoomStep()->rawValue().toDouble(),
                  kProtocolMaxZoom - kMinZoom);
}

bool GimbalControlManager::zoomInAvailable() const
{
    double targetZoom = 0.0;
    return enabled()
        && _maximumZoomKnown
        && _zoomStatusKnown
        && !_absoluteZoomPending
        && !_stableZoomConfirmationPending
        && !_continuousZoomActive
        && A8MiniZoomPolicy::stepTarget(_currentZoom,
                                        zoomStep(),
                                        kMinZoom,
                                        _maximumZoom,
                                        1,
                                        &targetZoom);
}

bool GimbalControlManager::zoomOutAvailable() const
{
    double targetZoom = 0.0;
    return enabled()
        && _maximumZoomKnown
        && _zoomStatusKnown
        && !_absoluteZoomPending
        && !_stableZoomConfirmationPending
        && !_continuousZoomActive
        && A8MiniZoomPolicy::stepTarget(_currentZoom,
                                        zoomStep(),
                                        kMinZoom,
                                        _maximumZoom,
                                        -1,
                                        &targetZoom);
}

bool GimbalControlManager::zoomIn()
{
    if (!_zoomStatusKnown || _absoluteZoomPending || _continuousZoomActive) {
        _setLastError(tr("Waiting for the SIYI camera zoom value."));
        if (!_absoluteZoomPending && !_continuousZoomActive) {
            requestCurrentZoom();
        }
        return false;
    }

    double targetZoom = 0.0;
    if (!A8MiniZoomPolicy::stepTarget(_currentZoom,
                                      zoomStep(),
                                      kMinZoom,
                                      _maximumZoom,
                                      1,
                                      &targetZoom)) {
        _setLastError(tr("The SIYI camera is already at its valid zoom-in boundary."));
        return false;
    }
    return setZoom(targetZoom);
}

bool GimbalControlManager::zoomOut()
{
    if (!_zoomStatusKnown || _absoluteZoomPending || _continuousZoomActive) {
        _setLastError(tr("Waiting for the SIYI camera zoom value."));
        if (!_absoluteZoomPending && !_continuousZoomActive) {
            requestCurrentZoom();
        }
        return false;
    }

    double targetZoom = 0.0;
    if (!A8MiniZoomPolicy::stepTarget(_currentZoom,
                                      zoomStep(),
                                      kMinZoom,
                                      _maximumZoom,
                                      -1,
                                      &targetZoom)) {
        _setLastError(tr("The SIYI camera is already at its valid zoom-out boundary."));
        return false;
    }
    return setZoom(targetZoom);
}

bool GimbalControlManager::setZoom(double zoomLevel)
{
    if (!_cameraCommandAvailable()) {
        return false;
    }
    if (!_maximumZoomKnown) {
        _setLastError(tr("Waiting for the SIYI camera zoom range."));
        _pollSdk();
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

    // 绝对倍率是新的镜头运动命令。若长按正处在两个已确认分度之间的等待
    // 间隔，只需停止后续重复，再发送这个唯一的新目标。
    if (_continuousZoomActive && !_stopContinuousZoom()) {
        return false;
    }
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

    // 长按不再发送自由运行的0x05。它串行重复完整0x0f分度，每一步必须
    // 先由0x18确认，因而不会停在1.8x并继续产生2.8x之类的漂移网格。
    if (!_maximumZoomKnown) {
        _setLastError(tr("Waiting for the first supported pulled-video resolution."));
        return false;
    }
    if (!_zoomStatusKnown || _absoluteZoomPending) {
        _setLastError(tr("Waiting for the SIYI camera zoom value."));
        if (!_absoluteZoomPending) {
            requestCurrentZoom();
        }
        return false;
    }

    double targetZoom = 0.0;
    if (!A8MiniZoomPolicy::stepTarget(_currentZoom,
                                      zoomStep(),
                                      kMinZoom,
                                      _maximumZoom,
                                      normalizedDirection,
                                      &targetZoom)) {
        _setLastError(tr("No further SIYI zoom target remains in this direction."));
        return false;
    }

    _setContinuousZoomState(true, normalizedDirection);
    if (!_sendAbsoluteZoomTarget(targetZoom)) {
        _setContinuousZoomState(false);
        return false;
    }

    _continuousZoomWatchdog.start();
    _setLastError(QString());
    return true;
}

bool GimbalControlManager::stopZoom()
{
    // 长按是串行绝对步进，不存在自由运动命令；释放只停止后续步进，
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
    _absoluteZoomMatchCount = 0;
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
    emit zoomAvailabilityChanged();

    if (_continuousZoomActive) {
        _stopContinuousZoom();
    }
    if (!enabled() || !_maximumZoomKnown || _absoluteZoomPending) {
        return;
    }

    // 步长改变后，旧倍率可能不在新网格上。重新读取两次稳定值并在必要时
    // 用0x0f吸附，确认完成前不公开旧倍率。
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

    // 0x0f ACK不含可关联的操作代次，迟到的拒绝ACK不能取消一个更新的目标。
    // 保持当前目标在途并继续以0x18实际倍率作为唯一完成依据。
    qCWarning(GimbalControlLog)
        << "SIYI camera returned a negative absolute-zoom ACK;"
        << "waiting for confirmed current zoom";
    _absoluteZoomMatchCount = 0;
    _setLastError(tr("The SIYI camera did not accept the zoom step; verifying the actual zoom value."));
    if (!_zoomQueryOutstanding && !_zoomResponseBlocked) {
        _scheduleZoomSync();
    }
}

void GimbalControlManager::_handleMaximumZoom(double maximumZoom)
{
    if (!enabled() || _pulledVideoResolutionConfirmed) {
        return;
    }

    // 0x16由设备返回“当前支持缩放范围”。它只在实际拉流尺寸尚未锁存时
    // 作为恢复后备；一旦取得真实拉流尺寸，分辨率策略仍是最终依据。
    _applyMaximumZoomCapability(maximumZoom, "SIYI current supported zoom range");
}

void GimbalControlManager::_handlePulledVideoSize()
{
    _tryConfirmPulledVideoResolution();
    _schedulePulledVideoResolutionFallback();
}

void GimbalControlManager::_handleVideoDecodingChanged()
{
    if (_videoManager && !_videoManager->decoding()) {
        // 新管线可能无法再次触发旧sink观察器。断流时只释放直接尺寸标记，
        // 保留最近能力；恢复后新直接结果或稳定VideoManager尺寸均可更新上限。
        _pulledVideoFallbackTimer.stop();
        _videoManagerFallbackCandidate = QSize();
        _negotiatedPulledVideoSize = QSize();
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

    // 有效的直接观察结果（即使不在支持白名单内）比VideoManager兜底更可信。
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
    if (_pulledVideoResolutionConfirmed
        && qAbs(_maximumZoom - maximumZoom) <= kZoomComparisonTolerance) {
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
    if (!_pulledVideoResolutionConfirmed) {
        return;
    }

    // 新的直接协商尺寸比旧能力可信。未知尺寸不能继续沿用旧上限；
    // 立即退回0x16设备范围后备，并在得到新能力后重新双确认实际倍率。
    _pulledVideoResolutionConfirmed = false;
    _stopContinuousZoom();
    _sdkResponseTimer.stop();
    _zoomSyncTimer.stop();
    _zoomOperationTimer.stop();
    _cancelOutstandingZoomQuery();
    _zoomResponseBlocked = false;
    _absoluteZoomPending = false;
    _absoluteZoomMatchCount = 0;
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

    const double normalizedMaximum =
        qRound(qBound(kMinZoom, maximumZoom, kProtocolMaxZoom) * 10.0) / 10.0;
    const bool capabilityChanged = !_maximumZoomKnown
        || qAbs(_maximumZoom - normalizedMaximum) > kZoomComparisonTolerance;

    _setMaximumZoom(normalizedMaximum);
    _setMaximumZoomKnown(true);
    if (!capabilityChanged) {
        return;
    }

    // 能力来源恢复或上限改变后，先取得两份一致的合法0x18实际倍率。
    // 用户发出的绝对目标同样要求两次命中，不能用命令目标更新显示。
    _stopContinuousZoom();
    _sdkResponseTimer.stop();
    _zoomSyncTimer.stop();
    _zoomOperationTimer.stop();
    _cancelOutstandingZoomQuery();
    _zoomResponseBlocked = false;
    _absoluteZoomPending = false;
    _absoluteZoomMatchCount = 0;
    _requestedZoom = _currentZoom;
    _beginStableZoomConfirmation(true, 0);
    _setZoomStatusKnown(false);
    qCInfo(GimbalControlLog)
        << "Activated SIYI zoom capability from" << sourceDescription
        << "maximum zoom" << normalizedMaximum;
    if (enabled()) {
        _cancelOutstandingZoomQuery();
        _zoomOperationTimer.start();
        _scheduleZoomSync();
    }
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
        || qAbs(_maximumZoom - maximumZoom) > kZoomComparisonTolerance;
    if (!capabilityChanged) {
        return true;
    }

    const bool replacingConfirmedResolution = _pulledVideoResolutionConfirmed;
    _lastRejectedPulledVideoSize = QSize();
    _pulledVideoFallbackTimer.stop();
    _videoManagerFallbackCandidate = QSize();
    _pulledVideoResolutionConfirmed = true;
    _applyMaximumZoomCapability(maximumZoom, sourceDescription);
    qCInfo(GimbalControlLog)
        << (replacingConfirmedResolution
                ? "Updated pulled video resolution:"
                : "Latched pulled video resolution:")
        << width << "x" << height
        << "source" << sourceDescription
        << "maximum zoom" << maximumZoom;
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
    if (!qIsFinite(zoomLevel) || zoomLevel < kMinZoom || zoomLevel > kProtocolMaxZoom) {
        return;
    }

    if (!_maximumZoomKnown) {
        _setZoomStatusKnown(false);
        return;
    }

    if (_absoluteZoomPending) {
        if (qAbs(zoomLevel - _requestedZoom) > kZoomComparisonTolerance) {
            // 运动中间值和迟到值都不能成为显示值或下一步的计算基准。
            _absoluteZoomMatchCount = 0;
            _scheduleZoomSync();
            return;
        }

        ++_absoluteZoomMatchCount;
        if (_absoluteZoomMatchCount < 2) {
            // 绝对目标也要求两次串行查询命中，不能让一份迟到重复包完成操作。
            _scheduleZoomSync();
            return;
        }

        if (!A8MiniZoomPolicy::isAlignedZoom(zoomLevel,
                                             zoomStep(),
                                             kMinZoom,
                                             _maximumZoom)) {
            // 用户可能在镜头运动期间修改zoomStep。旧目标即使已到达，也不能
            // 以新步长的非网格值解锁UI；立即转入唯一的新对齐目标。
            double alignedZoom = 0.0;
            _absoluteZoomPending = false;
            _absoluteZoomMatchCount = 0;
            if (!A8MiniZoomPolicy::alignmentTarget(zoomLevel,
                                                   zoomStep(),
                                                   kMinZoom,
                                                   _maximumZoom,
                                                   0,
                                                   &alignedZoom)) {
                _zoomSyncTimer.stop();
                _zoomOperationTimer.stop();
                _cancelOutstandingZoomQuery();
                _setZoomStatusKnown(false);
                _setLastError(tr("Failed to realign the SIYI camera after the zoom step changed."));
                return;
            }
            if (!_sendAbsoluteZoomTarget(alignedZoom) && _lastError.isEmpty()) {
                _setLastError(tr("Failed to send the realigned SIYI camera zoom step."));
            }
            return;
        }

        _finalizeConfirmedZoom(zoomLevel);
        return;
    }

    if (zoomLevel > _maximumZoom + 0.05) {
        // 拉流上限降低时镜头可能仍在更高倍率。先确认两次相同超限值，再命令
        // 回真实拉流上限；过程中始终显示unknown，不公开超限或运动中间值。
        if (!_stableZoomConfirmationPending
            || !_stableZoomCandidateValid
            || qAbs(zoomLevel - _stableZoomCandidate) > kZoomComparisonTolerance) {
            _stableZoomConfirmationPending = true;
            _stableZoomCandidate = zoomLevel;
            _stableZoomCandidateValid = true;
            _scheduleZoomSync();
            return;
        }

        double safeTargetZoom = 0.0;
        _clearStableZoomConfirmation();
        if (!A8MiniZoomPolicy::alignmentTarget(_maximumZoom,
                                               zoomStep(),
                                               kMinZoom,
                                               _maximumZoom,
                                               0,
                                               &safeTargetZoom)) {
            _setZoomStatusKnown(false);
            _setLastError(tr("Failed to restore the SIYI camera to a valid zoom step."));
            return;
        }
        if (!_sendAbsoluteZoomTarget(safeTargetZoom)) {
            if (_lastError.isEmpty()) {
                _setLastError(tr("Failed to send the SIYI camera zoom-limit correction."));
            }
            return;
        }
        qCWarning(GimbalControlLog)
            << "Confirmed SIYI zoom" << zoomLevel
            << "above pulled-video limit" << _maximumZoom
            << "- commanding the pulled-video maximum boundary"
            << safeTargetZoom;
        return;
    }

    bool stableValueConfirmed = false;
    if (_stableZoomConfirmationPending) {
        // 首次同步或发现外部任意倍率时要求两次一致，再吸附到固定分度或真实上限。
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
                if (!_sendAbsoluteZoomTarget(alignedZoom) && _lastError.isEmpty()) {
                    _setLastError(tr("Failed to send the aligned SIYI camera zoom step."));
                }
                return;
            }
        }
    }

    if (!stableValueConfirmed
        && (!_zoomStatusKnown
            || qAbs(zoomLevel - _currentZoom) > kZoomComparisonTolerance)) {
        // 空闲状态下的首次值或外部倍率变化同样要求两次独立查询一致。
        // 这样单个迟到/运动中回包不会直接替换显示值。
        _beginStableZoomConfirmation(true, 0);
        _stableZoomCandidate = zoomLevel;
        _stableZoomCandidateValid = true;
        _setZoomStatusKnown(false);
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
        _beginStableZoomConfirmation(true, 0);
        _stableZoomCandidate = zoomLevel;
        _stableZoomCandidateValid = true;
        _setZoomStatusKnown(false);
        if (!_zoomOperationTimer.isActive()) {
            _zoomOperationTimer.start();
        }
        _scheduleZoomSync();
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
    _absoluteZoomMatchCount = 0;
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
    _absoluteZoomMatchCount = 0;
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

    // 实际拉流尺寸是首选能力来源；在尺寸链尚未成功时持续查询设备当前
    // 支持范围，避免“视频正常但尺寸观察器未回调”永久阻断0x18和控件。
    if (!_pulledVideoResolutionConfirmed) {
        _configureSdkEndpoint();
        (void) _sdk->requestMaximumZoom();
    }

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
    _zoomSyncTimer.stop();
    _zoomOperationTimer.stop();
    _cancelOutstandingZoomQuery();
    _zoomResponseBlocked = false;
    _absoluteZoomPending = false;
    _absoluteZoomMatchCount = 0;
    _clearStableZoomConfirmation();
    _stopContinuousZoom();
    _requestedZoom = _currentZoom;
    _setZoomStatusKnown(false);
    _setLastError(tr("Timed out waiting for a confirmed SIYI camera zoom value."));
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
    if (!_continuousZoomActive
        || !enabled()
        || !_maximumZoomKnown
        || !_zoomStatusKnown
        || _absoluteZoomPending) {
        return;
    }

    double targetZoom = 0.0;
    if (!A8MiniZoomPolicy::stepTarget(_currentZoom,
                                      zoomStep(),
                                      kMinZoom,
                                      _maximumZoom,
                                      _continuousZoomDirection,
                                      &targetZoom)) {
        // 到达真实最小值或最大值属于正常结束；边界吸附已由stepTarget处理。
        _stopContinuousZoom();
        _setLastError(QString());
        return;
    }

    if (!_sendAbsoluteZoomTarget(targetZoom)) {
        _stopContinuousZoom();
    }
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

bool GimbalControlManager::_sendAbsoluteZoomTarget(double zoomLevel)
{
    if (!_sdk
        || !_maximumZoomKnown
        || _absoluteZoomPending
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
    _zoomOperationTimer.stop();
    _configureSdkEndpoint();
    if (!_sdk->sendAbsoluteZoom(targetZoom)) {
        return false;
    }

    // 全系统同一时刻只允许一个0x0f目标在途。ACK仅表示受理，最终状态必须
    // 等主动查询的0x18精确等于targetZoom；所有过程值保持unknown。
    _requestedZoom = targetZoom;
    _absoluteZoomPending = true;
    _absoluteZoomMatchCount = 0;
    _setZoomStatusKnown(false);
    _zoomOperationTimer.start();
    _sdkResponseTimer.start();
    _scheduleZoomSync();
    return true;
}

bool GimbalControlManager::_stopContinuousZoom()
{
    if (!_continuousZoomActive) {
        return true;
    }

    _continuousZoomWatchdog.stop();
    _continuousZoomStepTimer.stop();
    _setContinuousZoomState(false);
    return true;
}

void GimbalControlManager::_clearStableZoomConfirmation()
{
    _stableZoomConfirmationPending = false;
    _stableZoomCandidateValid = false;
    _stableZoomCandidate = kMinZoom;
    _normalizeAfterStableZoom = false;
    _stableZoomDirection = 0;
}

void GimbalControlManager::_beginStableZoomConfirmation(bool normalizeToStepGrid,
                                                        int direction)
{
    _clearStableZoomConfirmation();
    _stableZoomConfirmationPending = true;
    _normalizeAfterStableZoom = normalizeToStepGrid;
    _stableZoomDirection = qBound(-1, direction, 1);
}

void GimbalControlManager::_finalizeConfirmedZoom(double zoomLevel)
{
    const bool controlsWereWaiting = !_zoomStatusKnown;
    _zoomSyncTimer.stop();
    _zoomOperationTimer.stop();
    _cancelOutstandingZoomQuery();
    _zoomResponseBlocked = false;
    _absoluteZoomPending = false;
    _absoluteZoomMatchCount = 0;
    _clearStableZoomConfirmation();
    _requestedZoom = zoomLevel;
    _setCurrentZoom(zoomLevel);
    _setZoomStatusKnown(true);
    _setLastError(QString());

    if (controlsWereWaiting) {
        qCInfo(GimbalControlLog)
            << "Confirmed stable SIYI zoom" << zoomLevel
            << "- zoom controls are ready";
    }

    if (_continuousZoomActive) {
        _continuousZoomStepTimer.start();
    }
}

void GimbalControlManager::_resetMaximumZoomCapability()
{
    // 短暂SDK超时或RTSP重连保留最近确认的拉流能力；新的受支持协商
    // 尺寸会显式替换它，新的不支持尺寸则释放它并回到0x16后备。
    _setMaximumZoomKnown(_pulledVideoResolutionConfirmed);
    if (!_maximumZoomKnown) {
        _setMaximumZoom(kDefaultMaxZoom);
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
    if (!qIsFinite(zoomLevel) || zoomLevel < kMinZoom || zoomLevel > kProtocolMaxZoom) {
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
        emit maximumZoomChanged();
        emit zoomAvailabilityChanged();

        if (_zoomStatusKnown && _currentZoom > _maximumZoom + 0.05) {
            _absoluteZoomPending = false;
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
    if (_zoomStatusKnown != known) {
        _zoomStatusKnown = known;
        emit zoomStatusKnownChanged();
        emit zoomAvailabilityChanged();
    }
}

quint16 GimbalControlManager::_sdkPort() const
{
    const uint port = _settings ? _settings->sdkPort()->rawValue().toUInt() : 37260;
    return static_cast<quint16>(qBound(1u, port, 65535u));
}
