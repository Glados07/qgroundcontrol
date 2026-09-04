/****************************************************************************
 *
 * UniRC 10 Pro Bluetooth SDK channel bridge.
 *
 ****************************************************************************/

#pragma once

#include "UniRcChannelPolicy.h"
#include "UniRcProtocol.h"

#include <QtBluetooth/QBluetoothSocket>
#include <QtCore/QByteArray>
#include <QtCore/QElapsedTimer>
#include <QtCore/QLoggingCategory>
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QString>
#include <QtCore/QTimer>
#include <QtCore/QVariantList>

class GimbalCenterCoordinator;
class GimbalControlManager;
class GimbalControlSettings;

Q_DECLARE_LOGGING_CATEGORY(UniRcChannelLog)

class UniRcChannelController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool bluetoothConnected READ bluetoothConnected NOTIFY bluetoothConnectedChanged)
    Q_PROPERTY(bool sdkRouteActive READ sdkRouteActive NOTIFY sdkRouteActiveChanged)
    Q_PROPERTY(bool channelInputActive READ channelInputActive NOTIFY channelInputActiveChanged)
    Q_PROPERTY(QVariantList channelValues READ channelValues NOTIFY channelsChanged)
    Q_PROPERTY(int channel9 READ channel9 NOTIFY channelsChanged)
    Q_PROPERTY(int channel10 READ channel10 NOTIFY channelsChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(QString diagnosticStage READ diagnosticStage NOTIFY diagnosticsChanged)
    Q_PROPERTY(QString diagnosticSummary READ diagnosticSummary NOTIFY diagnosticsChanged)

public:
    explicit UniRcChannelController(GimbalControlSettings *settings,
                                    GimbalControlManager *gimbalControlManager,
                                    GimbalCenterCoordinator *gimbalCenterCoordinator,
                                    QObject *parent = nullptr);
    ~UniRcChannelController() override;

    bool bluetoothConnected() const { return _bluetoothConnected; }
    bool sdkRouteActive() const { return _sdkRouteActive; }
    bool channelInputActive() const { return _channelInputActive; }
    QVariantList channelValues() const { return _channelValues; }
    int channel9() const { return _channelValues.value(8).toInt(); }
    int channel10() const { return _channelValues.value(9).toInt(); }
    QString lastError() const { return _lastError; }
    QString diagnosticStage() const { return _diagnosticStage; }
    QString diagnosticSummary() const;

    Q_INVOKABLE void shutdown();

signals:
    void bluetoothConnectedChanged();
    void sdkRouteActiveChanged();
    void channelInputActiveChanged();
    void channelsChanged();
    void lastErrorChanged();
    void diagnosticsChanged();

private slots:
    void _settingsChanged();
    void _zoomDirectionSettingChanged();
    void _applicationStateChanged(Qt::ApplicationState state);
    void _reconcile();
    void _connectionTimeoutExpired();
    void _inputWatchdogExpired();
    void _socketConnected();
    void _socketDisconnected();
    void _socketReadyRead();
    void _socketBytesWritten(qint64 bytes);
    void _socketError(QBluetoothSocket::SocketError error);

private:
    bool _shouldRun() const;
    bool _ensureBluetoothPermission();
    bool _ensureBluetoothPoweredOn();
    void _connectBluetooth();
    void _closeBluetooth(bool sendDisableRequest, const char *reason);
    bool _sendChannelRequest(quint8 frequencyCode);
    void _readAvailableBluetoothData();
    void _markChannelRequestTransmitted(const char *evidence);
    void _handleChannelPacket(const UniRcProtocol::DecodedPacket &packet);
    void _scheduleBluetoothFailure(const QString &message,
                                   const char *reason);
    void _resetReceiveDiagnostics();
    QString _receiveTimeoutMessage() const;
    QString _configuredBluetoothAddress() const;
    QString _transportDescription() const;
    void _applyZoomDirection(int direction, bool directionChanged);
    void _tryStartZoom(int direction);
    void _resetInput(bool normalZoomStop);
    void _setBluetoothConnected(bool connected);
    void _setSdkRouteActive(bool active);
    void _setChannelInputActive(bool active);
    void _setLastError(const QString &message);
    void _setDiagnosticStage(const QString &stage);

    static constexpr int kInitialFrameTimeoutMs = 1500;
    static constexpr int kActiveFrameTimeoutMs = 350;
    static constexpr int kConnectionTimeoutMs = 10000;
    static constexpr int kRequestQueueTimeoutMs = 10000;
    static constexpr int kReconnectDelayMs = 3000;
    static constexpr int kFailureRetryDelayMs = 10000;
    static constexpr int kSocketCloseTimeoutMs = 1000;
    static constexpr int kZoomStartRetryMs = 250;
    static constexpr int kDiagnosticRefreshMs = 500;
    static constexpr int kReceiveSampleMaxBytes = 64;
    static constexpr quint32 kBluetoothSdkInterface = 0;

    QPointer<GimbalControlSettings> _settings;
    QPointer<GimbalControlManager> _gimbalControlManager;
    QPointer<GimbalCenterCoordinator> _gimbalCenterCoordinator;
    QBluetoothSocket *_socket = nullptr;
    UniRcProtocol::StreamParser _parser;
    UniRcChannelPolicy _channelPolicy;
    QTimer _reconnectTimer;
    QTimer _connectionTimeout;
    QTimer _inputWatchdog;
    QElapsedTimer _zoomStartRetryElapsed;
    QElapsedTimer _connectionElapsed;
    QElapsedTimer _requestElapsed;
    QElapsedTimer _diagnosticRefreshElapsed;
    QString _lastError;
    QString _diagnosticStage = QStringLiteral("DISABLED");
    QByteArray _receiveSample;
    QByteArray _lastRequestPacket;
    QVariantList _channelValues;
    quint64 _connectionAttempt = 0;
    quint64 _receivedByteCount = 0;
    quint64 _decodedFrameCount = 0;
    quint64 _channelFrameCount = 0;
    quint64 _invalidChannelFrameCount = 0;
    quint64 _requestFrameCount = 0;
    quint64 _requestByteCount = 0;
    quint64 _requestConfirmedByteCount = 0;
    int _lastFramePayloadSize = -1;
    int _acceptedZoomDirection = 0;
    quint8 _lastFrameControl = 0;
    quint8 _lastFrameCommand = 0;
    bool _bluetoothConnected = false;
    bool _bluetoothPaired = false;
    bool _sdkRouteActive = false;
    bool _channelInputActive = false;
    bool _invalidChannelWarningActive = false;
    bool _applicationActive = false;
    bool _permissionRequestPending = false;
    bool _requestAwaitingTransmission = false;
    bool _failureScheduled = false;
    bool _shuttingDown = false;
};
