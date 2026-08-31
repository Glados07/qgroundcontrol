/****************************************************************************
 *
 * UniRC 10 Pro built-in SDK UART channel bridge.
 *
 ****************************************************************************/

#include "UniRcChannelController.h"

#include "GimbalCenterCoordinator.h"
#include "GimbalControlManager.h"
#include "GimbalControlSettings.h"
#include "QGCLoggingCategory.h"
#include "UniRcSerialAccessPolicy.h"

#include <QtCore/QByteArray>
#include <QtCore/QCoreApplication>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QStringList>
#include <QtCore/QVariant>
#include <QtGui/QGuiApplication>

#include <array>

#ifdef Q_OS_ANDROID
#include <QtCore/QJniEnvironment>
#include <QtCore/QJniObject>
#include <QtCore/QSocketNotifier>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/serial.h>
#include <poll.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
#endif

QGC_LOGGING_CATEGORY(UniRcChannelLog, "gcs.custom.android.unircchannel")

namespace {

#ifdef Q_OS_ANDROID
constexpr char kAndroidBluetoothStateClassName[] =
    "org/mavlink/qgroundcontrol/QGCCustomBluetoothState";
constexpr int kNTtyLineDiscipline = 0;
constexpr int kOwnershipProbeReadLimitBytes = 16 * 1024;

bool optionalTtyIoctlUnsupported(int errorNumber)
{
    return errorNumber == ENOTTY || errorNumber == EINVAL;
}

qint64 sdkSafeIdleBaudRate(qulonglong speedCode)
{
    if (speedCode == static_cast<qulonglong>(B9600)) {
        return 9600;
    }
    if (speedCode == static_cast<qulonglong>(B38400)) {
        return 38400;
    }
    if (speedCode == static_cast<qulonglong>(B115200)) {
        return 115200;
    }
    return -1;
}

QString serialSpeedDescription(qulonglong speedCode)
{
    if (speedCode == static_cast<qulonglong>(B9600)) {
        return QStringLiteral("9600");
    }
    if (speedCode == static_cast<qulonglong>(B38400)) {
        return QStringLiteral("38400");
    }
    if (speedCode == static_cast<qulonglong>(B115200)) {
        return QStringLiteral("115200");
    }
#ifdef B230400
    if (speedCode == static_cast<qulonglong>(B230400)) {
        return QStringLiteral("230400");
    }
#endif
#ifdef B460800
    if (speedCode == static_cast<qulonglong>(B460800)) {
        return QStringLiteral("460800");
    }
#endif
#ifdef B921600
    if (speedCode == static_cast<qulonglong>(B921600)) {
        return QStringLiteral("921600");
    }
#endif
#ifdef B1000000
    if (speedCode == static_cast<qulonglong>(B1000000)) {
        return QStringLiteral("1000000");
    }
#endif
#ifdef B2000000
    if (speedCode == static_cast<qulonglong>(B2000000)) {
        return QStringLiteral("2000000");
    }
#endif
#ifdef B3000000
    if (speedCode == static_cast<qulonglong>(B3000000)) {
        return QStringLiteral("3000000");
    }
#endif
#ifdef B3500000
    if (speedCode == static_cast<qulonglong>(B3500000)) {
        return QStringLiteral("3500000");
    }
#endif
#ifdef B4000000
    if (speedCode == static_cast<qulonglong>(B4000000)) {
        return QStringLiteral("4000000");
    }
#endif
#ifdef BOTHER
    if (speedCode == static_cast<qulonglong>(BOTHER)) {
        return QStringLiteral("BOTHER(code=%1)").arg(speedCode);
    }
#endif
    return QStringLiteral("code=%1").arg(speedCode);
}

struct AndroidBluetoothSnapshot {
    UniRcSerialAccessPolicy::BluetoothState state =
        UniRcSerialAccessPolicy::BluetoothState::Unknown;
    QString evidence = QStringLiteral("helper=unavailable");
};

AndroidBluetoothSnapshot androidBluetoothSnapshot()
{
    using BluetoothState = UniRcSerialAccessPolicy::BluetoothState;
    AndroidBluetoothSnapshot snapshot;

    if (!QJniObject::isClassAvailable(kAndroidBluetoothStateClassName)) {
        return snapshot;
    }

    const jint state = QJniObject::callStaticMethod<jint>(
        kAndroidBluetoothStateClassName,
        "getStateForUniRc",
        "()I");
    QJniEnvironment environment;
    if (environment.checkAndClearExceptions()) {
        snapshot.evidence = QStringLiteral("helper=exception");
        return snapshot;
    }

    const QJniObject diagnostics = QJniObject::callStaticObjectMethod(
        kAndroidBluetoothStateClassName,
        "getDiagnosticsForUniRc",
        "()Ljava/lang/String;");
    if (!environment.checkAndClearExceptions() && diagnostics.isValid()) {
        snapshot.evidence = diagnostics.toString();
    } else {
        snapshot.evidence = QStringLiteral("helper=diagnostics-unavailable");
    }

    switch (state) {
    case static_cast<jint>(BluetoothState::FullyOff):
        snapshot.state = BluetoothState::FullyOff;
        break;
    case static_cast<jint>(BluetoothState::ClassicActive):
        snapshot.state = BluetoothState::ClassicActive;
        break;
    case static_cast<jint>(BluetoothState::BleActive):
        snapshot.state = BluetoothState::BleActive;
        break;
    case static_cast<jint>(BluetoothState::ScanAlwaysEnabled):
        snapshot.state = BluetoothState::ScanAlwaysEnabled;
        break;
    case static_cast<jint>(BluetoothState::PermissionRequired):
        snapshot.state = BluetoothState::PermissionRequired;
        break;
    default:
        break;
    }
    return snapshot;
}

void requestAndroidBluetoothPermission()
{
    if (!QJniObject::isClassAvailable(kAndroidBluetoothStateClassName)) {
        return;
    }
    QJniObject::callStaticMethod<void>(
        kAndroidBluetoothStateClassName,
        "requestBluetoothConnectPermissionForUniRc",
        "()V");
    QJniEnvironment environment;
    (void) environment.checkAndClearExceptions();
}

QString errnoMessage(const QString &operation,
                     const QString &devicePath,
                     int errorNumber)
{
    return UniRcChannelController::tr("%1 %2 failed: %3 (errno %4)")
        .arg(operation,
             devicePath,
             QString::fromLocal8Bit(std::strerror(errorNumber)))
        .arg(errorNumber);
}
#endif

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
{
    Q_ASSERT(_settings);
    Q_ASSERT(_gimbalControlManager);
    Q_ASSERT(_gimbalCenterCoordinator);

    _reconnectTimer.setSingleShot(true);
    _reconnectTimer.setInterval(kReconnectDelayMs);
    _inputWatchdog.setSingleShot(true);
#ifdef Q_OS_ANDROID
    _ownershipProbeTimer.setSingleShot(true);
    _runtimeSafetyTimer.setSingleShot(false);
    _runtimeSafetyTimer.setInterval(kRuntimeSafetyPollMs);
#endif

    connect(&_reconnectTimer,
            &QTimer::timeout,
            this,
            &UniRcChannelController::_reconcile);
    connect(&_inputWatchdog,
            &QTimer::timeout,
            this,
            &UniRcChannelController::_inputWatchdogExpired);
#ifdef Q_OS_ANDROID
    connect(&_ownershipProbeTimer,
            &QTimer::timeout,
            this,
            &UniRcChannelController::_ownershipProbeExpired);
    connect(&_runtimeSafetyTimer,
            &QTimer::timeout,
            this,
            &UniRcChannelController::_runtimeSafetyCheck);
#endif
    connect(_settings->uniRcChannelControlEnabled(),
            &Fact::rawValueChanged,
            this,
            &UniRcChannelController::_settingsChanged);
    connect(_settings->uniRcSdkSerialPort(),
            &Fact::rawValueChanged,
            this,
            &UniRcChannelController::_settingsChanged);

    if (auto *application = qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
        _applicationActive =
            application->applicationState() == Qt::ApplicationActive;
        connect(application,
                &QGuiApplication::applicationStateChanged,
                this,
                &UniRcChannelController::_applicationStateChanged);
    }

    QTimer::singleShot(0, this, &UniRcChannelController::_reconcile);
}

UniRcChannelController::~UniRcChannelController()
{
    shutdown();
}

void UniRcChannelController::shutdown()
{
    if (_shuttingDown) {
        return;
    }
    _shuttingDown = true;
    _reconnectTimer.stop();
    _inputWatchdog.stop();
#ifdef Q_OS_ANDROID
    _ownershipProbeTimer.stop();
    _runtimeSafetyTimer.stop();
#endif
    if (_gimbalCenterCoordinator) {
        _gimbalCenterCoordinator->cancel();
    }
    _closeSerial(true, "shutdown");
}

void UniRcChannelController::_settingsChanged()
{
    if (_shuttingDown) {
        return;
    }

#ifdef Q_OS_ANDROID
    _bluetoothFullOffElapsed.invalidate();
    _lastPreflightLogKey.clear();
#endif

    const QString configuredPath =
        _settings->uniRcSdkSerialPort()->rawValue().toString().trimmed();
    bool serialSessionOpen = _serialOpen;
#ifdef Q_OS_ANDROID
    serialSessionOpen = _serialFd >= 0;
#endif
    if (serialSessionOpen
        && (!_settings->uniRcChannelControlEnabled()->rawValue().toBool()
            || configuredPath != _openedDevicePath)) {
        _closeSerial(true, "settings-changed");
    }
    _reconcile();
}

void UniRcChannelController::_applicationStateChanged(Qt::ApplicationState state)
{
    _applicationActive = state == Qt::ApplicationActive;
    if (!_applicationActive) {
#ifdef Q_OS_ANDROID
        _bluetoothFullOffElapsed.invalidate();
        _lastPreflightLogKey.clear();
#endif
        _reconnectTimer.stop();
        _inputWatchdog.stop();
        if (_gimbalCenterCoordinator) {
            _gimbalCenterCoordinator->cancel();
        }
        _closeSerial(true, "application-background");
        return;
    }
    _reconcile();
}

bool UniRcChannelController::_shouldRun() const
{
#ifdef Q_OS_ANDROID
    return !_shuttingDown
        && !_serialFailureScheduled
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
        _inputWatchdog.stop();
        _closeSerial(true, "not-running");
        if (!_shuttingDown
            && _settings
            && !_settings->uniRcChannelControlEnabled()->rawValue().toBool()) {
            _setLastError(QString());
        }
        return;
    }

#ifdef Q_OS_ANDROID
    if (_serialFd >= 0) {
        return;
    }
    if (!_openSerial() && !_reconnectTimer.isActive()) {
        _reconnectTimer.start(_nextRetryDelayMs);
    }
#endif
}

bool UniRcChannelController::_openSerial()
{
#ifndef Q_OS_ANDROID
    return false;
#else
    const QString devicePath =
        _settings->uniRcSdkSerialPort()->rawValue().toString().trimmed();
    _nextRetryDelayMs = kFailureRetryDelayMs;
    if (!devicePath.startsWith(QStringLiteral("/dev/"))) {
        const QString message =
            tr("UniRC SDK serial device must be an absolute /dev path.");
        _setLastError(message);
        _logPreflightTransition(QStringLiteral("invalid-device-path"),
                                message,
                                QStringLiteral("device=%1").arg(devicePath),
                                true);
        return false;
    }

    QString accessPolicyPath = devicePath;
    const QString canonicalPath = QFileInfo(devicePath).canonicalFilePath();
    if (canonicalPath == QStringLiteral("/dev/ttyHS0")) {
        accessPolicyPath = canonicalPath;
    }

    _lastBluetoothEvidence = QStringLiteral("check=not-required");

    const bool sharedBluetoothUart =
        UniRcSerialAccessPolicy::requiresBluetoothOff(accessPolicyPath);
    if (sharedBluetoothUart) {
        const AndroidBluetoothSnapshot bluetooth = androidBluetoothSnapshot();
        _lastBluetoothEvidence = bluetooth.evidence;
        qint64 fullOffStableMs = 0;
        if (bluetooth.state
            == UniRcSerialAccessPolicy::BluetoothState::FullyOff) {
            if (!_bluetoothFullOffElapsed.isValid()) {
                _bluetoothFullOffElapsed.start();
            }
            fullOffStableMs = _bluetoothFullOffElapsed.elapsed();
        } else {
            _bluetoothFullOffElapsed.invalidate();
        }

        const UniRcSerialAccessPolicy::Decision accessDecision =
            UniRcSerialAccessPolicy::evaluate(
                accessPolicyPath,
                bluetooth.state,
                fullOffStableMs,
                kBluetoothStableMs);
        if (accessDecision != UniRcSerialAccessPolicy::Decision::Allow) {
            QString message;
            if (accessDecision
                == UniRcSerialAccessPolicy::Decision::BlockBluetoothClassicActive) {
                message =
                    tr("Android Bluetooth is enabled or changing state. Turn it off before QGC can use the UniRC UART2 device %1.")
                        .arg(devicePath);
            } else if (accessDecision
                == UniRcSerialAccessPolicy::Decision::WaitForBluetoothRelease) {
                message =
                    tr("Bluetooth and BLE are off; waiting %2 ms to confirm that %1 remains released.")
                        .arg(devicePath)
                        .arg(kBluetoothStableMs);
            } else if (accessDecision
                       == UniRcSerialAccessPolicy::Decision::BlockBluetoothBleActive) {
                message =
                    tr("Android Bluetooth is off, but the BLE-only service is still active and may own %1. Turn off Bluetooth scanning in Android location/scanning settings.")
                        .arg(devicePath);
            } else if (accessDecision
                       == UniRcSerialAccessPolicy::Decision::BlockBluetoothScanAlwaysEnabled) {
                message =
                    tr("Android Bluetooth scanning is still enabled and can restart the Bluetooth HAL on %1. Turn off Bluetooth scanning in Android location/scanning settings.")
                        .arg(devicePath);
            } else if (accessDecision
                       == UniRcSerialAccessPolicy::Decision::BlockBluetoothPermissionRequired) {
                requestAndroidBluetoothPermission();
                message =
                    tr("QGC needs the Nearby devices permission to verify that Bluetooth has released %1. Grant it once; if it was denied, enable it in Android app settings before trying again.")
                        .arg(devicePath);
            } else {
                message =
                    tr("QGC cannot verify that Bluetooth and BLE have released %1. For safety, the UniRC UART2 SDK will not access it.")
                        .arg(devicePath);
            }
            _nextRetryDelayMs =
                accessDecision
                        == UniRcSerialAccessPolicy::Decision::WaitForBluetoothRelease
                    ? kBluetoothPollMs
                    : kBlockedBluetoothRetryMs;
            const QString preflightKey = QStringLiteral("bluetooth-%1-%2")
                .arg(static_cast<int>(accessDecision))
                .arg(accessDecision
                     == UniRcSerialAccessPolicy::Decision::WaitForBluetoothRelease
                         ? 1
                         : 0);
            _logPreflightTransition(
                preflightKey,
                message,
                bluetooth.evidence,
                accessDecision
                    != UniRcSerialAccessPolicy::Decision::WaitForBluetoothRelease);
            _setLastError(message);
            return false;
        }

        _logPreflightTransition(QStringLiteral("bluetooth-ready"),
                                tr("Bluetooth and BLE are fully off; checking whether %1 is idle before sending any SDK data.")
                                    .arg(devicePath),
                                bluetooth.evidence,
                                false);
    }

    _startAttempt(devicePath, _lastBluetoothEvidence);
    _sharedBluetoothUart = sharedBluetoothUart;
    const QByteArray nativePath = QFile::encodeName(devicePath);
    struct stat deviceStat {};
    if (::stat(nativePath.constData(), &deviceStat) == 0
        && !S_ISCHR(deviceStat.st_mode)) {
        _failAttempt("stat",
                     "not-character-device",
                     tr("UniRC SDK serial path is not a character device: %1")
                         .arg(devicePath),
                     false);
        return false;
    }

    const int fd = ::open(nativePath.constData(),
                          O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        const int errorNumber = errno;
        _failAttempt("open",
                     "open-failed",
                     errnoMessage(tr("Open"), devicePath, errorNumber),
                     false);
        return false;
    }
    _serialFd = fd;
    _openedDevicePath = devicePath;

    if (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
        const int errorNumber = errno;
        if (errorNumber == EWOULDBLOCK || errorNumber == EAGAIN) {
            _failAttempt("claim",
                         "cooperative-lock-busy",
                         errnoMessage(tr("Lock"), devicePath, errorNumber),
                         true);
            return false;
        }
        qCDebug(UniRcChannelLog)
            << "UniRC attempt" << _activeAttemptId
            << "UniRC serial advisory lock is unsupported"
            << devicePath << errorNumber << std::strerror(errorNumber);
    }

#ifdef TIOCEXCL
    if (::ioctl(fd, TIOCEXCL) != 0) {
        const int errorNumber = errno;
        if (UniRcSerialAccessPolicy::requiresBluetoothOff(accessPolicyPath)) {
            _failAttempt("claim",
                         errorNumber == EBUSY
                             ? "tty-exclusive-busy"
                             : "tty-exclusive-unavailable",
                         errnoMessage(tr("Exclusive open"),
                                      devicePath,
                                      errorNumber),
                         true);
            return false;
        }
        qCDebug(UniRcChannelLog)
            << "UniRC attempt" << _activeAttemptId
            << "UniRC serial TIOCEXCL is unsupported"
            << devicePath << errorNumber << std::strerror(errorNumber);
    } else {
        _ttyExclusiveClaimed = true;
    }
#endif

    if (!_sharedBluetoothUart) {
        return _configureAndRequestChannels();
    }

    QString probeError;
    if (!_captureSerialActivity(fd, &_ownershipProbeStart, &probeError)) {
        _failAttempt("ownership-probe",
                     "probe-unavailable",
                     tr("QGC cannot safely verify whether %1 is idle: %2")
                         .arg(devicePath, probeError),
                     true);
        return false;
    }
    QString releasedDetails;
    if (!_serialLooksReleased(_ownershipProbeStart, &releasedDetails)) {
        _failAttempt(
            "ownership-probe",
            "uart-not-released",
            tr("%1 has not returned to an SDK-safe idle UART state (%2). QGC did not configure or write the port; Android Bluetooth/HCI may still own it.")
                .arg(devicePath, releasedDetails),
            true);
        return false;
    }
    _serialPhase = SerialPhase::OwnershipProbe;
    _setLastError(
        tr("Checking %1 for existing UART activity before sending the UniRC request.")
            .arg(devicePath));
    qCDebug(UniRcChannelLog)
        << "UniRC attempt" << _activeAttemptId
        << "event ownership-probe-start"
        << "durationMs" << kOwnershipProbeMs
        << "device" << devicePath
        << "termios" << _ownershipProbeStart.termiosSignature
        << "lineDisciplineAvailable"
        << _ownershipProbeStart.lineDisciplineAvailable
        << "lineDisciplineErrno" << _ownershipProbeStart.lineDisciplineError
        << "countersAvailable" << _ownershipProbeStart.countersAvailable
        << "countersErrno" << _ownershipProbeStart.countersError
        << "rx" << _ownershipProbeStart.rxCount
        << "tx" << _ownershipProbeStart.txCount
        << "errors"
        << (_ownershipProbeStart.frameCount
            + _ownershipProbeStart.overrunCount
            + _ownershipProbeStart.parityCount
            + _ownershipProbeStart.breakCount
            + _ownershipProbeStart.bufferOverrunCount)
        << "queuedBytes" << _ownershipProbeStart.queuedBytes
        << "readable" << _ownershipProbeStart.readable
        << "bluetooth" << _lastBluetoothEvidence;
    _ownershipProbeTimer.start(kOwnershipProbeMs);
    return true;
#endif
}

#ifdef Q_OS_ANDROID
void UniRcChannelController::_ownershipProbeExpired()
{
    if (_serialFd < 0
        || _serialPhase != SerialPhase::OwnershipProbe
        || !_shouldRun()) {
        _closeSerial(false, "ownership-probe-cancelled");
        return;
    }

    if (_sharedBluetoothUart) {
        const AndroidBluetoothSnapshot bluetooth = androidBluetoothSnapshot();
        _lastBluetoothEvidence = bluetooth.evidence;
        if (bluetooth.state
            != UniRcSerialAccessPolicy::BluetoothState::FullyOff) {
            _bluetoothFullOffElapsed.invalidate();
            _failAttempt(
                "ownership-probe",
                "bluetooth-reactivated",
                tr("Bluetooth or BLE became active while QGC was checking %1. No UniRC data was written.")
                    .arg(_openedDevicePath),
                true);
            return;
        }
    }

    SerialActivitySnapshot probeEnd;
    QString probeError;
    if (!_captureSerialActivity(_serialFd, &probeEnd, &probeError)) {
        _failAttempt("ownership-probe",
                     "probe-read-failed",
                     tr("QGC could not complete the passive UART check for %1: %2")
                         .arg(_openedDevicePath, probeError),
                     true);
        return;
    }

    QString releasedDetails;
    if (!_serialLooksReleased(probeEnd, &releasedDetails)) {
        _failAttempt(
            "ownership-probe",
            "uart-not-released",
            tr("%1 changed to a non-idle UART state while QGC was checking it (%2). No UniRC data was written.")
                .arg(_openedDevicePath, releasedDetails),
            true);
        return;
    }

    QString activityDetails;
    const bool activityChanged =
        _serialActivityChanged(_ownershipProbeStart,
                               probeEnd,
                               &activityDetails);
    bool existingSdkStreamRecognized = false;
    UniRcProtocol::PeriodicStreamInspection streamInspection;
    QByteArray probeInput;
    if (activityChanged) {
        const qint64 rxDelta =
            probeEnd.rxCount - _ownershipProbeStart.rxCount;
        const qint64 txDelta =
            probeEnd.txCount - _ownershipProbeStart.txCount;
        const qint64 errorDelta =
            (probeEnd.frameCount - _ownershipProbeStart.frameCount)
            + (probeEnd.overrunCount - _ownershipProbeStart.overrunCount)
            + (probeEnd.parityCount - _ownershipProbeStart.parityCount)
            + (probeEnd.breakCount - _ownershipProbeStart.breakCount)
            + (probeEnd.bufferOverrunCount
               - _ownershipProbeStart.bufferOverrunCount);
        const bool counterAvailabilityStable =
            probeEnd.countersAvailable
            == _ownershipProbeStart.countersAvailable;
        const bool inputObserved =
            _ownershipProbeStart.queuedBytes > 0
            || probeEnd.queuedBytes > 0
            || _ownershipProbeStart.readable
            || probeEnd.readable
            || (_ownershipProbeStart.countersAvailable && rxDelta > 0);
        const bool sdkSpeedStable =
            _ownershipProbeStart.inputSpeed
                == static_cast<qulonglong>(B115200)
            && _ownershipProbeStart.outputSpeed
                == static_cast<qulonglong>(B115200)
            && probeEnd.inputSpeed == static_cast<qulonglong>(B115200)
            && probeEnd.outputSpeed == static_cast<qulonglong>(B115200);
        const bool safeToInspectExistingStream =
            inputObserved
            && sdkSpeedStable
            && counterAvailabilityStable
            && txDelta == 0
            && errorDelta == 0
            && _ownershipProbeStart.termiosSignature
                == probeEnd.termiosSignature;

        if (safeToInspectExistingStream) {
            QString drainError;
            if (!_drainOwnershipProbeInput(&probeInput, &drainError)) {
                _receivedByteCount =
                    static_cast<quint64>(probeInput.size());
                _receiveSample =
                    probeInput.left(kReceiveSampleMaxBytes);
                _failAttempt(
                    "ownership-probe",
                    "preexisting-input-read-failed",
                    tr("QGC found input on %1 before sending the UniRC request but could not inspect it safely: %2")
                        .arg(_openedDevicePath, drainError),
                    true);
                return;
            }
            streamInspection =
                UniRcProtocol::inspectPeriodicChannelStream(probeInput);
            existingSdkStreamRecognized = streamInspection.recognized;
        }
    }

    if (activityChanged && !existingSdkStreamRecognized) {
        if (!probeInput.isEmpty()) {
            _receivedByteCount = static_cast<quint64>(probeInput.size());
            _receiveSample = probeInput.left(kReceiveSampleMaxBytes);
        }
        _failAttempt(
            "ownership-probe",
            "uart-active-before-request",
            tr("Existing non-UniRC UART activity was detected on %1 before QGC sent any data (%2; inspectedBytes=%3; consecutive0x42=%4). Bluetooth HAL or another process may still own the port.")
                .arg(_openedDevicePath)
                .arg(activityDetails)
                .arg(probeInput.size())
                .arg(streamInspection.frameCount),
            true);
        return;
    }

    if (existingSdkStreamRecognized) {
        UniRcProtocol::Channels channels {};
        (void) UniRcProtocol::parseChannelData(streamInspection.lastPacket,
                                               &channels);
        qCInfo(UniRcChannelLog)
            << "UniRC attempt" << _activeAttemptId
            << "event existing-sdk-stream-recognized"
            << "frames" << streamInspection.frameCount
            << "leadingBytes" << streamInspection.leadingBytes
            << "trailingBytes" << streamInspection.trailingBytes
            << "payload" << streamInspection.lastPacket.payload.toHex(' ')
            << "CH9" << channels.at(8)
            << "CH10" << channels.at(9)
            << "action refresh-with-new-request";
        activityDetails =
            QStringLiteral("existing-sdk-stream-recognized(frames=%1)")
                .arg(streamInspection.frameCount);
    }

    qCDebug(UniRcChannelLog)
        << "UniRC attempt" << _activeAttemptId
        << "event ownership-probe-passed"
        << "device" << _openedDevicePath
        << "activity" << activityDetails
        << "startTermios" << _ownershipProbeStart.termiosSignature
        << "endTermios" << probeEnd.termiosSignature
        << "startCountersAvailable"
        << _ownershipProbeStart.countersAvailable
        << "startCountersErrno" << _ownershipProbeStart.countersError
        << "endCountersAvailable" << probeEnd.countersAvailable
        << "endCountersErrno" << probeEnd.countersError
        << "rxDelta" << probeEnd.rxCount - _ownershipProbeStart.rxCount
        << "txDelta" << probeEnd.txCount - _ownershipProbeStart.txCount
        << "startQueuedBytes" << _ownershipProbeStart.queuedBytes
        << "endQueuedBytes" << probeEnd.queuedBytes
        << "startReadable" << _ownershipProbeStart.readable
        << "endReadable" << probeEnd.readable;

    (void) _configureAndRequestChannels();
}

bool UniRcChannelController::_configureAndRequestChannels()
{
    if (!_configureSerial(_serialFd, _openedDevicePath)) {
        _failAttempt("configure",
                     "configure-failed",
                     lastError(),
                     true);
        return false;
    }

    QString configuredDetails;
    if (!_serialConfigurationMatches(_serialFd, &configuredDetails)) {
        _failAttempt(
            "configure",
            "verification-failed",
            tr("The UART configuration on %1 did not remain at 115200 8N1 without hardware flow control (%2). Another owner may still be changing it.")
                .arg(_openedDevicePath, configuredDetails),
            true);
        return false;
    }

    if (_sharedBluetoothUart) {
        const AndroidBluetoothSnapshot beforeRequestBluetooth =
            androidBluetoothSnapshot();
        _lastBluetoothEvidence = beforeRequestBluetooth.evidence;
        if (beforeRequestBluetooth.state
            != UniRcSerialAccessPolicy::BluetoothState::FullyOff) {
            _bluetoothFullOffElapsed.invalidate();
            _failAttempt(
                "request",
                "safety-state-changed-before-write",
                tr("Bluetooth/BLE or the UART configuration changed before the UniRC request could be sent to %1. QGC closed the port without writing data.")
                    .arg(_openedDevicePath),
                true);
            return false;
        }
    }
    if (!_serialConfigurationMatches(_serialFd, &configuredDetails)) {
        _failAttempt(
            "configure",
            "verification-changed-before-write",
            tr("The UART configuration on %1 did not remain at 115200 8N1 without hardware flow control (%2). Another owner may still be changing it.")
                .arg(_openedDevicePath, configuredDetails),
            true);
        return false;
    }

    _readNotifier = new QSocketNotifier(
        static_cast<qintptr>(_serialFd), QSocketNotifier::Read, this);
    connect(_readNotifier,
            &QSocketNotifier::activated,
            this,
            [this]() {
                _readAvailable();
            });

    const ChannelRequestResult requestResult =
        _sendChannelRequest(kChannelFrequencyCode20Hz);
    if (requestResult != ChannelRequestResult::Succeeded) {
        const char *reason = "request-write-failed";
        QString message =
            tr("Failed to request UniRC channel data from %1.")
                .arg(_openedDevicePath);
        if (requestResult == ChannelRequestResult::OutputQueueTimedOut) {
            reason = "request-output-queue-timeout";
            message =
                tr("The three UniRC request frames were queued on %1, but the UART output queue did not empty within %2 ms (%3).")
                    .arg(_openedDevicePath)
                    .arg(kOutputQueueTimeoutMs)
                    .arg(_requestOutputQueueEvidence);
        } else if (requestResult
                   == ChannelRequestResult::OutputQueueError) {
            reason = "request-output-queue-error";
            message =
                tr("The three UniRC request frames were queued on %1, but checking the UART output queue failed (%2).")
                    .arg(_openedDevicePath,
                         _requestOutputQueueEvidence);
        }
        _failAttempt(
            "request",
            reason,
            message,
            true);
        return false;
    }

    _requestElapsed.start();
    _serialPhase = SerialPhase::AwaitingFirstFrame;
    _setSerialOpen(true);
    _setChannelInputActive(false);
    _setLastError(QString());
    _inputWatchdog.start(kInitialFrameTimeoutMs);
    _runtimeSafetyTimer.start();
    qCDebug(UniRcChannelLog)
        << "UniRC attempt" << _activeAttemptId
        << "event request-sent"
        << "device" << _openedDevicePath
        << "frequencyCode" << static_cast<int>(kChannelFrequencyCode20Hz)
        << "requestFrames" << _requestFrameCount
        << "requestBytes" << _requestByteCount
        << "requestOutputQueue" << _requestOutputQueueEvidence
        << "requestPacket" << _lastRequestPacket.toHex(' ')
        << "termios" << configuredDetails
        << "ownership"
        << (_sharedBluetoothUart
                ? "passive-activity-check-passed"
                : "not-required");
    return true;
}

void UniRcChannelController::_runtimeSafetyCheck()
{
    if (_serialFd < 0
        || (_serialPhase != SerialPhase::AwaitingFirstFrame
            && _serialPhase != SerialPhase::Active)
        || _serialFailureScheduled) {
        _runtimeSafetyTimer.stop();
        return;
    }

    if (_sharedBluetoothUart) {
        const AndroidBluetoothSnapshot bluetooth = androidBluetoothSnapshot();
        _lastBluetoothEvidence = bluetooth.evidence;
        if (bluetooth.state
            != UniRcSerialAccessPolicy::BluetoothState::FullyOff) {
            _bluetoothFullOffElapsed.invalidate();
            _scheduleSerialFailure(
                tr("Bluetooth or BLE became active while QGC was using %1. The UART was closed without writing a stop frame.")
                    .arg(_openedDevicePath),
                "runtime-safety",
                "bluetooth-reactivated");
            return;
        }
    }

    QString configurationDetails;
    if (!_serialConfigurationMatches(_serialFd, &configurationDetails)) {
        _scheduleSerialFailure(
            tr("The configuration of %1 changed while the UniRC SDK was active. Bluetooth HAL or another process may have reclaimed the UART; QGC closed it without writing more data.")
                .arg(_openedDevicePath),
            "runtime-safety",
            "termios-drift");
    }
}

bool UniRcChannelController::_captureSerialActivity(
    int fd,
    SerialActivitySnapshot *snapshot,
    QString *error) const
{
    if (!snapshot || fd < 0) {
        if (error) {
            *error = QStringLiteral("invalid descriptor");
        }
        return false;
    }

    *snapshot = SerialActivitySnapshot {};
    struct termios configuration {};
    if (::tcgetattr(fd, &configuration) != 0) {
        if (error) {
            *error = errnoMessage(tr("Read termios for"),
                                  _openedDevicePath,
                                  errno);
        }
        return false;
    }

    int lineDiscipline = -1;
#ifdef TIOCGETD
    if (::ioctl(fd, TIOCGETD, &lineDiscipline) == 0) {
        snapshot->lineDisciplineAvailable = true;
    } else {
        snapshot->lineDisciplineError = errno;
    }
#endif
    snapshot->termiosAvailable = true;
    snapshot->inputSpeed =
        static_cast<qulonglong>(::cfgetispeed(&configuration));
    snapshot->outputSpeed =
        static_cast<qulonglong>(::cfgetospeed(&configuration));
    snapshot->lineDiscipline = lineDiscipline;
#ifdef CRTSCTS
    snapshot->hardwareFlowControl = configuration.c_cflag & CRTSCTS;
#endif
    snapshot->termiosSignature =
        QStringLiteral("ispeed=%1,ospeed=%2,cflag=0x%3,iflag=0x%4,oflag=0x%5,lflag=0x%6,line=%7")
            .arg(serialSpeedDescription(snapshot->inputSpeed))
            .arg(serialSpeedDescription(snapshot->outputSpeed))
            .arg(static_cast<qulonglong>(configuration.c_cflag), 0, 16)
            .arg(static_cast<qulonglong>(configuration.c_iflag), 0, 16)
            .arg(static_cast<qulonglong>(configuration.c_oflag), 0, 16)
            .arg(static_cast<qulonglong>(configuration.c_lflag), 0, 16)
            .arg(lineDiscipline);

#ifdef TIOCGICOUNT
    struct serial_icounter_struct counters {};
    if (::ioctl(fd, TIOCGICOUNT, &counters) == 0) {
        snapshot->countersAvailable = true;
        snapshot->rxCount = counters.rx;
        snapshot->txCount = counters.tx;
        snapshot->frameCount = counters.frame;
        snapshot->overrunCount = counters.overrun;
        snapshot->parityCount = counters.parity;
        snapshot->breakCount = counters.brk;
        snapshot->bufferOverrunCount = counters.buf_overrun;
    } else {
        const int errorNumber = errno;
        snapshot->countersError = errorNumber;
        if (!optionalTtyIoctlUnsupported(errorNumber)) {
            if (error) {
                *error = errnoMessage(
                    tr("Read UART activity counters for"),
                    _openedDevicePath,
                    errorNumber);
            }
            return false;
        }
    }
#else
    snapshot->countersError = ENOTTY;
#endif

    int queuedBytes = 0;
    if (::ioctl(fd, FIONREAD, &queuedBytes) != 0) {
        if (error) {
            *error = errnoMessage(tr("Read queued byte count for"),
                                  _openedDevicePath,
                                  errno);
        }
        return false;
    }
    snapshot->queuedBytes = queuedBytes;

    struct pollfd descriptor {
        fd, POLLIN, 0
    };
    const int pollResult = ::poll(&descriptor, 1, 0);
    if (pollResult < 0) {
        if (error) {
            *error = errnoMessage(tr("Poll"), _openedDevicePath, errno);
        }
        return false;
    }
    snapshot->readable = pollResult > 0
        && (descriptor.revents & POLLIN);
    return true;
}

bool UniRcChannelController::_drainOwnershipProbeInput(
    QByteArray *bytes,
    QString *error) const
{
    if (!bytes || _serialFd < 0) {
        if (error) {
            *error = QStringLiteral("invalid descriptor or output buffer");
        }
        return false;
    }

    bytes->clear();
    char buffer[512];
    while (bytes->size() < kOwnershipProbeReadLimitBytes) {
        const qsizetype remaining =
            kOwnershipProbeReadLimitBytes - bytes->size();
        const size_t requested = static_cast<size_t>(
            qMin<qsizetype>(remaining,
                            static_cast<qsizetype>(sizeof(buffer))));
        const ssize_t size = ::read(_serialFd, buffer, requested);
        if (size > 0) {
            bytes->append(buffer, static_cast<qsizetype>(size));
            continue;
        }
        if (size < 0 && errno == EINTR) {
            continue;
        }
        if (size == 0
            || (size < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))) {
            return true;
        }
        if (error) {
            *error = errnoMessage(tr("Read ownership probe input from"),
                                  _openedDevicePath,
                                  errno);
        }
        return false;
    }

    int remainingBytes = 0;
    if (::ioctl(_serialFd, FIONREAD, &remainingBytes) != 0) {
        if (error) {
            *error = errnoMessage(tr("Read queued byte count for"),
                                  _openedDevicePath,
                                  errno);
        }
        return false;
    }
    if (remainingBytes > 0) {
        if (error) {
            *error = QStringLiteral("probe input exceeded %1 bytes")
                .arg(kOwnershipProbeReadLimitBytes);
        }
        return false;
    }
    return true;
}

bool UniRcChannelController::_serialActivityChanged(
    const SerialActivitySnapshot &before,
    const SerialActivitySnapshot &after,
    QString *details) const
{
    QStringList changes;
    if (!before.termiosAvailable || !after.termiosAvailable) {
        changes.append(QStringLiteral("termios-unavailable"));
    } else if (before.termiosSignature != after.termiosSignature) {
        changes.append(QStringLiteral("termios-changed"));
    }

    const qint64 rxDelta = after.rxCount - before.rxCount;
    const qint64 txDelta = after.txCount - before.txCount;
    const qint64 errorDelta =
        (after.frameCount - before.frameCount)
        + (after.overrunCount - before.overrunCount)
        + (after.parityCount - before.parityCount)
        + (after.breakCount - before.breakCount)
        + (after.bufferOverrunCount - before.bufferOverrunCount);
    const UniRcSerialAccessPolicy::PassiveCounterDecision counterDecision =
        UniRcSerialAccessPolicy::evaluatePassiveCounters(
            before.countersAvailable,
            after.countersAvailable,
            rxDelta,
            txDelta,
            errorDelta);
    QString counterEvidence;
    switch (counterDecision) {
    case UniRcSerialAccessPolicy::PassiveCounterDecision::ActivityDetected:
        changes.append(
            QStringLiteral("counter-delta(rx=%1,tx=%2,error=%3)")
                .arg(rxDelta)
                .arg(txDelta)
                .arg(errorDelta));
        counterEvidence = QStringLiteral("counters=activity");
        break;
    case UniRcSerialAccessPolicy::PassiveCounterDecision::AvailabilityChanged:
        changes.append(
            QStringLiteral("counter-availability-changed(before=%1,after=%2)")
                .arg(before.countersAvailable)
                .arg(after.countersAvailable));
        counterEvidence = QStringLiteral("counters=availability-changed");
        break;
    case UniRcSerialAccessPolicy::PassiveCounterDecision::StableWithCounters:
        counterEvidence = QStringLiteral("counters=stable");
        break;
    case UniRcSerialAccessPolicy::PassiveCounterDecision::StableWithoutCounters:
        counterEvidence = QStringLiteral("counters=unavailable-optional");
        break;
    }

    if (before.queuedBytes > 0 || after.queuedBytes > 0
        || before.readable || after.readable) {
        changes.append(
            QStringLiteral("pending-input(before=%1/%2,after=%3/%4)")
                .arg(before.queuedBytes)
                .arg(before.readable)
                .arg(after.queuedBytes)
                .arg(after.readable));
    }
    if (details) {
        *details = changes.isEmpty()
            ? QStringLiteral("idle,%1").arg(counterEvidence)
            : changes.join(QLatin1Char(','));
    }
    return !changes.isEmpty();
}

bool UniRcChannelController::_serialLooksReleased(
    const SerialActivitySnapshot &snapshot,
    QString *details) const
{
    QStringList blockers;
    if (!snapshot.termiosAvailable) {
        blockers.append(QStringLiteral("termios-unavailable"));
    }
    if (!snapshot.lineDisciplineAvailable) {
        blockers.append(QStringLiteral("line-discipline-unavailable"));
    } else if (snapshot.lineDiscipline != kNTtyLineDiscipline) {
        blockers.append(
            QStringLiteral("line-discipline=%1")
                .arg(snapshot.lineDiscipline));
    }

    const qint64 inputBaud = sdkSafeIdleBaudRate(snapshot.inputSpeed);
    const qint64 outputBaud = sdkSafeIdleBaudRate(snapshot.outputSpeed);
    const bool idleSpeed =
        UniRcSerialAccessPolicy::isSdkSafeIdleBaudPair(inputBaud, outputBaud);
    if (snapshot.termiosAvailable && !idleSpeed) {
        blockers.append(
            QStringLiteral("unexpected-speed(in=%1,out=%2)")
                .arg(serialSpeedDescription(snapshot.inputSpeed))
                .arg(serialSpeedDescription(snapshot.outputSpeed)));
    }
    if (snapshot.hardwareFlowControl) {
        blockers.append(QStringLiteral("hardware-flow-control=on"));
    }

    if (details) {
        *details = blockers.isEmpty()
            ? QStringLiteral("n_tty,idle-speed=%1,no-hw-flow")
                  .arg(serialSpeedDescription(snapshot.inputSpeed))
            : blockers.join(QLatin1Char(','));
    }
    return blockers.isEmpty();
}

bool UniRcChannelController::_serialConfigurationMatches(
    int fd,
    QString *details) const
{
    struct termios configuration {};
    if (fd < 0 || ::tcgetattr(fd, &configuration) != 0) {
        if (details) {
            *details = fd < 0
                ? QStringLiteral("fd-closed")
                : QStringLiteral("tcgetattr-errno-%1").arg(errno);
        }
        return false;
    }

    const bool speedMatches =
        ::cfgetispeed(&configuration) == B115200
        && ::cfgetospeed(&configuration) == B115200;
    const bool dataMatches =
        (configuration.c_cflag & CSIZE) == CS8
        && !(configuration.c_cflag & PARENB)
        && !(configuration.c_cflag & CSTOPB)
        && (configuration.c_cflag & CLOCAL)
        && (configuration.c_cflag & CREAD);
    bool flowMatches = true;
#ifdef CRTSCTS
    flowMatches = !(configuration.c_cflag & CRTSCTS);
#endif
    const bool inputRawMatches =
        !(configuration.c_iflag
          & (IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR
             | ICRNL | IXON));
    const bool outputRawMatches = !(configuration.c_oflag & OPOST);
    const bool localRawMatches =
        !(configuration.c_lflag
          & (ECHO | ECHONL | ICANON | ISIG | IEXTEN));
    const bool readTimingMatches =
        configuration.c_cc[VMIN] == 0
        && configuration.c_cc[VTIME] == 0;
    int lineDiscipline = -1;
    bool lineDisciplineMatches = false;
#ifdef TIOCGETD
    lineDisciplineMatches =
        ::ioctl(fd, TIOCGETD, &lineDiscipline) == 0
        && lineDiscipline == kNTtyLineDiscipline;
#endif
    if (details) {
        *details = QStringLiteral("speed=%1,data8n1=%2,inputRaw=%3,outputRaw=%4,localRaw=%5,readTiming=%6,hwflowOff=%7,n_tty=%8(line=%9)")
            .arg(speedMatches)
            .arg(dataMatches)
            .arg(inputRawMatches)
            .arg(outputRawMatches)
            .arg(localRawMatches)
            .arg(readTimingMatches)
            .arg(flowMatches)
            .arg(lineDisciplineMatches)
            .arg(lineDiscipline);
    }
    return speedMatches && dataMatches && inputRawMatches
        && outputRawMatches && localRawMatches && readTimingMatches
        && flowMatches && lineDisciplineMatches;
}

bool UniRcChannelController::_safeToWriteDisableRequest() const
{
    if (_serialFd < 0
        || _serialFailureScheduled
        || _serialPhase != SerialPhase::Active
        || _channelFrameCount == 0) {
        return false;
    }
    if (_sharedBluetoothUart) {
        const AndroidBluetoothSnapshot bluetooth = androidBluetoothSnapshot();
        if (bluetooth.state
            != UniRcSerialAccessPolicy::BluetoothState::FullyOff) {
            return false;
        }
    }
    QString configurationDetails;
    return _serialConfigurationMatches(_serialFd, &configurationDetails);
}

void UniRcChannelController::_startAttempt(
    const QString &devicePath,
    const QString &bluetoothEvidence)
{
    _activeAttemptId = ++_attemptSequence;
    _attemptElapsed.start();
    _requestElapsed.invalidate();
    _openedDevicePath = devicePath;
    _serialPhase = SerialPhase::Idle;
    _parser.reset();
    _resetReceiveDiagnostics();
    _channelPolicy.reset();
    _acceptedZoomDirection = 0;
    _zoomStartRetryElapsed.invalidate();
    _firstRxLogged = false;
    _ttyExclusiveClaimed = false;
    qCDebug(UniRcChannelLog)
        << "UniRC attempt" << _activeAttemptId
        << "event begin"
        << "device" << devicePath
        << "bluetooth" << bluetoothEvidence;
}

void UniRcChannelController::_failAttempt(const char *stage,
                                          const char *reason,
                                          const QString &message,
                                          bool closeDescriptor)
{
    _logAttemptFailure(stage, reason, message);
    if (closeDescriptor) {
        _closeSerial(false, reason);
    } else {
        _serialPhase = SerialPhase::Idle;
        _activeAttemptId = 0;
        _attemptElapsed.invalidate();
        _openedDevicePath.clear();
        _sharedBluetoothUart = false;
    }
    _setLastError(message);
    _nextRetryDelayMs = kFailureRetryDelayMs;
    if (_shouldRun() && !_reconnectTimer.isActive()) {
        _reconnectTimer.start(_nextRetryDelayMs);
    }
}

void UniRcChannelController::_logPreflightTransition(
    const QString &key,
    const QString &message,
    const QString &evidence,
    bool warning)
{
    const QString transitionKey = key + QLatin1Char('/') + evidence;
    if (_lastPreflightLogKey == transitionKey) {
        return;
    }
    _lastPreflightLogKey = transitionKey;
    if (warning) {
        qCWarning(UniRcChannelLog)
            << "UniRC preflight"
            << "state" << key
            << "message" << message
            << "evidence" << evidence;
    } else {
        qCDebug(UniRcChannelLog)
            << "UniRC preflight"
            << "state" << key
            << "message" << message
            << "evidence" << evidence;
    }
}

void UniRcChannelController::_logAttemptFailure(
    const char *stage,
    const char *reason,
    const QString &message)
{
    const QString key = QString::fromLatin1(stage)
        + QLatin1Char('/') + QString::fromLatin1(reason)
        + QLatin1Char('/') + _openedDevicePath
        + QLatin1Char('/') + message
        + QLatin1Char('/') + _lastBluetoothEvidence;
    if (_lastFailureLogKey == key
        && _failureLogElapsed.isValid()
        && _failureLogElapsed.elapsed() < kRepeatedFailureLogMs) {
        ++_suppressedFailureCount;
        return;
    }

    const quint64 suppressed = _lastFailureLogKey == key
        ? _suppressedFailureCount
        : 0;
    _suppressedFailureCount = 0;
    _lastFailureLogKey = key;
    _failureLogElapsed.restart();
    qCWarning(UniRcChannelLog)
        << "UniRC attempt" << _activeAttemptId
        << "event end"
        << "stage" << stage
        << "reason" << reason
        << "elapsedMs"
        << (_attemptElapsed.isValid() ? _attemptElapsed.elapsed() : -1)
        << "device" << _openedDevicePath
        << "rxBytes" << _receivedByteCount
        << "requestFrames" << _requestFrameCount
        << "requestBytes" << _requestByteCount
        << "requestOutputQueueEmpty" << _requestOutputQueueEmpty
        << "requestOutputQueue" << _requestOutputQueueEvidence
        << "requestPacket" << _lastRequestPacket.toHex(' ')
        << "decodedFrames" << _decodedFrameCount
        << "channelFrames" << _channelFrameCount
        << "invalidChannelFrames" << _invalidChannelFrameCount
        << "lastControl" << static_cast<int>(_lastFrameControl)
        << "lastCommand" << static_cast<int>(_lastFrameCommand)
        << "lastPayloadBytes" << _lastFramePayloadSize
        << "rxSample" << _receiveSample.toHex(' ')
        << "bluetooth" << _lastBluetoothEvidence
        << "suppressedSameFailure" << suppressed
        << "message" << message;
}
#endif

bool UniRcChannelController::_configureSerial(int fd,
                                              const QString &devicePath)
{
#ifndef Q_OS_ANDROID
    Q_UNUSED(fd);
    Q_UNUSED(devicePath);
    return false;
#else
    struct termios configuration {};
    if (::tcgetattr(fd, &configuration) != 0) {
        const int errorNumber = errno;
        _setLastError(errnoMessage(tr("Read termios for"),
                                   devicePath,
                                   errorNumber));
        return false;
    }

    ::cfmakeraw(&configuration);
    configuration.c_cflag &= ~(PARENB | CSTOPB | CSIZE);
    configuration.c_cflag |= CS8 | CLOCAL | CREAD;
#ifdef CRTSCTS
    configuration.c_cflag &= ~CRTSCTS;
#endif
    configuration.c_cc[VMIN] = 0;
    configuration.c_cc[VTIME] = 0;
    if (::cfsetispeed(&configuration, B115200) != 0
        || ::cfsetospeed(&configuration, B115200) != 0
        || ::tcsetattr(fd, TCSANOW, &configuration) != 0) {
        const int errorNumber = errno;
        _setLastError(errnoMessage(tr("Configure"),
                                   devicePath,
                                   errorNumber));
        return false;
    }

    if (::tcflush(fd, TCIOFLUSH) != 0) {
        const int errorNumber = errno;
        _setLastError(errnoMessage(tr("Flush"),
                                   devicePath,
                                   errorNumber));
        return false;
    }
    return true;
#endif
}

void UniRcChannelController::_closeSerial(bool sendDisableRequest,
                                          const char *reason)
{
    _inputWatchdog.stop();
    _resetInput(false);

#ifdef Q_OS_ANDROID
    _ownershipProbeTimer.stop();
    _runtimeSafetyTimer.stop();
    if (_readNotifier) {
        _readNotifier->setEnabled(false);
        delete _readNotifier;
        _readNotifier = nullptr;
    }

    if (_serialFd >= 0) {
        const bool writeDisable =
            sendDisableRequest && _safeToWriteDisableRequest();
        if (writeDisable) {
            (void) _sendChannelRequest(0);
        } else if (sendDisableRequest) {
            qCDebug(UniRcChannelLog)
                << "UniRC attempt" << _activeAttemptId
                << "event disable-request-skipped"
                << "reason" << reason
                << "phase" << static_cast<int>(_serialPhase)
                << "channelFrames" << _channelFrameCount;
        }
#ifdef TIOCNXCL
        if (_ttyExclusiveClaimed) {
            (void) ::ioctl(_serialFd, TIOCNXCL);
        }
#endif
        (void) ::flock(_serialFd, LOCK_UN);
        (void) ::close(_serialFd);
        _serialFd = -1;
    }
    _ttyExclusiveClaimed = false;
    _sharedBluetoothUart = false;
    if (_activeAttemptId != 0) {
        qCDebug(UniRcChannelLog)
            << "UniRC attempt" << _activeAttemptId
            << "event descriptor-closed"
            << "reason" << reason;
    }
    _serialPhase = SerialPhase::Idle;
    _activeAttemptId = 0;
    _attemptElapsed.invalidate();
    _requestElapsed.invalidate();
    _bluetoothFullOffElapsed.invalidate();
#else
    Q_UNUSED(sendDisableRequest);
    Q_UNUSED(reason);
#endif

    _openedDevicePath.clear();
    _parser.reset();
    _setSerialOpen(false);
}

bool UniRcChannelController::_writeAll(const QByteArray &bytes, int timeoutMs)
{
#ifndef Q_OS_ANDROID
    Q_UNUSED(bytes);
    Q_UNUSED(timeoutMs);
    return false;
#else
    if (_serialFd < 0 || bytes.isEmpty()) {
        return false;
    }

    QElapsedTimer elapsed;
    elapsed.start();
    qsizetype offset = 0;
    while (offset < bytes.size()) {
        const ssize_t written =
            ::write(_serialFd,
                    bytes.constData() + offset,
                    static_cast<size_t>(bytes.size() - offset));
        if (written > 0) {
            offset += static_cast<qsizetype>(written);
            continue;
        }
        if (written < 0 && errno == EINTR) {
            continue;
        }
        if (written < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            const int errorNumber = errno;
            qCDebug(UniRcChannelLog)
                << "UniRC attempt" << _activeAttemptId
                << "event write-failed"
                << _openedDevicePath
                << "offset" << offset << "/" << bytes.size()
                << "errno" << errorNumber << std::strerror(errorNumber);
            return false;
        }

        const int remainingMs = timeoutMs - static_cast<int>(elapsed.elapsed());
        if (remainingMs <= 0) {
            qCDebug(UniRcChannelLog)
                << "UniRC attempt" << _activeAttemptId
                << "event write-timeout"
                << _openedDevicePath
                << "offset" << offset << "/" << bytes.size();
            return false;
        }
        struct pollfd descriptor {
            _serialFd, POLLOUT, 0
        };
        const int pollResult = ::poll(&descriptor, 1, remainingMs);
        if (pollResult < 0 && errno == EINTR) {
            continue;
        }
        if (pollResult <= 0
            || (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL))) {
            const int errorNumber = pollResult < 0 ? errno : 0;
            qCDebug(UniRcChannelLog)
                << "UniRC attempt" << _activeAttemptId
                << "event write-poll-failed"
                << _openedDevicePath
                << "result" << pollResult
                << "revents" << descriptor.revents
                << "errno" << errorNumber
                << (errorNumber ? std::strerror(errorNumber) : "none");
            return false;
        }
    }
    return true;
#endif
}

UniRcChannelController::OutputQueueResult
UniRcChannelController::_waitForSerialOutputQueueEmpty(int timeoutMs,
                                                       QString *details)
{
#ifndef Q_OS_ANDROID
    Q_UNUSED(timeoutMs);
    if (details) {
        *details = QStringLiteral("not-android");
    }
    return OutputQueueResult::Unavailable;
#else
    if (_serialFd < 0) {
        if (details) {
            *details = QStringLiteral("fd-closed");
        }
        return OutputQueueResult::TimedOut;
    }

#ifdef TIOCOUTQ
    QElapsedTimer elapsed;
    elapsed.start();
    while (true) {
        int queuedBytes = -1;
        if (::ioctl(_serialFd, TIOCOUTQ, &queuedBytes) != 0) {
            const int errorNumber = errno;
            if (details) {
                *details = QStringLiteral("tiocoutq-%1-errno-%2")
                    .arg(optionalTtyIoctlUnsupported(errorNumber)
                             ? QStringLiteral("unsupported")
                             : QStringLiteral("error"))
                    .arg(errorNumber);
            }
            return optionalTtyIoctlUnsupported(errorNumber)
                ? OutputQueueResult::Unavailable
                : OutputQueueResult::Error;
        }
        if (queuedBytes == 0) {
            if (details) {
                *details = QStringLiteral("output-queue-empty");
            }
            return OutputQueueResult::Confirmed;
        }

        const int remainingMs =
            timeoutMs - static_cast<int>(elapsed.elapsed());
        if (remainingMs <= 0) {
            if (details) {
                *details = QStringLiteral("output-queue-timeout(bytes=%1)")
                    .arg(queuedBytes);
            }
            return OutputQueueResult::TimedOut;
        }

        const int waitMs = qMin(remainingMs, 5);
        int pollResult = -1;
        do {
            pollResult = ::poll(nullptr, 0, waitMs);
        } while (pollResult < 0 && errno == EINTR);
        if (pollResult < 0) {
            const int errorNumber = errno;
            if (details) {
                *details = QStringLiteral("output-queue-wait-errno-%1")
                    .arg(errorNumber);
            }
            return OutputQueueResult::Error;
        }
    }
#else
    Q_UNUSED(timeoutMs);
    if (details) {
        *details = QStringLiteral("tiocoutq-unavailable");
    }
    return OutputQueueResult::Unavailable;
#endif
#endif
}

UniRcChannelController::ChannelRequestResult
UniRcChannelController::_sendChannelRequest(quint8 frequencyCode)
{
#ifndef Q_OS_ANDROID
    Q_UNUSED(frequencyCode);
    return ChannelRequestResult::WriteFailed;
#else
    const QByteArray packet =
        UniRcProtocol::channelDataRequestPacket(frequencyCode, 0);
    if (packet.isEmpty()) {
        return ChannelRequestResult::WriteFailed;
    }

    const bool enableRequest = frequencyCode != UniRcProtocol::FrequencyOff;
    if (enableRequest) {
        _lastRequestPacket = packet;
        _requestFrameCount = 0;
        _requestByteCount = 0;
        _requestOutputQueueEmpty = false;
        _requestOutputQueueEvidence.clear();
    }

    for (int requestIndex = 0; requestIndex < 3; ++requestIndex) {
        if (!_writeAll(packet, kWriteTimeoutMs)) {
            qCDebug(UniRcChannelLog)
                << "UniRC attempt" << _activeAttemptId
                << "event request-frame-failed"
                << requestIndex + 1 << "/3"
                << "frequencyCode" << static_cast<int>(frequencyCode)
                << "packet" << packet.toHex(' ');
            return ChannelRequestResult::WriteFailed;
        }
        if (enableRequest) {
            ++_requestFrameCount;
            _requestByteCount += static_cast<quint64>(packet.size());
        }
        qCDebug(UniRcChannelLog)
            << "UniRC attempt" << _activeAttemptId
            << "event request-frame-queued"
            << requestIndex + 1 << "/3"
            << "frequencyCode" << static_cast<int>(frequencyCode)
            << "bytes" << packet.size()
            << "packet" << packet.toHex(' ');
    }

    QString outputQueueDetails;
    const OutputQueueResult outputQueueResult =
        _waitForSerialOutputQueueEmpty(kOutputQueueTimeoutMs,
                                       &outputQueueDetails);
    if (outputQueueResult == OutputQueueResult::TimedOut) {
        if (enableRequest) {
            _requestOutputQueueEvidence =
                QStringLiteral("timed-out:%1").arg(outputQueueDetails);
        }
        qCDebug(UniRcChannelLog)
            << "UniRC attempt" << _activeAttemptId
            << "event request-output-queue-timeout"
            << "device" << _openedDevicePath
            << "details" << outputQueueDetails;
        return ChannelRequestResult::OutputQueueTimedOut;
    }
    if (outputQueueResult == OutputQueueResult::Error) {
        if (enableRequest) {
            _requestOutputQueueEvidence =
                QStringLiteral("error:%1").arg(outputQueueDetails);
        }
        qCDebug(UniRcChannelLog)
            << "UniRC attempt" << _activeAttemptId
            << "event request-output-queue-error"
            << "device" << _openedDevicePath
            << "details" << outputQueueDetails;
        return ChannelRequestResult::OutputQueueError;
    }
    if (enableRequest) {
        _requestOutputQueueEmpty =
            outputQueueResult == OutputQueueResult::Confirmed;
        _requestOutputQueueEvidence =
            QStringLiteral("%1:%2")
                .arg(outputQueueResult == OutputQueueResult::Confirmed
                         ? QStringLiteral("confirmed")
                         : QStringLiteral("unavailable"),
                     outputQueueDetails);
    }
    qCDebug(UniRcChannelLog)
        << "UniRC attempt" << _activeAttemptId
        << "event"
        << (outputQueueResult == OutputQueueResult::Confirmed
                ? "request-output-queue-empty"
                : "request-output-queue-unavailable")
        << "frequencyCode" << static_cast<int>(frequencyCode)
        << "frames" << 3
        << "bytes" << packet.size() * 3
        << "details" << outputQueueDetails
        << "packet" << packet.toHex(' ');
    return ChannelRequestResult::Succeeded;
#endif
}

void UniRcChannelController::_readAvailable()
{
#ifndef Q_OS_ANDROID
    return;
#else
    if (_serialFd < 0 || _serialFailureScheduled || _shuttingDown) {
        return;
    }

    QByteArray incoming;
    incoming.reserve(2048);
    char buffer[512];
    for (int readCount = 0; readCount < 16; ++readCount) {
        const ssize_t size = ::read(_serialFd, buffer, sizeof(buffer));
        if (size > 0) {
            incoming.append(buffer, static_cast<qsizetype>(size));
            continue;
        }
        if (size < 0 && errno == EINTR) {
            continue;
        }
        if (size == 0
            || (size < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))) {
            break;
        }

        const int errorNumber = errno;
        _scheduleSerialFailure(
            errnoMessage(tr("Read"), _openedDevicePath, errorNumber),
            "read",
            "read-failed");
        return;
    }

    if (incoming.isEmpty()) {
        return;
    }

    if (!_firstRxLogged) {
        _firstRxLogged = true;
        qCDebug(UniRcChannelLog)
            << "UniRC attempt" << _activeAttemptId
            << "event first-rx"
            << "requestElapsedMs"
            << (_requestElapsed.isValid() ? _requestElapsed.elapsed() : -1)
            << "attemptElapsedMs"
            << (_attemptElapsed.isValid() ? _attemptElapsed.elapsed() : -1)
            << "chunkBytes" << incoming.size();
    }
    _receivedByteCount += static_cast<quint64>(incoming.size());
    if (_receiveSample.size() < kReceiveSampleMaxBytes) {
        _receiveSample.append(
            incoming.left(kReceiveSampleMaxBytes - _receiveSample.size()));
    }
    const QList<UniRcProtocol::DecodedPacket> packets = _parser.append(incoming);
    _decodedFrameCount += static_cast<quint64>(packets.size());
    for (const UniRcProtocol::DecodedPacket &packet : packets) {
        _lastFrameControl = packet.control;
        _lastFrameCommand = packet.command;
        _lastFramePayloadSize = packet.payload.size();
        _handleChannelPacket(packet);
    }
#endif
}

void UniRcChannelController::_handleChannelPacket(
    const UniRcProtocol::DecodedPacket &packet)
{
    if (_serialFailureScheduled || _shuttingDown) {
        return;
    }
    if (!_gimbalControlManager || !_gimbalCenterCoordinator) {
        _scheduleSerialFailure(
            tr("UniRC gimbal control dependencies are unavailable."),
            "dispatch",
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
#ifdef Q_OS_ANDROID
    if (_serialPhase != SerialPhase::Active) {
        _serialPhase = SerialPhase::Active;
        _lastFailureLogKey.clear();
        _suppressedFailureCount = 0;
        qCInfo(UniRcChannelLog)
            << "UniRC attempt" << _activeAttemptId
            << "event stream-active"
            << "requestElapsedMs"
            << (_requestElapsed.isValid() ? _requestElapsed.elapsed() : -1)
            << "attemptElapsedMs"
            << (_attemptElapsed.isValid() ? _attemptElapsed.elapsed() : -1)
            << "device" << _openedDevicePath
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
#endif
    // A syntactically valid 0x42 frame proves that the UART/SDK route is
    // alive, even when CH9/CH10 are not mapped yet. Do not let the initial
    // watchdog overwrite that actionable mapping error with a link timeout.
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
        // Same-owner/same-direction calls do not resend 0x05; they only refresh
        // the Manager's independent 60-second safety watchdog.
        (void) _gimbalControlManager->startUniRcZoom(direction);
        return;
    }
    if (_acceptedZoomDirection == direction) {
        // The Manager may have stopped at the recording-mode endpoint. Do not
        // restart until the physical wheel passes through center or reverses.
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
#ifdef Q_OS_ANDROID
    // A busy GUI thread may delay both the notifier and this timer while
    // complete 20 Hz frames are already queued in the kernel. Drain those
    // bytes first; a valid frame restarts the single-shot watchdog and avoids
    // treating event-loop latency as a physical UART loss.
    _readAvailable();
    if (_inputWatchdog.isActive() || _serialFailureScheduled) {
        return;
    }
#endif
    _resetInput(false);
    const QString message = _receiveTimeoutMessage();
    const bool streamWasActive = _channelFrameCount > 0;
    _scheduleSerialFailure(message,
                           streamWasActive ? "rx-active" : "rx-initial",
                           streamWasActive ? "channel-stream-stopped"
                                           : "initial-response-timeout");
}

void UniRcChannelController::_resetReceiveDiagnostics()
{
    _receiveSample.clear();
    _requestOutputQueueEvidence.clear();
    _lastRequestPacket.clear();
    _receivedByteCount = 0;
    _decodedFrameCount = 0;
    _channelFrameCount = 0;
    _invalidChannelFrameCount = 0;
    _requestFrameCount = 0;
    _requestByteCount = 0;
    _requestOutputQueueEmpty = false;
    _invalidChannelWarningActive = false;
    _lastFrameControl = 0;
    _lastFrameCommand = 0;
    _lastFramePayloadSize = -1;
}

QString UniRcChannelController::_receiveTimeoutMessage() const
{
    if (_channelFrameCount > 0) {
        return tr("UniRC 0x42 channel data stopped on %1 after %2 frame(s).")
            .arg(_openedDevicePath)
            .arg(_channelFrameCount);
    }
    if (_receivedByteCount == 0) {
        return tr("QGC opened %1 and sent the UniRC request, but received no bytes. The UART2 SDK route is not active or the port is still owned by firmware/another process.")
            .arg(_openedDevicePath);
    }
    if (_decodedFrameCount == 0) {
        return tr("Received %1 serial byte(s) from %2, but no valid UniRC SDK frame; check UART2 routing and the 115200 serial format.")
            .arg(_receivedByteCount)
            .arg(_openedDevicePath);
    }
    return tr("Received %1 valid UniRC SDK frame(s) from %2, but no CTRL=0, CMD=0x42, 32-byte channel frame.")
        .arg(_decodedFrameCount)
        .arg(_openedDevicePath);
}

void UniRcChannelController::_scheduleSerialFailure(
    const QString &message,
    const char *stage,
    const char *reason)
{
    if (_serialFailureScheduled || _shuttingDown) {
        return;
    }
    _serialFailureScheduled = true;
    _inputWatchdog.stop();
#ifdef Q_OS_ANDROID
    _runtimeSafetyTimer.stop();
    if (_readNotifier) {
        // Keep deletion/close outside the activated callback, but prevent a
        // valid tail frame from re-arming CH9 or firing CH10 first.
        _readNotifier->setEnabled(false);
    }
    _resetInput(false);
    const QByteArray stageName(stage);
    const QByteArray reasonName(reason);
    const quint64 failedAttemptId = _activeAttemptId;
    _logAttemptFailure(stageName.constData(),
                       reasonName.constData(),
                       message);
    QTimer::singleShot(0, this, [this,
                                message,
                                reasonName,
                                failedAttemptId]() {
        _serialFailureScheduled = false;
        if (_shuttingDown) {
            return;
        }
        if (_activeAttemptId != failedAttemptId) {
            _reconcile();
            return;
        }
        _closeSerial(false, reasonName.constData());
        _setLastError(message);
        _nextRetryDelayMs = kFailureRetryDelayMs;
        if (_shouldRun() && !_reconnectTimer.isActive()) {
            _reconnectTimer.start(_nextRetryDelayMs);
        }
    });
#else
    Q_UNUSED(stage)
    Q_UNUSED(reason)
    _resetInput(false);
    _serialFailureScheduled = false;
    _setLastError(message);
#endif
}

void UniRcChannelController::_setSerialOpen(bool open)
{
    if (_serialOpen == open) {
        return;
    }
    _serialOpen = open;
    emit serialOpenChanged();
}

void UniRcChannelController::_setChannelInputActive(bool active)
{
    if (_channelInputActive == active) {
        return;
    }
    _channelInputActive = active;
    emit channelInputActiveChanged();
}

void UniRcChannelController::_setLastError(const QString &message)
{
    if (_lastError == message) {
        return;
    }
    _lastError = message;
    emit lastErrorChanged();
}
