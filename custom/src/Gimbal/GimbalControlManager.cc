/****************************************************************************
 *
 * 思翼云台相机控制管理器。
 *
 ****************************************************************************/

#include "GimbalControlManager.h"

#include "GimbalControlSettings.h"
#include "SiyiSdk.h"
#include "Fact.h"

#include <QtCore/QtMath>

GimbalControlManager::GimbalControlManager(GimbalControlSettings* settings, QObject* parent)
    : QObject(parent)
    , _settings(settings)
    , _sdk(new SiyiSdk(this))
{
    Q_CHECK_PTR(_settings);
    _sdk->setZoomRange(kMinZoom, kProtocolMaxZoom);

    _sdkResponseTimer.setSingleShot(true);
    // 倍率和相机状态每 2 秒轮询一次；响应超时必须短于轮询周期。
    _sdkResponseTimer.setInterval(1500);
    _zoomQueryResponseTimer.setSingleShot(true);
    _zoomQueryResponseTimer.setInterval(1500);
    _sdkPollTimer.setInterval(2000);
    _continuousZoomWatchdog.setSingleShot(true);
    // A8 Mini连续数字变倍在部分固件上较慢；60秒仍能兜底丢失的释放事件，
    // 同时不会在到达5.5x/6.0x前于约3.5x处提前截停。
    _continuousZoomWatchdog.setInterval(60000);
    _zoomSyncTimer.setSingleShot(true);
    _zoomSyncTimer.setInterval(350);
    _photoFeedbackTimer.setSingleShot(true);
    _photoFeedbackTimer.setInterval(2000);
    _recordingStatusDelayTimer.setSingleShot(true);
    _recordingStatusDelayTimer.setInterval(400);
    _recordingCommandTimeoutTimer.setSingleShot(true);
    _recordingCommandTimeoutTimer.setInterval(2500);

    connect(_sdk, &SiyiSdk::maximumZoomReceived, this, &GimbalControlManager::_handleMaximumZoom);
    connect(_sdk, &SiyiSdk::currentZoomReceived, this, &GimbalControlManager::_handleCurrentZoom);
    connect(_sdk, &SiyiSdk::cameraSystemStatusReceived, this, &GimbalControlManager::_handleCameraSystemStatus);
    connect(_sdk, &SiyiSdk::functionFeedbackReceived, this, &GimbalControlManager::_handleFunctionFeedback);
    connect(_sdk, &SiyiSdk::packetReceived, this, [this]() {
        const bool wasResponding = _sdkResponding;
        _sdkResponseTimer.stop();
        _setSdkResponding(true);
        if (!wasResponding) {
            _setLastError(QString());
        }
    });
    connect(_sdk, &SiyiSdk::communicationError, this, &GimbalControlManager::_handleCommunicationError);
    connect(&_sdkResponseTimer, &QTimer::timeout, this, &GimbalControlManager::_markSdkNotResponding);
    connect(&_zoomQueryResponseTimer, &QTimer::timeout, this, &GimbalControlManager::_markZoomStatusUnknown);
    connect(&_sdkPollTimer, &QTimer::timeout, this, &GimbalControlManager::_pollSdk);
    connect(&_zoomSyncTimer, &QTimer::timeout, this, &GimbalControlManager::_requestZoomAfterSettle);
    connect(&_continuousZoomWatchdog, &QTimer::timeout, this, &GimbalControlManager::_stopContinuousZoomForSafety);
    connect(&_photoFeedbackTimer, &QTimer::timeout, this, [this]() { _photoCommandPending = false; });
    connect(&_recordingStatusDelayTimer, &QTimer::timeout, this, &GimbalControlManager::_requestRecordingStatusAfterDelay);
    connect(&_recordingCommandTimeoutTimer, &QTimer::timeout, this, &GimbalControlManager::_handleRecordingCommandTimeout);

    connect(_settings->enabled(), &Fact::rawValueChanged, this, &GimbalControlManager::_settingsChanged);
    connect(_settings->sdkHost(), &Fact::rawValueChanged, this, &GimbalControlManager::_settingsChanged);
    connect(_settings->sdkPort(), &Fact::rawValueChanged, this, &GimbalControlManager::_settingsChanged);
    connect(_settings->zoomStep(), &Fact::rawValueChanged, this, [this](const QVariant&) { emit zoomStepChanged(); });

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
    const double availableRange = qMax(0.1, _maximumZoom - kMinZoom);
    return qBound(0.1, _settings->zoomStep()->rawValue().toDouble(), availableRange);
}

bool GimbalControlManager::zoomIn()
{
    if (!_zoomStatusKnown) {
        _setLastError(tr("Waiting for the SIYI camera zoom value."));
        requestCurrentZoom();
        return false;
    }

    const double baseZoom = _absoluteZoomPending ? _requestedZoom : _currentZoom;
    return setZoom(baseZoom + zoomStep());
}

bool GimbalControlManager::zoomOut()
{
    if (!_zoomStatusKnown) {
        _setLastError(tr("Waiting for the SIYI camera zoom value."));
        requestCurrentZoom();
        return false;
    }

    const double baseZoom = _absoluteZoomPending ? _requestedZoom : _currentZoom;
    return setZoom(baseZoom - zoomStep());
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
    if (!qIsFinite(zoomLevel)) {
        _setLastError(tr("Invalid SIYI camera zoom value."));
        return false;
    }

    // 绝对倍率是新的镜头运动命令。若此前正在连续缩放，先同步发送一次停止，
    // 再发送绝对倍率；不存在任何会落到新命令之后的延迟 stop。
    if (_continuousZoomActive && !_stopContinuousZoom(false)) {
        return false;
    }
    _clearStableZoomConfirmation();

    _configureSdkEndpoint();
    // 协议只传一位小数；Manager使用同一个量化目标，避免UI/requested值与
    // 实际0x0f payload不一致而永远无法确认。
    const double targetZoom = qRound(_clampZoom(zoomLevel) * 10.0) / 10.0;
    if (!_sdk->sendAbsoluteZoom(targetZoom)) {
        return false;
    }

    // 快速连续短按必须基于最新请求目标累计，不能被在途的旧 0x18 回包回退。
    _requestedZoom = targetZoom;
    _absoluteZoomPending = true;
    _zoomQueryResponseTimer.stop();
    _setCurrentZoom(targetZoom);
    _sdkResponseTimer.start();
    _scheduleZoomSync();
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
        _setLastError(tr("Continuous zoom direction must be -1 or 1."));
        return false;
    }

    if (_zoomStatusKnown &&
        ((normalizedDirection > 0 && _currentZoom >= _maximumZoom - 0.05) ||
         (normalizedDirection < 0 && _currentZoom <= kMinZoom + 0.05))) {
        return false;
    }

    if (_continuousZoomActive && _continuousZoomDirection == normalizedDirection) {
        _continuousZoomWatchdog.start();
        return true;
    }

    if (_continuousZoomActive && !_stopContinuousZoom(false)) {
        return false;
    }

    // 连续缩放取代尚未确认的绝对目标。连续期间忽略 0x18 和 0x05 的旧倍率，
    // 释放后再由一次延迟 0x18 查询统一校正。
    _zoomSyncTimer.stop();
    _zoomResponseBlocked = false;
    _absoluteZoomPending = false;
    _clearStableZoomConfirmation();
    _requestedZoom = _currentZoom;
    _configureSdkEndpoint();
    const QString host = _sdkHost();
    const quint16 port = _sdkPort();
    if (!_sdk->sendManualZoomTo(static_cast<qint8>(normalizedDirection), host, port)) {
        return false;
    }

    _continuousZoomHost = host;
    _continuousZoomPort = port;
    _zoomQueryResponseTimer.stop();
    _setZoomStatusKnown(false);
    _setContinuousZoomState(true, normalizedDirection);
    _continuousZoomWatchdog.start();
    _setLastError(QString());
    return true;
}

bool GimbalControlManager::stopZoom()
{
    // 严格幂等：Idle 状态不向网络发送无主停止包。
    return _stopContinuousZoom(true);
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
    if (!enabled() || _continuousZoomActive || _zoomResponseBlocked) {
        return false;
    }

    _configureSdkEndpoint();
    const bool sent = _sdk->requestCurrentZoom();
    if (sent) {
        _sdkResponseTimer.start();
        _zoomQueryResponseTimer.start();
    }
    return sent;
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
    // Fact 已经变更，但活动连续缩放保存了启动时的 endpoint；先在那里停止。
    if (_continuousZoomActive) {
        _stopContinuousZoom(false);
    }

    _continuousZoomWatchdog.stop();
    _zoomSyncTimer.stop();
    _zoomQueryResponseTimer.stop();
    _zoomResponseBlocked = false;
    _absoluteZoomPending = false;
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
    }

    if (nowEnabled) {
        _sdkPollTimer.start();
        _pollSdk();
    }
}

void GimbalControlManager::_handleMaximumZoom(double zoomLevel)
{
    if (!qIsFinite(zoomLevel) || zoomLevel < kMinZoom || zoomLevel > kProtocolMaxZoom) {
        return;
    }

    const bool capabilityWasKnown = _maximumZoomKnown;
    const double previousMaximumZoom = _maximumZoom;
    _setMaximumZoom(zoomLevel);
    _maximumZoomKnown = true;
    const bool capabilityChanged = qAbs(previousMaximumZoom - _maximumZoom) > 0.051;

    if (_absoluteZoomPending && _requestedZoom > _maximumZoom + 0.05) {
        _absoluteZoomPending = false;
        _requestedZoom = _currentZoom;
        _setZoomStatusKnown(false);
    }

    // 启动/重连后必须先取得0x16能力值，再用一条随后发出的0x18确认当前倍率。
    // 不能让同轮中先到达的旧0x18按默认5.5x边界提前解锁短按命令。
    if ((!capabilityWasKnown || capabilityChanged) && enabled()) {
        // 当前模式上限首次确认或发生变化时，丢弃同轮在途0x18的计时状态，
        // 并要求随后两份稳定样本一致，确保能力值与当前倍率来自同一模式。
        _stableZoomConfirmationPending = true;
        _stableZoomCandidateValid = false;
        _stableZoomCandidate = kMinZoom;
        _setZoomStatusKnown(false);
        _zoomQueryResponseTimer.stop();
        if (!_continuousZoomActive && !_zoomResponseBlocked) {
            _scheduleZoomSync();
        }
    } else if (!_zoomStatusKnown
               && enabled()
               && !_continuousZoomActive
               && !_zoomResponseBlocked
               && !_zoomQueryResponseTimer.isActive()) {
        _scheduleZoomSync();
    }
}

void GimbalControlManager::_handleCurrentZoom(double zoomLevel)
{
    _sdkResponseTimer.stop();
    _setSdkResponding(true);

    // 运动命令稳定前的 0x18，以及连续缩放期间的在途响应都可能代表旧倍率。
    if (_zoomResponseBlocked || _continuousZoomActive) {
        return;
    }
    if (!qIsFinite(zoomLevel) || zoomLevel < kMinZoom || zoomLevel > kProtocolMaxZoom) {
        return;
    }

    if (!_maximumZoomKnown) {
        // 缓存显示值，但在0x16到达前保持unknown；否则2K/4K可能在短暂窗口内
        // 按默认5.5x边界发送越界的0x0f命令。
        _absoluteZoomPending = false;
        _requestedZoom = zoomLevel;
        _zoomQueryResponseTimer.stop();
        _setCurrentZoom(zoomLevel);
        _setZoomStatusKnown(false);
        return;
    }
    if (zoomLevel > _maximumZoom + 0.05) {
        // 已取得0x16能力值后，超出当前模式上限的0x18只能是迟到旧包。
        _scheduleZoomSync();
        return;
    }

    if (_stableZoomConfirmationPending) {
        // 连续停止、首次能力确认或能力变化后的首个0x18都可能是迟到值。
        // 要求两次连续采样一致，不把2K真实3.5x硬改成5.5x，也不接受单个旧包。
        if (!_stableZoomCandidateValid || qAbs(zoomLevel - _stableZoomCandidate) > 0.051) {
            _stableZoomCandidate = zoomLevel;
            _stableZoomCandidateValid = true;
            _scheduleZoomSync();
            return;
        }
        _clearStableZoomConfirmation();
    }

    if (_absoluteZoomPending && qAbs(zoomLevel - _requestedZoom) > 0.051) {
        // 相机尚在运动，或这是命令前旧查询的迟到回包。保留requested基准，
        // 继续在专用总超时范围内查询，不能用中间值破坏快速短按累计。
        _scheduleZoomSync();
        return;
    }

    _absoluteZoomPending = false;
    _requestedZoom = zoomLevel;
    _zoomQueryResponseTimer.stop();
    _setCurrentZoom(zoomLevel);
    _setZoomStatusKnown(true);
}

void GimbalControlManager::_handleCameraSystemStatus(quint8 hdrStatus,
                                                     quint8 recordingStatus,
                                                     quint8 gimbalMotionMode,
                                                     quint8 gimbalMountingDirection,
                                                     quint8 videoOutputStatus,
                                                     quint8 zoomLinkage)
{
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
        // start可能已经被相机接收；即使另一条UDP写入失败，也必须向启动时
        // endpoint尽力停止。helper先置Idle再发送，可避免失败信号递归stop。
        _stopContinuousZoom(false);
    }
    _sdkResponseTimer.stop();
    _zoomSyncTimer.stop();
    _zoomQueryResponseTimer.stop();
    _zoomResponseBlocked = false;
    _absoluteZoomPending = false;
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
        _stopContinuousZoom(false);
    }
    _continuousZoomWatchdog.stop();
    _zoomSyncTimer.stop();
    _zoomQueryResponseTimer.stop();
    _zoomResponseBlocked = false;
    _absoluteZoomPending = false;
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

    _configureSdkEndpoint();
    // 已有0x18等待响应时不再由周期轮询重复发送、重启专用超时；绝对缩放
    // 的确认重试只由_zoomSyncTimer负责，保证总超时不会被后台轮询无限延长。
    const bool maximumZoomRequestSent = _sdk->requestMaximumZoom();
    const bool zoomRequestSent = !_continuousZoomActive
        && !_zoomResponseBlocked
        && !_zoomQueryResponseTimer.isActive()
        ? _sdk->requestCurrentZoom()
        : false;
    const bool cameraStatusRequestSent = _sdk->requestCameraSystemStatus();
    if (zoomRequestSent) {
        _zoomQueryResponseTimer.start();
    }
    if (zoomRequestSent || maximumZoomRequestSent || cameraStatusRequestSent) {
        _sdkResponseTimer.start();
    }
}

void GimbalControlManager::_markZoomStatusUnknown()
{
    _zoomSyncTimer.stop();
    _zoomResponseBlocked = false;
    _absoluteZoomPending = false;
    if (_stableZoomConfirmationPending) {
        // 总超时只丢弃本轮候选，不允许下一轮周期0x18退化成单样本确认。
        _stableZoomCandidateValid = false;
        _stableZoomCandidate = kMinZoom;
    } else {
        _clearStableZoomConfirmation();
    }
    _requestedZoom = _currentZoom;
    _setZoomStatusKnown(false);
}

void GimbalControlManager::_requestZoomAfterSettle()
{
    _zoomResponseBlocked = false;
    if (!enabled() || _continuousZoomActive) {
        return;
    }

    _configureSdkEndpoint();
    if (_sdk->requestCurrentZoom()) {
        _sdkResponseTimer.start();
        if (!_zoomQueryResponseTimer.isActive()) {
            _zoomQueryResponseTimer.start();
        }
    }
}

void GimbalControlManager::_stopContinuousZoomForSafety()
{
    if (!_continuousZoomActive) {
        return;
    }

    _stopContinuousZoom(true);
    _setLastError(tr("Continuous zoom was stopped by the safety timeout."));
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

bool GimbalControlManager::_stopContinuousZoom(bool scheduleZoomSync)
{
    if (!_continuousZoomActive) {
        return true;
    }

    const QString host = _continuousZoomHost;
    const quint16 port = _continuousZoomPort;
    _continuousZoomWatchdog.stop();
    // 先切换为 Idle，避免 writeDatagram 失败触发 communicationError 时重入 stop。
    _setContinuousZoomState(false);
    _clearStableZoomConfirmation();
    const bool sent = _sendZoomStopTo(host, port);
    if (sent && scheduleZoomSync && enabled()) {
        _stableZoomConfirmationPending = true;
        _sdkResponseTimer.start();
        _scheduleZoomSync();
    }
    return sent;
}

void GimbalControlManager::_clearStableZoomConfirmation()
{
    _stableZoomConfirmationPending = false;
    _stableZoomCandidateValid = false;
    _stableZoomCandidate = kMinZoom;
}

void GimbalControlManager::_resetMaximumZoomCapability()
{
    _maximumZoomKnown = false;
    _setMaximumZoom(kDefaultMaxZoom);
}

void GimbalControlManager::_scheduleZoomSync()
{
    _zoomResponseBlocked = true;
    _zoomSyncTimer.start();
}

void GimbalControlManager::_setCurrentZoom(double zoomLevel)
{
    // 这里只接收已经校验过的设备值或已经钳制过的本地命令目标；绝不把
    // 非法设备回包伪装成边界值。
    if (!qIsFinite(zoomLevel) || zoomLevel < kMinZoom || zoomLevel > kProtocolMaxZoom) {
        return;
    }
    if (!qFuzzyCompare(_currentZoom + 1.0, zoomLevel + 1.0)) {
        _currentZoom = zoomLevel;
        emit currentZoomChanged();
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
        emit zoomStepChanged();

        if (_zoomStatusKnown && _currentZoom > _maximumZoom + 0.05) {
            _absoluteZoomPending = false;
            _requestedZoom = _currentZoom;
            _setZoomStatusKnown(false);
        }
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
    if (!active) {
        _continuousZoomHost.clear();
        _continuousZoomPort = 0;
    }
    if (activeChanged) {
        emit continuousZoomActiveChanged();
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
    }
}

bool GimbalControlManager::_sendZoomStopTo(const QString& host, quint16 port)
{
    if (!_sdk) {
        return false;
    }

    // UDP没有传输确认。两个停止包在同一调用内紧邻发送，不使用任何延迟Timer；
    // 因而可降低单包丢失风险，也不可能落到随后的新缩放命令之后。
    const bool firstSent = _sdk->sendManualZoomTo(0, host, port);
    const bool secondSent = _sdk->sendManualZoomTo(0, host, port);
    // 任一写入失败都会同步触发communicationError。只有两包均写入成功时，
    // 外层才可重新建立倍率确认或继续发送新的缩放命令。
    return firstSent && secondSent;
}

QString GimbalControlManager::_sdkHost() const
{
    return _settings ? _settings->sdkHost()->rawValue().toString().trimmed() : QString();
}

double GimbalControlManager::_clampZoom(double zoomLevel) const
{
    return qBound(kMinZoom, zoomLevel, _maximumZoom);
}

quint16 GimbalControlManager::_sdkPort() const
{
    const uint port = _settings ? _settings->sdkPort()->rawValue().toUInt() : 37260;
    return static_cast<quint16>(qBound(1u, port, 65535u));
}
