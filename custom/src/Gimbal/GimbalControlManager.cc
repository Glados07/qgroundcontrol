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

    _sdkResponseTimer.setSingleShot(true);
    // 倍率和相机状态每 2 秒轮询一次；响应超时必须短于轮询周期。
    _sdkResponseTimer.setInterval(1500);
    _sdkPollTimer.setInterval(2000);
    _continuousZoomWatchdog.setSingleShot(true);
    _continuousZoomWatchdog.setInterval(15000);
    _zoomStopRetryTimer.setSingleShot(true);
    _zoomStopRetryTimer.setInterval(250);
    _photoFeedbackTimer.setSingleShot(true);
    _photoFeedbackTimer.setInterval(2000);
    _recordingStatusDelayTimer.setSingleShot(true);
    _recordingStatusDelayTimer.setInterval(400);
    _recordingCommandTimeoutTimer.setSingleShot(true);
    _recordingCommandTimeoutTimer.setInterval(2500);

    connect(_sdk, &SiyiSdk::currentZoomReceived, this, &GimbalControlManager::_handleCurrentZoom);
    connect(_sdk, &SiyiSdk::manualZoomReceived, this, &GimbalControlManager::_handleManualZoom);
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
    connect(&_sdkPollTimer, &QTimer::timeout, this, &GimbalControlManager::_pollSdk);
    connect(&_zoomStopRetryTimer, &QTimer::timeout, this, &GimbalControlManager::_advanceZoomStopSequence);
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
        _sendZoomStopTo(_continuousZoomHost, _continuousZoomPort);
    } else if (_zoomStopPending && _sdk) {
        _sendZoomStopTo(_zoomStopHost, _zoomStopPort);
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
    return qBound(0.1, _settings->zoomStep()->rawValue().toDouble(), kMaxZoom - kMinZoom);
}

bool GimbalControlManager::zoomIn()
{
    return setZoom(_currentZoom + zoomStep());
}

bool GimbalControlManager::zoomOut()
{
    return setZoom(_currentZoom - zoomStep());
}

bool GimbalControlManager::setZoom(double zoomLevel)
{
    if (!enabled()) {
        _setLastError(tr("SIYI gimbal zoom control is disabled."));
        return false;
    }

    // 绝对倍率也是一条新的镜头运动命令，不能让旧连续缩放序列的延迟 stop 在它之后到达。
    if (_continuousZoomActive) {
        _continuousZoomWatchdog.stop();
        _beginZoomStopSequence(_continuousZoomHost, _continuousZoomPort);
        _setContinuousZoomState(false);
    }
    if (_zoomStopPending) {
        _finishZoomStopSequenceBeforeStart();
    }

    _configureSdkEndpoint();
    const double targetZoom = _clampZoom(zoomLevel);
    if (!_sdk->sendAbsoluteZoom(targetZoom)) {
        return false;
    }

    // 先乐观更新 UI，再请求相机返回真实倍率，避免按键后界面没有反馈。
    _setCurrentZoom(targetZoom);
    _sdkResponseTimer.start();
    _sdk->requestCurrentZoom();
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

    if ((normalizedDirection > 0 && _currentZoom >= kMaxZoom - 0.05) ||
        (normalizedDirection < 0 && _currentZoom <= kMinZoom + 0.05)) {
        return false;
    }

    if (_continuousZoomActive && _continuousZoomDirection == normalizedDirection) {
        _continuousZoomWatchdog.start();
        return true;
    }

    if (_continuousZoomActive) {
        _beginZoomStopSequence(_continuousZoomHost, _continuousZoomPort);
        _setContinuousZoomState(false);
    }
    if (_zoomStopPending) {
        // 新一轮连续缩放开始前，用最后一组停止包结束旧端点的有限停机序列。
        _finishZoomStopSequenceBeforeStart();
    }

    _configureSdkEndpoint();
    const QString host = _sdkHost();
    const quint16 port = _sdkPort();
    if (!_sdk->sendManualZoomTo(static_cast<qint8>(normalizedDirection), host, port)) {
        return false;
    }

    _continuousZoomHost = host;
    _continuousZoomPort = port;
    _setContinuousZoomState(true, normalizedDirection);
    _continuousZoomWatchdog.start();
    _setLastError(QString());
    return true;
}

bool GimbalControlManager::stopZoom()
{
    _continuousZoomWatchdog.stop();
    const bool wasActive = _continuousZoomActive;
    bool sent = true;
    if (wasActive) {
        sent = _beginZoomStopSequence(_continuousZoomHost, _continuousZoomPort);
        _setContinuousZoomState(false);
    } else if (!_zoomStopPending) {
        // 允许界面重复调用 stop；没有活动序列时仅向当前端点补发一组幂等停止包。
        _configureSdkEndpoint();
        sent = _sendZoomStopTo(_sdkHost(), _sdkPort());
    }
    if (sent && enabled() && wasActive) {
        _sdkResponseTimer.start();
        _sdk->requestCurrentZoom();
    }
    return sent;
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
    if (!enabled()) {
        return false;
    }

    _configureSdkEndpoint();
    _sdkResponseTimer.start();
    return _sdk->requestCurrentZoom();
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
    // Fact 已经变更，但活动连续缩放保存了启动时的 endpoint；先在那里启动有限停机序列。
    if (_continuousZoomActive) {
        _beginZoomStopSequence(_continuousZoomHost, _continuousZoomPort);
        _setContinuousZoomState(false);
    }

    _continuousZoomWatchdog.stop();
    _photoFeedbackTimer.stop();
    _photoCommandPending = false;
    _finishRecordingCommand();
    _sdkResponseTimer.stop();
    _sdkPollTimer.stop();
    _setSdkResponding(false);
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

void GimbalControlManager::_handleCurrentZoom(double zoomLevel)
{
    _sdkResponseTimer.stop();
    _setSdkResponding(true);
    _setCurrentZoom(zoomLevel);
}

void GimbalControlManager::_handleManualZoom(double zoomLevel)
{
    _handleCurrentZoom(zoomLevel);
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
    _sdkResponseTimer.stop();
    _photoFeedbackTimer.stop();
    _photoCommandPending = false;
    _finishRecordingCommand();
    _setSdkResponding(false);
    _setCameraStatusKnown(false);
    _setRecording(false);
    _setLastError(message);
}

void GimbalControlManager::_markSdkNotResponding()
{
    if (_continuousZoomActive) {
        _beginZoomStopSequence(_continuousZoomHost, _continuousZoomPort);
        _setContinuousZoomState(false);
    }
    _continuousZoomWatchdog.stop();
    _photoFeedbackTimer.stop();
    _photoCommandPending = false;
    _finishRecordingCommand();
    _setSdkResponding(false);
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
    const bool zoomRequestSent = _sdk->requestCurrentZoom();
    const bool cameraStatusRequestSent = _sdk->requestCameraSystemStatus();
    if (zoomRequestSent || cameraStatusRequestSent) {
        _sdkResponseTimer.start();
    }
}

void GimbalControlManager::_advanceZoomStopSequence()
{
    if (!_zoomStopPending) {
        return;
    }

    _sendZoomStopTo(_zoomStopHost, _zoomStopPort);
    if (_zoomStopRetryStage == 0) {
        // 首次重发位于约 250ms；第二次再等待 1750ms，使总时长约为 2 秒。
        _zoomStopRetryStage = 1;
        _zoomStopRetryTimer.setInterval(1750);
        _zoomStopRetryTimer.start();
    } else {
        _clearZoomStopSequence();
    }
}

void GimbalControlManager::_stopContinuousZoomForSafety()
{
    if (!_continuousZoomActive) {
        return;
    }

    _beginZoomStopSequence(_continuousZoomHost, _continuousZoomPort);
    _setContinuousZoomState(false);
    _setLastError(tr("Continuous zoom was stopped by the safety timeout."));
    if (enabled()) {
        _sdkResponseTimer.start();
        _sdk->requestCurrentZoom();
    }
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

bool GimbalControlManager::_beginZoomStopSequence(const QString& host, quint16 port)
{
    if (_zoomStopPending) {
        return true;
    }

    _zoomStopHost = host;
    _zoomStopPort = port;
    _zoomStopPending = true;
    _zoomStopRetryStage = 0;

    // 停止命令幂等：立即双发，约 250ms 双发，累计约 2 秒再双发后结束。
    const bool sent = _sendZoomStopTo(_zoomStopHost, _zoomStopPort);
    _zoomStopRetryTimer.setInterval(250);
    _zoomStopRetryTimer.start();
    return sent;
}

void GimbalControlManager::_finishZoomStopSequenceBeforeStart()
{
    if (!_zoomStopPending) {
        return;
    }

    // 不能让旧序列的延迟 stop 落在新 start 之后；先补发最后一组，再取消旧定时序列。
    _zoomStopRetryTimer.stop();
    _sendZoomStopTo(_zoomStopHost, _zoomStopPort);
    _clearZoomStopSequence();
}

void GimbalControlManager::_clearZoomStopSequence()
{
    _zoomStopRetryTimer.stop();
    _zoomStopPending = false;
    _zoomStopRetryStage = 0;
    _zoomStopHost.clear();
    _zoomStopPort = 0;
}

void GimbalControlManager::_setCurrentZoom(double zoomLevel)
{
    const double clampedZoom = _clampZoom(zoomLevel);
    if (!qFuzzyCompare(_currentZoom + 1.0, clampedZoom + 1.0)) {
        _currentZoom = clampedZoom;
        emit currentZoomChanged();
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
    if (!_sdkResponding) {
        _setLastError(tr("The SIYI gimbal camera is not connected."));
        return false;
    }
    return true;
}

bool GimbalControlManager::_sendZoomStopTo(const QString& host, quint16 port)
{
    if (!_sdk) {
        return false;
    }

    const bool firstSent = _sdk->sendManualZoomTo(0, host, port);
    const bool secondSent = _sdk->sendManualZoomTo(0, host, port);
    return firstSent || secondSent;
}

QString GimbalControlManager::_sdkHost() const
{
    return _settings ? _settings->sdkHost()->rawValue().toString().trimmed() : QString();
}

double GimbalControlManager::_clampZoom(double zoomLevel) const
{
    return qBound(kMinZoom, zoomLevel, kMaxZoom);
}

quint16 GimbalControlManager::_sdkPort() const
{
    const uint port = _settings ? _settings->sdkPort()->rawValue().toUInt() : 37260;
    return static_cast<quint16>(qBound(1u, port, 65535u));
}
