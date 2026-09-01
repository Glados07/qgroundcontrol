/****************************************************************************
 *
 * UniRC 10 Pro Bluetooth SDK channel bridge.
 *
 ****************************************************************************/

#include "UniRcChannelController.h"

#include "GimbalCenterCoordinator.h"
#include "GimbalControlManager.h"
#include "GimbalControlSettings.h"
#include "QGCLoggingCategory.h"

#include <QtBluetooth/QBluetoothAddress>
#include <QtBluetooth/QBluetoothLocalDevice>
#include <QtBluetooth/QBluetoothServiceInfo>
#include <QtBluetooth/QBluetoothUuid>
#include <QtCore/QCoreApplication>
#include <QtCore/QIODevice>
#include <QtCore/QPermissions>
#include <QtCore/QVariant>
#include <QtGui/QGuiApplication>

#include <array>
#include <utility>

QGC_LOGGING_CATEGORY(UniRcChannelLog, "gcs.custom.android.unircchannel")

namespace {

bool isUniRcBluetoothName(const QString &name)
{
    return name.trimmed().startsWith(QStringLiteral("BLUE"),
                                     Qt::CaseInsensitive);
}

QString bluetoothDeviceLabel(const QBluetoothDeviceInfo &info)
{
    const QString name = info.name().trimmed().isEmpty()
        ? UniRcChannelController::tr("Unnamed Bluetooth device")
        : info.name().trimmed();
    return QStringLiteral("%1 [%2]")
        .arg(name, info.address().toString());
}

} // namespace

UniRcChannelController::UniRcChannelController(
    GimbalControlSettings *settings,
    GimbalControlManager *gimbalControlManager,
    GimbalCenterCoordinator *gimbalCenterCoordinator,
    QObject *parent)
    : QObject(parent)
    , _settings(settings)
    , _gimbalControlManager(gimbalControlManager)
    , _gimbalCenterCoordinator(gimbalCenterCoordinator)
    , _discoveryAgent(new QBluetoothDeviceDiscoveryAgent(this))
{
    Q_ASSERT(_settings);
    Q_ASSERT(_gimbalControlManager);
    Q_ASSERT(_gimbalCenterCoordinator);

    _reconnectTimer.setSingleShot(true);
    _reconnectTimer.setInterval(kReconnectDelayMs);
    _connectionTimeout.setSingleShot(true);
    _connectionTimeout.setInterval(kConnectionTimeoutMs);
    _inputWatchdog.setSingleShot(true);

    connect(&_reconnectTimer,
            &QTimer::timeout,
            this,
            &UniRcChannelController::_reconcile);
    connect(&_inputWatchdog,
            &QTimer::timeout,
            this,
            &UniRcChannelController::_inputWatchdogExpired);
    connect(&_connectionTimeout,
            &QTimer::timeout,
            this,
            &UniRcChannelController::_connectionTimeoutExpired);
    connect(_discoveryAgent,
            &QBluetoothDeviceDiscoveryAgent::deviceDiscovered,
            this,
            &UniRcChannelController::_deviceDiscovered);
    connect(_discoveryAgent,
            &QBluetoothDeviceDiscoveryAgent::finished,
            this,
            &UniRcChannelController::_discoveryFinished);
    connect(_discoveryAgent,
            &QBluetoothDeviceDiscoveryAgent::canceled,
            this,
            &UniRcChannelController::_discoveryCanceled);
    connect(_discoveryAgent,
            &QBluetoothDeviceDiscoveryAgent::errorOccurred,
            this,
            &UniRcChannelController::_discoveryError);
    connect(_settings->uniRcChannelControlEnabled(),
            &Fact::rawValueChanged,
            this,
            &UniRcChannelController::_settingsChanged);
    connect(_settings->uniRcSdkBluetoothAddress(),
            &Fact::rawValueChanged,
            this,
            &UniRcChannelController::_settingsChanged);

    if (auto *application =
            qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
        _applicationActive =
            application->applicationState() == Qt::ApplicationActive;
        connect(application,
                &QGuiApplication::applicationStateChanged,
                this,
                &UniRcChannelController::_applicationStateChanged);
    }

    _updateSelectedBluetoothDevice();
    QTimer::singleShot(0, this, &UniRcChannelController::_reconcile);
}

UniRcChannelController::~UniRcChannelController()
{
    shutdown();
}

bool UniRcChannelController::bluetoothScanning() const
{
    return _discoveryAgent && _discoveryAgent->isActive();
}

void UniRcChannelController::startBluetoothScan()
{
    _startBluetoothScan(true);
}

void UniRcChannelController::selectBluetoothDevice(int index)
{
    if (!_settings
        || index < 0
        || index >= _bluetoothDeviceInfos.size()) {
        return;
    }

    const QBluetoothDeviceInfo &info = _bluetoothDeviceInfos.at(index);
    if (!info.isValid() || info.address().isNull()) {
        return;
    }
    if (!isUniRcBluetoothName(info.name())) {
        qCDebug(UniRcChannelLog)
            << "Ignoring non-UniRC Bluetooth device"
            << info.name() << info.address().toString();
        return;
    }

    if (_discoveryAgent->isActive()) {
        _discoveryAgent->stop();
    }
    _setDiagnosticStage(QStringLiteral("BT_DEVICE_SELECTED"));
    _settings->uniRcSdkBluetoothAddress()->setRawValue(
        info.address().toString());
    qCInfo(UniRcChannelLog)
        << "UniRC Bluetooth event device-selected"
        << info.name() << info.address().toString();
}

void UniRcChannelController::shutdown()
{
    if (_shuttingDown) {
        return;
    }

    _shuttingDown = true;
    _setDiagnosticStage(QStringLiteral("SHUTDOWN"));
    _reconnectTimer.stop();
    _connectionTimeout.stop();
    _inputWatchdog.stop();
    if (_discoveryAgent && _discoveryAgent->isActive()) {
        _discoveryAgent->stop();
    }
    if (_gimbalCenterCoordinator) {
        _gimbalCenterCoordinator->cancel();
    }
    _closeBluetooth(true, "shutdown");
}

void UniRcChannelController::_settingsChanged()
{
    if (_shuttingDown) {
        return;
    }

    _reconnectTimer.stop();
    if (_bluetoothPaired) {
        _bluetoothPaired = false;
        emit diagnosticsChanged();
    }
    if (_discoveryAgent->isActive()) {
        _discoveryAgent->stop();
    }
    _closeBluetooth(true, "settings-changed");
    _updateSelectedBluetoothDevice();
    _reconcile();
}

void UniRcChannelController::_applicationStateChanged(
    Qt::ApplicationState state)
{
    _applicationActive = state == Qt::ApplicationActive;
    if (!_applicationActive) {
        _setDiagnosticStage(QStringLiteral("APP_INACTIVE"));
        _reconnectTimer.stop();
        _connectionTimeout.stop();
        _inputWatchdog.stop();
        if (_discoveryAgent->isActive()) {
            _discoveryAgent->stop();
        }
        if (_gimbalCenterCoordinator) {
            _gimbalCenterCoordinator->cancel();
        }
        _closeBluetooth(true, "application-background");
        return;
    }
    _reconcile();
}

bool UniRcChannelController::_shouldRun() const
{
#ifdef Q_OS_ANDROID
    return !_shuttingDown
        && !_failureScheduled
        && _applicationActive
        && _settings
        && _settings->uniRcChannelControlEnabled()->rawValue().toBool();
#else
    return false;
#endif
}

void UniRcChannelController::_reconcile()
{
    if (!_shouldRun()) {
        _reconnectTimer.stop();
        _connectionTimeout.stop();
        _inputWatchdog.stop();
        if (_discoveryAgent->isActive()) {
            _discoveryAgent->stop();
        }
        _closeBluetooth(true, "not-running");
        if (!_failureScheduled) {
            if (_shuttingDown) {
                _setDiagnosticStage(QStringLiteral("SHUTDOWN"));
            } else if (!_applicationActive) {
                _setDiagnosticStage(QStringLiteral("APP_INACTIVE"));
            } else {
                _setDiagnosticStage(QStringLiteral("DISABLED"));
            }
        }
        if (!_shuttingDown
            && _settings
            && !_settings->uniRcChannelControlEnabled()->rawValue().toBool()) {
            _setLastError(QString());
        }
        return;
    }

    if (_socket || _discoveryAgent->isActive()
        || _permissionRequestPending) {
        return;
    }
    if (!_ensureBluetoothPermission() || !_ensureBluetoothPoweredOn()) {
        if (!_permissionRequestPending && !_reconnectTimer.isActive()) {
            _reconnectTimer.start(kReconnectDelayMs);
        }
        return;
    }

    if (_configuredBluetoothAddress().isEmpty()) {
        _startBluetoothScan(false);
        return;
    }
    _connectBluetooth();
}

bool UniRcChannelController::_ensureBluetoothPermission()
{
    QBluetoothPermission permission;
    permission.setCommunicationModes(QBluetoothPermission::Access);
    QCoreApplication *const application = QCoreApplication::instance();
    if (!application) {
        _setDiagnosticStage(QStringLiteral("BT_PERMISSION_ERROR"));
        _setLastError(tr("Bluetooth permission service is unavailable."));
        return false;
    }

    const Qt::PermissionStatus status =
        application->checkPermission(permission);
    if (status == Qt::PermissionStatus::Granted) {
        return true;
    }
    if (status == Qt::PermissionStatus::Denied) {
        _setDiagnosticStage(QStringLiteral("BT_PERMISSION_DENIED"));
        _setLastError(
            tr("Nearby devices permission is required for the UniRC SDK Bluetooth connection."));
        return false;
    }
    if (_permissionRequestPending) {
        return false;
    }

    _permissionRequestPending = true;
    _setDiagnosticStage(QStringLiteral("BT_PERMISSION_REQUEST"));
    _setLastError(
        tr("Grant Nearby devices permission to scan and connect to the UniRC SDK Bluetooth device."));
    application->requestPermission(
        permission,
        this,
        [this](const QPermission &result) {
            _permissionRequestPending = false;
            if (_shuttingDown) {
                return;
            }
            if (result.status() == Qt::PermissionStatus::Granted) {
                _setDiagnosticStage(QStringLiteral("BT_PERMISSION_GRANTED"));
                _setLastError(QString());
                _reconcile();
            } else {
                _setDiagnosticStage(QStringLiteral("BT_PERMISSION_DENIED"));
                _setLastError(
                    tr("Nearby devices permission was denied. Enable it in Android app settings before using UniRC Bluetooth."));
            }
        });
    return false;
}

bool UniRcChannelController::_ensureBluetoothPoweredOn()
{
    QBluetoothLocalDevice localDevice;
    if (!localDevice.isValid()) {
        _setDiagnosticStage(QStringLiteral("BT_ADAPTER_UNAVAILABLE"));
        _setLastError(tr("No Android Bluetooth adapter is available."));
        return false;
    }
    if (localDevice.hostMode() == QBluetoothLocalDevice::HostPoweredOff) {
        _setDiagnosticStage(QStringLiteral("BT_POWERED_OFF"));
        _setLastError(
            tr("Turn on Android Bluetooth before using the UniRC SDK Bluetooth connection."));
        return false;
    }
    return true;
}

void UniRcChannelController::_startBluetoothScan(bool userRequested)
{
    if (!_shouldRun()) {
        if (userRequested) {
            _setLastError(
                tr("Enable UniRC CH9/CH10 gimbal control before scanning."));
        }
        return;
    }
    if (!_ensureBluetoothPermission() || !_ensureBluetoothPoweredOn()) {
        return;
    }

    _reconnectTimer.stop();
    if (userRequested) {
        _closeBluetooth(true, "user-scan");
    }
    if (_discoveryAgent->isActive()) {
        _discoveryAgent->stop();
    }

    _bluetoothDeviceInfos.clear();
    _bluetoothDeviceLabels.clear();
    emit bluetoothDevicesChanged();
    _updateSelectedBluetoothDevice();
    _setLastError(
        tr("Scanning for UniRC BLUE Bluetooth devices..."));
    _setDiagnosticStage(QStringLiteral("BT_SCANNING"));
    qCInfo(UniRcChannelLog)
        << "UniRC Bluetooth event scan-start"
        << "userRequested" << userRequested;
    _discoveryAgent->start(
        QBluetoothDeviceDiscoveryAgent::ClassicMethod);
    emit bluetoothScanningChanged();
}

void UniRcChannelController::_deviceDiscovered(
    const QBluetoothDeviceInfo &info)
{
    if (!info.isValid() || info.address().isNull()) {
        return;
    }
    if (!isUniRcBluetoothName(info.name())) {
        qCDebug(UniRcChannelLog)
            << "Ignoring non-UniRC Bluetooth device"
            << info.name() << info.address().toString();
        return;
    }
    for (const QBluetoothDeviceInfo &known :
         std::as_const(_bluetoothDeviceInfos)) {
        if (known.address() == info.address()) {
            return;
        }
    }

    _bluetoothDeviceInfos.append(info);
    _bluetoothDeviceLabels.append(bluetoothDeviceLabel(info));
    emit bluetoothDevicesChanged();
    _updateSelectedBluetoothDevice();
    qCInfo(UniRcChannelLog)
        << "UniRC Bluetooth event device-discovered"
        << "name" << info.name()
        << "address" << info.address().toString()
        << "pairing"
        << static_cast<int>(
               QBluetoothLocalDevice().pairingStatus(info.address()));

}

void UniRcChannelController::_discoveryFinished()
{
    emit bluetoothScanningChanged();
    if (!_shouldRun() || _reconnectTimer.isActive()) {
        return;
    }
    if (!_configuredBluetoothAddress().isEmpty()) {
        _reconcile();
        return;
    }

    _setLastError(
        _bluetoothDeviceInfos.isEmpty()
            ? tr("No UniRC BLUE Bluetooth device was found. Pair it in Android settings, then scan again.")
            : tr("No UniRC BLUE Bluetooth device was selected. Select a discovered device or enter its Bluetooth address."));
    _setDiagnosticStage(
        _bluetoothDeviceInfos.isEmpty()
            ? QStringLiteral("BT_DEVICE_NOT_FOUND")
            : QStringLiteral("BT_DEVICE_NOT_SELECTED"));
}

void UniRcChannelController::_discoveryCanceled()
{
    emit bluetoothScanningChanged();
    _updateSelectedBluetoothDevice();
    if (!_shuttingDown && !_reconnectTimer.isActive()) {
        QTimer::singleShot(0, this, &UniRcChannelController::_reconcile);
    }
}

void UniRcChannelController::_discoveryError(
    QBluetoothDeviceDiscoveryAgent::Error error)
{
    emit bluetoothScanningChanged();
    if (_shuttingDown) {
        return;
    }
    const QString message =
        tr("UniRC Bluetooth scan failed: %1")
            .arg(_discoveryAgent->errorString());
    qCWarning(UniRcChannelLog)
        << message << "error" << error;
    _setDiagnosticStage(QStringLiteral("BT_SCAN_ERROR"));
    _setLastError(message);
    if (_shouldRun() && !_reconnectTimer.isActive()) {
        _reconnectTimer.start(kFailureRetryDelayMs);
    }
}

void UniRcChannelController::_connectBluetooth()
{
    if (!_shouldRun() || _socket) {
        return;
    }
    if (!_ensureBluetoothPermission() || !_ensureBluetoothPoweredOn()) {
        return;
    }

    const QString configuredAddress = _configuredBluetoothAddress();
    const QBluetoothAddress address(configuredAddress);
    if (address.isNull()) {
        _bluetoothPaired = false;
        emit diagnosticsChanged();
        _setDiagnosticStage(QStringLiteral("BT_ADDRESS_INVALID"));
        _setLastError(
            tr("The UniRC SDK Bluetooth address is invalid: %1")
                .arg(configuredAddress));
        return;
    }

    QBluetoothLocalDevice localDevice;
    if (localDevice.pairingStatus(address)
        == QBluetoothLocalDevice::Unpaired) {
        _bluetoothPaired = false;
        emit diagnosticsChanged();
        _setDiagnosticStage(QStringLiteral("BT_NOT_PAIRED"));
        _setLastError(
            tr("Pair the UniRC BLUE device %1 in Android Bluetooth settings before connecting.")
                .arg(configuredAddress));
        return;
    }

    _bluetoothPaired = true;
    emit diagnosticsChanged();
    qCInfo(UniRcChannelLog)
        << "UniRC Bluetooth attempt" << _connectionAttempt + 1
        << "event pairing-confirmed"
        << "device" << configuredAddress;

    _resetReceiveDiagnostics();
    ++_connectionAttempt;
    _connectionElapsed.restart();
    _socket = new QBluetoothSocket(
        QBluetoothServiceInfo::RfcommProtocol,
        this);
    connect(_socket,
            &QBluetoothSocket::connected,
            this,
            &UniRcChannelController::_socketConnected);
    connect(_socket,
            &QBluetoothSocket::disconnected,
            this,
            &UniRcChannelController::_socketDisconnected);
    connect(_socket,
            &QBluetoothSocket::readyRead,
            this,
            &UniRcChannelController::_socketReadyRead);
    connect(_socket,
            &QBluetoothSocket::bytesWritten,
            this,
            &UniRcChannelController::_socketBytesWritten);
    connect(_socket,
            &QBluetoothSocket::errorOccurred,
            this,
            &UniRcChannelController::_socketError);

    _setLastError(
        tr("Connecting to UniRC SDK Bluetooth device %1...")
            .arg(_transportDescription()));
    _setDiagnosticStage(QStringLiteral("RFCOMM_CONNECTING"));
    qCInfo(UniRcChannelLog)
        << "UniRC Bluetooth attempt" << _connectionAttempt
        << "event connect-start"
        << "device" << _transportDescription()
        << "service" << "SerialPort";
    const QBluetoothUuid serialPortService(
        QBluetoothUuid::ServiceClassUuid::SerialPort);
    _socket->connectToService(address,
                              serialPortService,
                              QIODevice::ReadWrite);
    _connectionTimeout.start();
}

void UniRcChannelController::_socketConnected()
{
    QBluetoothSocket *const socket =
        qobject_cast<QBluetoothSocket *>(sender());
    if (!socket || socket != _socket) {
        return;
    }
    if (!_shouldRun()) {
        _closeBluetooth(true, "connected-while-stopped");
        return;
    }

    _connectionTimeout.stop();
    _setBluetoothConnected(true);
    _setLastError(QString());
    _requestElapsed.restart();
    _setDiagnosticStage(QStringLiteral("RFCOMM_CONNECTED"));
    qCInfo(UniRcChannelLog)
        << "UniRC Bluetooth attempt" << _connectionAttempt
        << "event connected"
        << "elapsedMs" << _connectionElapsed.elapsed()
        << "device" << _transportDescription()
        << "peerName" << _socket->peerName()
        << "peerAddress" << _socket->peerAddress().toString();

    if (!_sendChannelRequest(UniRcProtocol::Frequency20Hz)) {
        _scheduleBluetoothFailure(
            tr("Failed to send the UniRC 0x42 request over Bluetooth: %1")
                .arg(_socket->errorString()),
            "request-write-failed");
        return;
    }
    _requestAwaitingTransmission = true;
    _setDiagnosticStage(QStringLiteral("REQUEST_0X42_QUEUED"));
    if (_socket->bytesToWrite() == 0) {
        _markChannelRequestTransmitted("queue-empty-after-write");
    }
    _inputWatchdog.start(kInitialFrameTimeoutMs);
}

void UniRcChannelController::_socketDisconnected()
{
    QBluetoothSocket *const socket =
        qobject_cast<QBluetoothSocket *>(sender());
    if (!socket || socket != _socket || _shuttingDown) {
        return;
    }
    _scheduleBluetoothFailure(
        tr("The UniRC SDK Bluetooth connection to %1 was disconnected.")
            .arg(_transportDescription()),
        "socket-disconnected");
}

void UniRcChannelController::_socketError(
    QBluetoothSocket::SocketError error)
{
    QBluetoothSocket *const socket =
        qobject_cast<QBluetoothSocket *>(sender());
    if (!socket || socket != _socket || _shuttingDown) {
        return;
    }
    _scheduleBluetoothFailure(
        tr("UniRC SDK Bluetooth error on %1: %2")
            .arg(_transportDescription(), _socket->errorString()),
        "socket-error");
    qCWarning(UniRcChannelLog)
        << "UniRC Bluetooth socket error" << error;
}

void UniRcChannelController::_connectionTimeoutExpired()
{
    if (!_socket || _bluetoothConnected || _shuttingDown) {
        return;
    }
    _scheduleBluetoothFailure(
        tr("Timed out while connecting to the UniRC SDK Bluetooth device %1.")
            .arg(_transportDescription()),
        "connection-timeout");
}

bool UniRcChannelController::_sendChannelRequest(quint8 frequencyCode)
{
    if (!_socket
        || _socket->state()
            != QBluetoothSocket::SocketState::ConnectedState
        || !_socket->isWritable()) {
        return false;
    }

    const QByteArray packet =
        UniRcProtocol::channelDataRequestPacket(frequencyCode);
    if (packet.isEmpty()) {
        return false;
    }

    for (int copy = 0; copy < 3; ++copy) {
        qint64 offset = 0;
        while (offset < packet.size()) {
            const qint64 written = _socket->write(
                packet.constData() + offset,
                packet.size() - offset);
            if (written <= 0) {
                return false;
            }
            offset += written;
        }
        ++_requestFrameCount;
        _requestByteCount += static_cast<quint64>(packet.size());
        qCInfo(UniRcChannelLog)
            << "UniRC Bluetooth attempt" << _connectionAttempt
            << "event request-frame-queued" << copy + 1 << "/3"
            << "frequencyCode" << frequencyCode
            << "bytes" << packet.size()
            << "packet" << packet.toHex(' ');
    }

    _lastRequestPacket = packet;
    qCInfo(UniRcChannelLog)
        << "UniRC Bluetooth attempt" << _connectionAttempt
        << "event request-queued"
        << "frequencyCode" << frequencyCode
        << "frames" << 3
        << "bytes" << packet.size() * 3
        << "socketBytesToWrite" << _socket->bytesToWrite()
        << "packet" << packet.toHex(' ');
    return true;
}

void UniRcChannelController::_socketReadyRead()
{
    QBluetoothSocket *const socket =
        qobject_cast<QBluetoothSocket *>(sender());
    if (!socket || socket != _socket
        || _failureScheduled || _shuttingDown) {
        return;
    }

    _readAvailableBluetoothData();
}

void UniRcChannelController::_socketBytesWritten(qint64 bytes)
{
    QBluetoothSocket *const socket =
        qobject_cast<QBluetoothSocket *>(sender());
    if (!socket || socket != _socket
        || bytes <= 0
        || _failureScheduled
        || _shuttingDown) {
        return;
    }

    if (_requestAwaitingTransmission) {
        _requestConfirmedByteCount += static_cast<quint64>(bytes);
        emit diagnosticsChanged();
        qCInfo(UniRcChannelLog)
            << "UniRC Bluetooth attempt" << _connectionAttempt
            << "event request-bytes-written"
            << "writtenNow" << bytes
            << "writtenTotal" << _requestConfirmedByteCount
            << "requestBytes" << _requestByteCount
            << "socketBytesToWrite" << socket->bytesToWrite();
        if (socket->bytesToWrite() == 0) {
            _markChannelRequestTransmitted("bytes-written");
        }
    }
}

void UniRcChannelController::_markChannelRequestTransmitted(
    const char *evidence)
{
    if (!_requestAwaitingTransmission || !_socket) {
        return;
    }

    _requestAwaitingTransmission = false;
    _requestConfirmedByteCount = qMax(_requestConfirmedByteCount,
                                      _requestByteCount);
    _requestElapsed.restart();
    if (_channelFrameCount == 0) {
        _inputWatchdog.start(kInitialFrameTimeoutMs);
    }
    if (_receivedByteCount == 0) {
        _setDiagnosticStage(
            QStringLiteral("REQUEST_0X42_TRANSMITTED"));
    } else {
        emit diagnosticsChanged();
    }
    qCInfo(UniRcChannelLog)
        << "UniRC Bluetooth attempt" << _connectionAttempt
        << "event request-transmitted"
        << "evidence" << evidence
        << "requestFrames" << _requestFrameCount
        << "requestBytes" << _requestByteCount
        << "writtenBytes" << _requestConfirmedByteCount
        << "socketBytesToWrite" << _socket->bytesToWrite()
        << "remoteAck" << false;
}

void UniRcChannelController::_readAvailableBluetoothData()
{
    if (!_socket || _failureScheduled || _shuttingDown) {
        return;
    }

    const QByteArray incoming = _socket->readAll();
    if (incoming.isEmpty()) {
        return;
    }

    const bool firstReceive = _receivedByteCount == 0;
    _receivedByteCount += static_cast<quint64>(incoming.size());
    if (_receiveSample.size() < kReceiveSampleMaxBytes) {
        _receiveSample.append(
            incoming.left(kReceiveSampleMaxBytes - _receiveSample.size()));
    }
    if (firstReceive) {
        _setDiagnosticStage(QStringLiteral("BT_RX_NO_SDK_FRAME"));
        qCInfo(UniRcChannelLog)
            << "UniRC Bluetooth attempt" << _connectionAttempt
            << "event first-rx"
            << "requestElapsedMs"
            << (_requestElapsed.isValid()
                    ? _requestElapsed.elapsed()
                    : -1)
            << "chunkBytes" << incoming.size()
            << "chunkSample" << incoming.left(32).toHex(' ');
    }

    const QList<UniRcProtocol::DecodedPacket> packets =
        _parser.append(incoming);
    const bool firstValidSdkFrame =
        _decodedFrameCount == 0 && !packets.isEmpty();
    _decodedFrameCount += static_cast<quint64>(packets.size());
    if (firstValidSdkFrame) {
        const UniRcProtocol::DecodedPacket &packet = packets.first();
        qCInfo(UniRcChannelLog)
            << "UniRC Bluetooth attempt" << _connectionAttempt
            << "event sdk-frame-valid"
            << "control"
            << QStringLiteral("0x%1")
                   .arg(static_cast<qulonglong>(packet.control),
                        2,
                        16,
                        QLatin1Char('0'))
            << "command"
            << QStringLiteral("0x%1")
                   .arg(static_cast<qulonglong>(packet.command),
                        2,
                        16,
                        QLatin1Char('0'))
            << "payloadBytes" << packet.payload.size()
            << "sequence" << packet.sequence;
    }
    for (const UniRcProtocol::DecodedPacket &packet : packets) {
        _lastFrameControl = packet.control;
        _lastFrameCommand = packet.command;
        _lastFramePayloadSize = packet.payload.size();
        _handleChannelPacket(packet);
    }
    if (firstValidSdkFrame
        && !_sdkRouteActive
        && !_failureScheduled) {
        _setDiagnosticStage(QStringLiteral("SDK_FRAME_NO_0X42"));
    }
    if (!_diagnosticRefreshElapsed.isValid()
        || _diagnosticRefreshElapsed.elapsed() >= kDiagnosticRefreshMs) {
        _diagnosticRefreshElapsed.restart();
        emit diagnosticsChanged();
    }
}

void UniRcChannelController::_handleChannelPacket(
    const UniRcProtocol::DecodedPacket &packet)
{
    if (_failureScheduled || _shuttingDown) {
        return;
    }
    if (!_gimbalControlManager || !_gimbalCenterCoordinator) {
        _scheduleBluetoothFailure(
            tr("UniRC gimbal control dependencies are unavailable."),
            "dependencies-unavailable");
        return;
    }

    std::array<qint16, 16> channels {};
    if (!UniRcProtocol::parseChannelData(packet, &channels)) {
        return;
    }

    const qint16 channel9 = channels.at(8);
    const qint16 channel10 = channels.at(9);
    ++_channelFrameCount;
    if (_channelFrameCount == 1) {
        _setSdkRouteActive(true);
        _setDiagnosticStage(QStringLiteral("SDK_ROUTE_ACTIVE"));
        qCInfo(UniRcChannelLog)
            << "UniRC Bluetooth attempt" << _connectionAttempt
            << "event stream-active"
            << "requestElapsedMs"
            << (_requestElapsed.isValid()
                    ? _requestElapsed.elapsed()
                    : -1)
            << "device" << _transportDescription()
            << "rxBytes" << _receivedByteCount
            << "decodedFrames" << _decodedFrameCount
            << "control"
            << QStringLiteral("0x%1")
                   .arg(static_cast<qulonglong>(packet.control),
                        2,
                        16,
                        QLatin1Char('0'))
            << "sequence" << packet.sequence
            << "command"
            << QStringLiteral("0x%1")
                   .arg(static_cast<qulonglong>(packet.command),
                        2,
                        16,
                        QLatin1Char('0'))
            << "payloadBytes" << packet.payload.size()
            << "payload" << packet.payload.toHex(' ')
            << "CH9" << channel9
            << "CH10" << channel10;
    }

    _inputWatchdog.start(kActiveFrameTimeoutMs);
    if (_channel9 != channel9 || _channel10 != channel10) {
        _channel9 = channel9;
        _channel10 = channel10;
        emit channelsChanged();
    }

    const UniRcChannelPolicy::Result result =
        _channelPolicy.update(channel9, channel10);
    if (!result.channelsValid) {
        ++_invalidChannelFrameCount;
        if (!_invalidChannelWarningActive) {
            qCWarning(UniRcChannelLog)
                << "Rejected out-of-range UniRC channels"
                << "CH9" << channel9 << "CH10" << channel10;
        }
        _invalidChannelWarningActive = true;
        _resetInput(false);
        _setLastError(
            tr("Receiving UniRC 0x42 data, but CH9=%1 or CH10=%2 is outside 900-2100; check the channel mapping.")
                .arg(channel9)
                .arg(channel10));
        return;
    }

    _invalidChannelWarningActive = false;
    _setChannelInputActive(true);
    _setLastError(QString());

    _applyZoomDirection(result.zoomDirection,
                        result.zoomDirectionChanged);
    if (result.centerRequested) {
        const bool accepted =
            _gimbalCenterCoordinator
            && _gimbalCenterCoordinator->requestCenter();
        qCInfo(UniRcChannelLog)
            << "UniRC CH10 press requested MAVLink gimbal center"
            << "accepted" << accepted;
    }
}

void UniRcChannelController::_applyZoomDirection(int direction,
                                                 bool directionChanged)
{
    if (directionChanged) {
        if (_gimbalControlManager->uniRcZoomActive()) {
            (void) _gimbalControlManager->stopUniRcZoom();
        }
        _acceptedZoomDirection = 0;
        _zoomStartRetryElapsed.invalidate();
        if (direction != 0) {
            _tryStartZoom(direction);
        } else {
            qCInfo(UniRcChannelLog)
                << "UniRC CH9 returned to center; stopped A8 Mini zoom";
        }
        return;
    }

    if (direction == 0) {
        return;
    }
    if (_gimbalControlManager->uniRcZoomActive()) {
        (void) _gimbalControlManager->startUniRcZoom(direction);
        return;
    }
    if (_acceptedZoomDirection == direction) {
        return;
    }
    if (!_zoomStartRetryElapsed.isValid()
        || _zoomStartRetryElapsed.elapsed() >= kZoomStartRetryMs) {
        _tryStartZoom(direction);
    }
}

void UniRcChannelController::_tryStartZoom(int direction)
{
    _zoomStartRetryElapsed.restart();
    if (!_gimbalControlManager->startUniRcZoom(direction)) {
        return;
    }

    _acceptedZoomDirection = direction;
    qCInfo(UniRcChannelLog)
        << "Accepted UniRC CH9 A8 Mini zoom direction" << direction
        << "native continuous active"
        << _gimbalControlManager->uniRcZoomActive();
}

void UniRcChannelController::_resetInput(bool normalZoomStop)
{
    if (_gimbalControlManager && _gimbalControlManager->uniRcZoomActive()) {
        if (normalZoomStop) {
            (void) _gimbalControlManager->stopUniRcZoom();
        } else {
            (void) _gimbalControlManager->cancelUniRcZoom();
        }
    }
    _channelPolicy.linkLost();
    _acceptedZoomDirection = 0;
    _zoomStartRetryElapsed.invalidate();
    _setChannelInputActive(false);
}

void UniRcChannelController::_inputWatchdogExpired()
{
    // If the GUI thread was busy, readyRead and this timer can be queued at
    // the same time while a complete 0x42 frame is already buffered. Drain
    // the current socket first; a valid frame re-arms the watchdog.
    _readAvailableBluetoothData();
    if (_inputWatchdog.isActive()
        || _failureScheduled
        || _shuttingDown) {
        return;
    }

    if (_channelFrameCount == 0 && _socket) {
        const qint64 queuedBytes = _socket->bytesToWrite();
        if (queuedBytes > 0
            && _requestElapsed.isValid()
            && _requestElapsed.elapsed() < kRequestQueueTimeoutMs) {
            const int remainingMs = static_cast<int>(
                kRequestQueueTimeoutMs - _requestElapsed.elapsed());
            qCInfo(UniRcChannelLog)
                << "UniRC Bluetooth attempt" << _connectionAttempt
                << "event request-still-queued"
                << "queuedBytes" << queuedBytes
                << "requestElapsedMs" << _requestElapsed.elapsed();
            _inputWatchdog.start(
                qMin(kInitialFrameTimeoutMs, remainingMs));
            return;
        }
        if (queuedBytes == 0 && _requestAwaitingTransmission) {
            _markChannelRequestTransmitted("watchdog-queue-empty");
            return;
        }
    }

    _resetInput(false);
    const bool requestStillQueued =
        _channelFrameCount == 0
        && _socket
        && _socket->bytesToWrite() > 0;
    _scheduleBluetoothFailure(
        _receiveTimeoutMessage(),
        requestStillQueued
            ? "request-queue-timeout"
            : (_channelFrameCount > 0
                   ? "channel-stream-stopped"
                   : "initial-response-timeout"));
}

void UniRcChannelController::_scheduleBluetoothFailure(
    const QString &message,
    const char *reason)
{
    if (_failureScheduled || _shuttingDown) {
        return;
    }

    _failureScheduled = true;
    _setSdkRouteActive(false);
    QString diagnosticReason = QString::fromLatin1(reason).toUpper();
    diagnosticReason.replace(QLatin1Char('-'), QLatin1Char('_'));
    _setDiagnosticStage(
        QStringLiteral("ERROR_%1").arg(diagnosticReason));
    _connectionTimeout.stop();
    _inputWatchdog.stop();
    _resetInput(false);
    qCWarning(UniRcChannelLog)
        << "UniRC Bluetooth attempt" << _connectionAttempt
        << "event failed"
        << "reason" << reason
        << "device" << _transportDescription()
        << "socketState"
        << (_socket
                ? static_cast<int>(_socket->state())
                : -1)
        << "socketError"
        << (_socket ? _socket->errorString() : QString())
        << "socketBytesToWrite"
        << (_socket ? _socket->bytesToWrite() : 0)
        << "requestFrames" << _requestFrameCount
        << "requestBytes" << _requestByteCount
        << "requestWrittenBytes" << _requestConfirmedByteCount
        << "requestElapsedMs"
        << (_requestElapsed.isValid()
                ? _requestElapsed.elapsed()
                : -1)
        << "rxBytes" << _receivedByteCount
        << "decodedFrames" << _decodedFrameCount
        << "channelFrames" << _channelFrameCount
        << "lastControl" << _lastFrameControl
        << "lastCommand" << _lastFrameCommand
        << "lastPayloadBytes" << _lastFramePayloadSize
        << "rxSample" << _receiveSample.toHex(' ')
        << "message" << message;
    _setLastError(message);

    QTimer::singleShot(0, this, [this]() {
        _closeBluetooth(false, "failure");
        _failureScheduled = false;
        if (_shouldRun() && !_reconnectTimer.isActive()) {
            _reconnectTimer.start(kFailureRetryDelayMs);
        }
    });
}

void UniRcChannelController::_closeBluetooth(bool sendDisableRequest,
                                             const char *reason)
{
    _connectionTimeout.stop();
    _inputWatchdog.stop();
    _resetInput(false);
    _setSdkRouteActive(false);
    _requestAwaitingTransmission = false;
    _parser.reset();

    if (!_socket) {
        _setBluetoothConnected(false);
        return;
    }

    QBluetoothSocket *const socket = _socket;
    if (sendDisableRequest
        && _bluetoothConnected
        && _channelFrameCount > 0) {
        (void) _sendChannelRequest(UniRcProtocol::FrequencyOff);
    }
    qCInfo(UniRcChannelLog)
        << "Closing UniRC SDK Bluetooth connection"
        << "reason" << reason
        << "device" << _transportDescription();

    socket->disconnect(this);
    _socket = nullptr;
    _setBluetoothConnected(false);
    if (socket->state()
        != QBluetoothSocket::SocketState::UnconnectedState) {
        connect(socket,
                &QBluetoothSocket::disconnected,
                socket,
                &QObject::deleteLater);
        socket->disconnectFromService();
        QTimer::singleShot(
            kSocketCloseTimeoutMs,
            socket,
            [socket]() {
                if (socket->state()
                    != QBluetoothSocket::SocketState::UnconnectedState) {
                    socket->abort();
                }
                socket->deleteLater();
            });
    } else {
        socket->deleteLater();
    }
}

void UniRcChannelController::_resetReceiveDiagnostics()
{
    _setSdkRouteActive(false);
    _parser.reset();
    _receiveSample.clear();
    _lastRequestPacket.clear();
    _receivedByteCount = 0;
    _decodedFrameCount = 0;
    _channelFrameCount = 0;
    _invalidChannelFrameCount = 0;
    _requestFrameCount = 0;
    _requestByteCount = 0;
    _requestConfirmedByteCount = 0;
    _requestAwaitingTransmission = false;
    _requestElapsed.invalidate();
    _diagnosticRefreshElapsed.invalidate();
    _invalidChannelWarningActive = false;
    _lastFrameControl = 0;
    _lastFrameCommand = 0;
    _lastFramePayloadSize = -1;
    emit diagnosticsChanged();
}

QString UniRcChannelController::_receiveTimeoutMessage() const
{
    if (_channelFrameCount > 0) {
        return tr("UniRC 0x42 Bluetooth channel data stopped on %1 after %2 frame(s).")
            .arg(_transportDescription())
            .arg(_channelFrameCount);
    }
    if (_socket && _socket->bytesToWrite() > 0) {
        return tr("The UniRC request still had %1 Bluetooth byte(s) queued after %2 ms; the SPP transport did not accept the request.")
            .arg(_socket->bytesToWrite())
            .arg(_requestElapsed.isValid()
                     ? _requestElapsed.elapsed()
                     : kRequestQueueTimeoutMs);
    }
    if (_receivedByteCount == 0) {
        return tr("Connected to %1 and completed the local Bluetooth write for the UniRC 0x42 request, but received no data. RFCOMM is connected, but the SDK route is not confirmed; check the UniGCS Bluetooth route.")
            .arg(_transportDescription());
    }
    if (_decodedFrameCount == 0) {
        return tr("Received %1 Bluetooth byte(s) from %2, but no valid UniRC SDK frame.")
            .arg(_receivedByteCount)
            .arg(_transportDescription());
    }
    return tr("Received %1 valid UniRC SDK frame(s) from %2, but no CTRL=0, CMD=0x42, 32-byte channel frame.")
        .arg(_decodedFrameCount)
        .arg(_transportDescription());
}

QString UniRcChannelController::_configuredBluetoothAddress() const
{
    return _settings
        ? _settings->uniRcSdkBluetoothAddress()
              ->rawValue().toString().trimmed()
        : QString();
}

QString UniRcChannelController::_transportDescription() const
{
    if (!_selectedBluetoothDevice.isEmpty()) {
        return _selectedBluetoothDevice;
    }
    const QString address = _configuredBluetoothAddress();
    return address.isEmpty()
        ? tr("unselected device")
        : address;
}

QString UniRcChannelController::diagnosticSummary() const
{
    const qint64 queuedBytes = _socket ? _socket->bytesToWrite() : 0;
    const QString lastFrame = _lastFramePayloadSize < 0
        ? QStringLiteral("n/a")
        : QStringLiteral("ctrl=0x%1,cmd=0x%2,len=%3")
              .arg(static_cast<qulonglong>(_lastFrameControl),
                   2,
                   16,
                   QLatin1Char('0'))
              .arg(static_cast<qulonglong>(_lastFrameCommand),
                   2,
                   16,
                   QLatin1Char('0'))
              .arg(_lastFramePayloadSize);
    return QStringLiteral(
               "stage=%1 | paired=%2 | RFCOMM=%3 | TX=%4/%5 B | queued=%6 B | RX=%7 B | SDK=%8 | 0x42=%9 | control=%10 | last=%11")
        .arg(_diagnosticStage)
        .arg(_bluetoothPaired ? QStringLiteral("yes")
                              : QStringLiteral("no"))
        .arg(_bluetoothConnected ? QStringLiteral("connected")
                                 : QStringLiteral("disconnected"))
        .arg(_requestConfirmedByteCount)
        .arg(_requestByteCount)
        .arg(queuedBytes)
        .arg(_receivedByteCount)
        .arg(_decodedFrameCount)
        .arg(_channelFrameCount)
        .arg(_channelInputActive ? QStringLiteral("ready")
                                 : QStringLiteral("safe"))
        .arg(lastFrame);
}

void UniRcChannelController::_updateSelectedBluetoothDevice()
{
    const QBluetoothAddress configured(
        _configuredBluetoothAddress());
    QString selected = configured.isNull()
        ? QString()
        : configured.toString();
    for (const QBluetoothDeviceInfo &info :
         std::as_const(_bluetoothDeviceInfos)) {
        if (info.address() == configured) {
            selected = bluetoothDeviceLabel(info);
            break;
        }
    }
    if (_selectedBluetoothDevice == selected) {
        return;
    }
    _selectedBluetoothDevice = selected;
    emit selectedBluetoothDeviceChanged();
    emit diagnosticsChanged();
}

void UniRcChannelController::_setBluetoothConnected(bool connected)
{
    if (_bluetoothConnected == connected) {
        return;
    }
    _bluetoothConnected = connected;
    emit bluetoothConnectedChanged();
    emit diagnosticsChanged();
}

void UniRcChannelController::_setSdkRouteActive(bool active)
{
    if (_sdkRouteActive == active) {
        return;
    }
    _sdkRouteActive = active;
    emit sdkRouteActiveChanged();
    emit diagnosticsChanged();
}

void UniRcChannelController::_setChannelInputActive(bool active)
{
    if (_channelInputActive == active) {
        return;
    }
    _channelInputActive = active;
    emit channelInputActiveChanged();
    emit diagnosticsChanged();
}

void UniRcChannelController::_setLastError(const QString &message)
{
    if (_lastError == message) {
        return;
    }
    _lastError = message;
    emit lastErrorChanged();
}

void UniRcChannelController::_setDiagnosticStage(
    const QString &stage)
{
    if (_diagnosticStage == stage) {
        emit diagnosticsChanged();
        return;
    }
    _diagnosticStage = stage;
    emit diagnosticsChanged();
    qCInfo(UniRcChannelLog)
        << "UNIRC_DIAG"
        << diagnosticSummary();
}
