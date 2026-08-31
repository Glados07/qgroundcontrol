/****************************************************************************
 *
 * UniRC 10 Pro Bluetooth SDK channel bridge.
 *
 ****************************************************************************/

#pragma once

#include "UniRcChannelPolicy.h"
#include "UniRcProtocol.h"

#include <QtBluetooth/QBluetoothDeviceDiscoveryAgent>
#include <QtBluetooth/QBluetoothDeviceInfo>
#include <QtBluetooth/QBluetoothSocket>
#include <QtCore/QByteArray>
#include <QtCore/QElapsedTimer>
#include <QtCore/QList>
#include <QtCore/QLoggingCategory>
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QTimer>

class GimbalCenterCoordinator;
class GimbalControlManager;
class GimbalControlSettings;

Q_DECLARE_LOGGING_CATEGORY(UniRcChannelLog)

class UniRcChannelController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool bluetoothConnected READ bluetoothConnected NOTIFY bluetoothConnectedChanged)
    Q_PROPERTY(bool bluetoothScanning READ bluetoothScanning NOTIFY bluetoothScanningChanged)
    Q_PROPERTY(QStringList bluetoothDevices READ bluetoothDevices NOTIFY bluetoothDevicesChanged)
    Q_PROPERTY(QString selectedBluetoothDevice READ selectedBluetoothDevice NOTIFY selectedBluetoothDeviceChanged)
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

    bool bluetoothConnected() const { return _bluetoothConnected; }
    bool bluetoothScanning() const;
    QStringList bluetoothDevices() const { return _bluetoothDeviceLabels; }
    QString selectedBluetoothDevice() const { return _selectedBluetoothDevice; }
    bool channelInputActive() const { return _channelInputActive; }
    int channel9() const { return _channel9; }
    int channel10() const { return _channel10; }
    QString lastError() const { return _lastError; }

    Q_INVOKABLE void startBluetoothScan();
    Q_INVOKABLE void selectBluetoothDevice(int index);
    Q_INVOKABLE void shutdown();

signals:
    void bluetoothConnectedChanged();
    void bluetoothScanningChanged();
    void bluetoothDevicesChanged();
    void selectedBluetoothDeviceChanged();
    void channelInputActiveChanged();
    void channelsChanged();
    void lastErrorChanged();

private slots:
    void _settingsChanged();
    void _applicationStateChanged(Qt::ApplicationState state);
    void _reconcile();
    void _connectionTimeoutExpired();
    void _inputWatchdogExpired();
    void _deviceDiscovered(const QBluetoothDeviceInfo &info);
    void _discoveryFinished();
    void _discoveryCanceled();
    void _discoveryError(QBluetoothDeviceDiscoveryAgent::Error error);
    void _socketConnected();
    void _socketDisconnected();
    void _socketReadyRead();
    void _socketError(QBluetoothSocket::SocketError error);

private:
    bool _shouldRun() const;
    bool _ensureBluetoothPermission();
    bool _ensureBluetoothPoweredOn();
    void _startBluetoothScan(bool userRequested);
    void _connectBluetooth();
    void _closeBluetooth(bool sendDisableRequest, const char *reason);
    bool _sendChannelRequest(quint8 frequencyCode);
    void _readAvailableBluetoothData();
    void _handleChannelPacket(const UniRcProtocol::DecodedPacket &packet);
    void _scheduleBluetoothFailure(const QString &message,
                                   const char *reason);
    void _resetReceiveDiagnostics();
    QString _receiveTimeoutMessage() const;
    QString _configuredBluetoothAddress() const;
    QString _transportDescription() const;
    void _updateSelectedBluetoothDevice();
    void _applyZoomDirection(int direction, bool directionChanged);
    void _tryStartZoom(int direction);
    void _resetInput(bool normalZoomStop);
    void _setBluetoothConnected(bool connected);
    void _setChannelInputActive(bool active);
    void _setLastError(const QString &message);

    static constexpr int kInitialFrameTimeoutMs = 1500;
    static constexpr int kActiveFrameTimeoutMs = 350;
    static constexpr int kConnectionTimeoutMs = 10000;
    static constexpr int kRequestQueueTimeoutMs = 10000;
    static constexpr int kReconnectDelayMs = 3000;
    static constexpr int kFailureRetryDelayMs = 10000;
    static constexpr int kSocketCloseTimeoutMs = 1000;
    static constexpr int kZoomStartRetryMs = 250;
    static constexpr int kReceiveSampleMaxBytes = 64;

    QPointer<GimbalControlSettings> _settings;
    QPointer<GimbalControlManager> _gimbalControlManager;
    QPointer<GimbalCenterCoordinator> _gimbalCenterCoordinator;
    QBluetoothDeviceDiscoveryAgent *_discoveryAgent = nullptr;
    QBluetoothSocket *_socket = nullptr;
    QList<QBluetoothDeviceInfo> _bluetoothDeviceInfos;
    QStringList _bluetoothDeviceLabels;
    UniRcProtocol::StreamParser _parser;
    UniRcChannelPolicy _channelPolicy;
    QTimer _reconnectTimer;
    QTimer _connectionTimeout;
    QTimer _inputWatchdog;
    QElapsedTimer _zoomStartRetryElapsed;
    QElapsedTimer _connectionElapsed;
    QElapsedTimer _requestElapsed;
    QString _selectedBluetoothDevice;
    QString _lastError;
    QByteArray _receiveSample;
    QByteArray _lastRequestPacket;
    quint64 _connectionAttempt = 0;
    quint64 _receivedByteCount = 0;
    quint64 _decodedFrameCount = 0;
    quint64 _channelFrameCount = 0;
    quint64 _invalidChannelFrameCount = 0;
    quint64 _requestFrameCount = 0;
    quint64 _requestByteCount = 0;
    int _lastFramePayloadSize = -1;
    int _channel9 = 0;
    int _channel10 = 0;
    int _acceptedZoomDirection = 0;
    quint8 _lastFrameControl = 0;
    quint8 _lastFrameCommand = 0;
    bool _bluetoothConnected = false;
    bool _channelInputActive = false;
    bool _invalidChannelWarningActive = false;
    bool _applicationActive = false;
    bool _permissionRequestPending = false;
    bool _requestQueuePendingObserved = false;
    bool _failureScheduled = false;
    bool _shuttingDown = false;
};
