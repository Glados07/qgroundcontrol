/****************************************************************************
 *
 * 思翼云台缩放控制管理器。
 * QML 只调用该类的 zoomIn/zoomOut；协议封包和 UDP 发送由 SiyiSdk/SiyiProtocol 处理。
 *
 ****************************************************************************/

#pragma once

#include <QtCore/QObject>
#include <QtCore/QTimer>

class Fact;
class GimbalControlSettings;
class SiyiSdk;

class GimbalControlManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled NOTIFY enabledChanged)
    Q_PROPERTY(double currentZoom READ currentZoom NOTIFY currentZoomChanged)
    Q_PROPERTY(double zoomStep READ zoomStep NOTIFY zoomStepChanged)
    Q_PROPERTY(double minimumZoom READ minimumZoom CONSTANT)
    Q_PROPERTY(double maximumZoom READ maximumZoom CONSTANT)
    Q_PROPERTY(bool sdkResponding READ sdkResponding NOTIFY sdkRespondingChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    explicit GimbalControlManager(GimbalControlSettings* settings, QObject* parent = nullptr);
    ~GimbalControlManager() override;

    bool enabled() const;
    double currentZoom() const { return _currentZoom; }
    double zoomStep() const;
    double minimumZoom() const { return kMinZoom; }
    double maximumZoom() const { return kMaxZoom; }
    bool sdkResponding() const { return _sdkResponding; }
    QString lastError() const { return _lastError; }

    Q_INVOKABLE bool zoomIn();
    Q_INVOKABLE bool zoomOut();
    Q_INVOKABLE bool setZoom(double zoomLevel);
    Q_INVOKABLE bool requestCurrentZoom();

signals:
    void enabledChanged();
    void currentZoomChanged();
    void zoomStepChanged();
    void sdkRespondingChanged();
    void lastErrorChanged();

private slots:
    void _settingsChanged();
    void _handleCurrentZoom(double zoomLevel);
    void _handleCommunicationError(const QString& message);
    void _markSdkNotResponding();

private:
    void _configureSdkEndpoint();
    void _setCurrentZoom(double zoomLevel);
    void _setSdkResponding(bool responding);
    void _setLastError(const QString& message);
    double _clampZoom(double zoomLevel) const;
    quint16 _sdkPort() const;

    static constexpr double kMinZoom = 1.0;
    static constexpr double kMaxZoom = 5.5;

    GimbalControlSettings* _settings = nullptr;
    SiyiSdk* _sdk = nullptr;
    QTimer _sdkResponseTimer;
    double _currentZoom = kMinZoom;
    bool _sdkResponding = false;
    QString _lastError;
};
