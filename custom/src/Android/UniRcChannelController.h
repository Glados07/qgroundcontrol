/****************************************************************************
 *
 * UniRC 10 Pro built-in SDK UART channel bridge.
 *
 ****************************************************************************/

#pragma once

#include "UniRcChannelPolicy.h"
#include "UniRcProtocol.h"

#include <QtCore/QElapsedTimer>
#include <QtCore/QLoggingCategory>
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QString>
#include <QtCore/QTimer>

class GimbalCenterCoordinator;
class GimbalControlManager;
class GimbalControlSettings;

#ifdef Q_OS_ANDROID
class QSocketNotifier;
#endif

Q_DECLARE_LOGGING_CATEGORY(UniRcChannelLog)

class UniRcChannelController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool serialOpen READ serialOpen NOTIFY serialOpenChanged)
    Q_PROPERTY(bool channelInputActive READ channelInputActive NOTIFY channelInputActiveChanged)
    Q_PROPERTY(int channel9 READ channel9 NOTIFY channelsChanged)
    Q_PROPERTY(int channel10 READ channel10 NOTIFY channelsChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    explicit UniRcChannelController(GimbalControlSettings *settings,
                                    GimbalControlManager *gimbalControlManager,
                                    GimbalCenterCoordinator *gimbalCenterCoordinator,
                                    QObject *parent = nullptr);
    ~UniRcChannelController() override;

    bool serialOpen() const { return _serialOpen; }
    bool channelInputActive() const { return _channelInputActive; }
    int channel9() const { return _channel9; }
    int channel10() const { return _channel10; }
    QString lastError() const { return _lastError; }

    Q_INVOKABLE void shutdown();

signals:
    void serialOpenChanged();
    void channelInputActiveChanged();
    void channelsChanged();
    void lastErrorChanged();

private slots:
    void _settingsChanged();
    void _applicationStateChanged(Qt::ApplicationState state);
    void _reconcile();
    void _readAvailable();
    void _inputWatchdogExpired();

private:
    bool _shouldRun() const;
    bool _openSerial();
    void _closeSerial(bool sendDisableRequest);
    bool _configureSerial(int fd, const QString &devicePath);
    bool _writeAll(const QByteArray &bytes, int timeoutMs);
    bool _sendChannelRequest(quint8 frequencyCode);
    void _scheduleSerialFailure(const QString &message);
    void _handleChannelPacket(const UniRcProtocol::DecodedPacket &packet);
    void _applyZoomDirection(int direction, bool directionChanged);
    void _tryStartZoom(int direction);
    void _resetInput(bool normalZoomStop);
    void _setSerialOpen(bool open);
    void _setChannelInputActive(bool active);
    void _setLastError(const QString &message);

    static constexpr quint8 kChannelFrequencyCode20Hz = 5;
    static constexpr int kInitialFrameTimeoutMs = 1200;
    static constexpr int kActiveFrameTimeoutMs = 350;
    static constexpr int kReconnectDelayMs = 3000;
    static constexpr int kZoomStartRetryMs = 250;
    static constexpr int kWriteTimeoutMs = 100;

    QPointer<GimbalControlSettings> _settings;
    QPointer<GimbalControlManager> _gimbalControlManager;
    QPointer<GimbalCenterCoordinator> _gimbalCenterCoordinator;
    UniRcProtocol::StreamParser _parser;
    UniRcChannelPolicy _channelPolicy;
    QTimer _reconnectTimer;
    QTimer _inputWatchdog;
    QElapsedTimer _zoomStartRetryElapsed;
    QString _openedDevicePath;
    QString _lastError;
    int _channel9 = 0;
    int _channel10 = 0;
    int _acceptedZoomDirection = 0;
    bool _serialOpen = false;
    bool _channelInputActive = false;
    bool _applicationActive = false;
    bool _serialFailureScheduled = false;
    bool _shuttingDown = false;

#ifdef Q_OS_ANDROID
    int _serialFd = -1;
    QSocketNotifier *_readNotifier = nullptr;
#endif
};
