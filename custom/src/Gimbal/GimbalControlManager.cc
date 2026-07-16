/****************************************************************************
 *
 * 思翼云台缩放控制管理器。
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
    // 当前倍率每 2 秒轮询一次，响应超时必须短于轮询周期，避免计时器被连续重启而永不超时。
    _sdkResponseTimer.setInterval(1500);

    connect(_sdk, &SiyiSdk::currentZoomReceived, this, &GimbalControlManager::_handleCurrentZoom);
    connect(_sdk, &SiyiSdk::packetReceived, this, [this]() { _setSdkResponding(true); });
    connect(_sdk, &SiyiSdk::communicationError, this, &GimbalControlManager::_handleCommunicationError);
    connect(&_sdkResponseTimer, &QTimer::timeout, this, &GimbalControlManager::_markSdkNotResponding);

    connect(_settings->enabled(), &Fact::rawValueChanged, this, &GimbalControlManager::_settingsChanged);
    connect(_settings->sdkHost(), &Fact::rawValueChanged, this, &GimbalControlManager::_settingsChanged);
    connect(_settings->sdkPort(), &Fact::rawValueChanged, this, &GimbalControlManager::_settingsChanged);
    connect(_settings->zoomStep(), &Fact::rawValueChanged, this, [this](const QVariant&) { emit zoomStepChanged(); });

    _configureSdkEndpoint();
}

GimbalControlManager::~GimbalControlManager() = default;

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

bool GimbalControlManager::requestCurrentZoom()
{
    if (!enabled()) {
        return false;
    }

    _configureSdkEndpoint();
    _sdkResponseTimer.start();
    return _sdk->requestCurrentZoom();
}

void GimbalControlManager::_settingsChanged()
{
    _sdkResponseTimer.stop();
    _setSdkResponding(false);
    _setLastError(QString());
    _configureSdkEndpoint();
    emit enabledChanged();
    if (enabled()) {
        requestCurrentZoom();
    } else {
        _sdkResponseTimer.stop();
        _setSdkResponding(false);
    }
}

void GimbalControlManager::_handleCurrentZoom(double zoomLevel)
{
    _sdkResponseTimer.stop();
    _setSdkResponding(true);
    _setCurrentZoom(zoomLevel);
    _setLastError(QString());
}

void GimbalControlManager::_handleCommunicationError(const QString& message)
{
    _sdkResponseTimer.stop();
    _setSdkResponding(false);
    _setLastError(message);
}

void GimbalControlManager::_markSdkNotResponding()
{
    _setSdkResponding(false);
    _setLastError(tr("No response from the SIYI SDK endpoint."));
}

void GimbalControlManager::_configureSdkEndpoint()
{
    if (!_settings || !_sdk) {
        return;
    }

    _sdk->setEndpoint(_settings->sdkHost()->rawValue().toString().trimmed(), _sdkPort());
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

void GimbalControlManager::_setLastError(const QString& message)
{
    if (_lastError != message) {
        _lastError = message;
        emit lastErrorChanged();
    }
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
