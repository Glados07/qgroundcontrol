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

#include <QtCore/QByteArray>
#include <QtCore/QCoreApplication>
#include <QtCore/QFile>
#include <QtCore/QVariant>
#include <QtGui/QGuiApplication>

#include <array>

#ifdef Q_OS_ANDROID
#include <QtCore/QSocketNotifier>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
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

    connect(&_reconnectTimer,
            &QTimer::timeout,
            this,
            &UniRcChannelController::_reconcile);
    connect(&_inputWatchdog,
            &QTimer::timeout,
            this,
            &UniRcChannelController::_inputWatchdogExpired);
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
    if (_gimbalCenterCoordinator) {
        _gimbalCenterCoordinator->cancel();
    }
    _closeSerial(true);
}

void UniRcChannelController::_settingsChanged()
{
    if (_shuttingDown) {
        return;
    }

    const QString configuredPath =
        _settings->uniRcSdkSerialPort()->rawValue().toString().trimmed();
    if (_serialOpen
        && (!_settings->uniRcChannelControlEnabled()->rawValue().toBool()
            || configuredPath != _openedDevicePath)) {
        _closeSerial(true);
    }
    _reconcile();
}

void UniRcChannelController::_applicationStateChanged(Qt::ApplicationState state)
{
    _applicationActive = state == Qt::ApplicationActive;
    if (!_applicationActive) {
        _reconnectTimer.stop();
        _inputWatchdog.stop();
        if (_gimbalCenterCoordinator) {
            _gimbalCenterCoordinator->cancel();
        }
        _closeSerial(true);
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
        _closeSerial(true);
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
        _reconnectTimer.start();
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
    if (!devicePath.startsWith(QStringLiteral("/dev/"))) {
        _setLastError(
            tr("UniRC SDK serial device must be an absolute /dev path."));
        return false;
    }

    const QByteArray nativePath = QFile::encodeName(devicePath);
    struct stat deviceStat {};
    if (::stat(nativePath.constData(), &deviceStat) == 0
        && !S_ISCHR(deviceStat.st_mode)) {
        _setLastError(tr("UniRC SDK serial path is not a character device: %1")
                          .arg(devicePath));
        return false;
    }

    const int fd = ::open(nativePath.constData(),
                          O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        const int errorNumber = errno;
        _setLastError(errnoMessage(tr("Open"), devicePath, errorNumber));
        qCWarning(UniRcChannelLog)
            << "Cannot open UniRC SDK Serial 2" << devicePath
            << "errno" << errorNumber << std::strerror(errorNumber)
            << "; verify UniGCS Serial 2 SDK assignment, DAC permission,"
               " SELinux policy and exclusive ownership";
        return false;
    }

    if (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
        const int errorNumber = errno;
        if (errorNumber == EWOULDBLOCK || errorNumber == EAGAIN) {
            _setLastError(errnoMessage(tr("Lock"), devicePath, errorNumber));
            ::close(fd);
            return false;
        }
        qCWarning(UniRcChannelLog)
            << "UniRC serial advisory lock is unsupported"
            << devicePath << errorNumber << std::strerror(errorNumber);
    }

#ifdef TIOCEXCL
    if (::ioctl(fd, TIOCEXCL) != 0) {
        const int errorNumber = errno;
        if (errorNumber == EBUSY) {
            _setLastError(errnoMessage(tr("Exclusive open"),
                                       devicePath,
                                       errorNumber));
            (void) ::flock(fd, LOCK_UN);
            ::close(fd);
            return false;
        }
        qCWarning(UniRcChannelLog)
            << "UniRC serial TIOCEXCL is unsupported"
            << devicePath << errorNumber << std::strerror(errorNumber);
    }
#endif

    if (!_configureSerial(fd, devicePath)) {
#ifdef TIOCNXCL
        (void) ::ioctl(fd, TIOCNXCL);
#endif
        (void) ::flock(fd, LOCK_UN);
        ::close(fd);
        return false;
    }

    _serialFd = fd;
    _openedDevicePath = devicePath;
    _parser.reset();
    _channelPolicy.reset();
    _acceptedZoomDirection = 0;
    _zoomStartRetryElapsed.invalidate();
    _readNotifier = new QSocketNotifier(
        static_cast<qintptr>(_serialFd), QSocketNotifier::Read, this);
    connect(_readNotifier,
            &QSocketNotifier::activated,
            this,
            [this]() {
                _readAvailable();
            });

    _setSerialOpen(true);
    _setChannelInputActive(false);
    _setLastError(QString());
    if (!_sendChannelRequest(kChannelFrequencyCode20Hz)) {
        const QString message =
            tr("Failed to request UniRC channel data from %1.")
                .arg(devicePath);
        // The enable write may have reached the controller only partially.
        // Best-effort the required three disable frames while the fd is still
        // owned, so a failed startup cannot leave periodic output enabled.
        _closeSerial(true);
        _setLastError(message);
        return false;
    }

    _inputWatchdog.start(kInitialFrameTimeoutMs);
    qCInfo(UniRcChannelLog)
        << "Opened UniRC SDK Serial 2" << devicePath
        << "115200 8N1; requested 16 channels at 20 Hz";
    return true;
#endif
}

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

    (void) ::tcflush(fd, TCIOFLUSH);
    return true;
#endif
}

void UniRcChannelController::_closeSerial(bool sendDisableRequest)
{
    _inputWatchdog.stop();
    _resetInput(false);

#ifdef Q_OS_ANDROID
    if (_readNotifier) {
        _readNotifier->setEnabled(false);
        delete _readNotifier;
        _readNotifier = nullptr;
    }

    if (_serialFd >= 0) {
        if (sendDisableRequest) {
            (void) _sendChannelRequest(0);
        }
#ifdef TIOCNXCL
        (void) ::ioctl(_serialFd, TIOCNXCL);
#endif
        (void) ::flock(_serialFd, LOCK_UN);
        (void) ::close(_serialFd);
        _serialFd = -1;
    }
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
            return false;
        }

        const int remainingMs = timeoutMs - static_cast<int>(elapsed.elapsed());
        if (remainingMs <= 0) {
            return false;
        }
        struct pollfd descriptor {
            _serialFd, POLLOUT, 0
        };
        const int pollResult = ::poll(&descriptor, 1, remainingMs);
        if (pollResult < 0 && errno == EINTR) {
            continue;
        }
        if (pollResult <= 0 || (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL))) {
            return false;
        }
    }
    return true;
#endif
}

bool UniRcChannelController::_sendChannelRequest(quint8 frequencyCode)
{
    const QByteArray packet =
        UniRcProtocol::channelDataRequestPacket(frequencyCode, 0);
    if (packet.isEmpty()) {
        return false;
    }

    // The UniRC V1.0 SDK requires both enable and disable requests to be sent
    // three consecutive times. Concatenating complete CRC-delimited frames
    // preserves that exact wire sequence without three event-loop races.
    return _writeAll(packet.repeated(3), kWriteTimeoutMs);
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
            errnoMessage(tr("Read"), _openedDevicePath, errorNumber));
        return;
    }

    if (incoming.isEmpty()) {
        return;
    }
    const QList<UniRcProtocol::DecodedPacket> packets = _parser.append(incoming);
    for (const UniRcProtocol::DecodedPacket &packet : packets) {
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
            tr("UniRC gimbal control dependencies are unavailable."));
        return;
    }

    std::array<qint16, 16> channels {};
    if (!UniRcProtocol::parseChannelData(packet, &channels)) {
        return;
    }

    const qint16 channel9 = channels.at(8);
    const qint16 channel10 = channels.at(9);
    const UniRcChannelPolicy::Result result =
        _channelPolicy.update(channel9, channel10);
    if (!result.channelsValid) {
        qCWarning(UniRcChannelLog)
            << "Rejected out-of-range UniRC channels"
            << "CH9" << channel9 << "CH10" << channel10;
        _resetInput(false);
        _setLastError(
            tr("UniRC CH9/CH10 values are outside the expected 900-2100 range."));
        return;
    }

    if (_channel9 != channel9 || _channel10 != channel10) {
        _channel9 = channel9;
        _channel10 = channel10;
        emit channelsChanged();
    }
    _setChannelInputActive(true);
    _setLastError(QString());
    _inputWatchdog.start(kActiveFrameTimeoutMs);

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
    _scheduleSerialFailure(
        tr("Timed out waiting for UniRC 0x42 channel data on %1.")
            .arg(_openedDevicePath));
}

void UniRcChannelController::_scheduleSerialFailure(const QString &message)
{
    if (_serialFailureScheduled || _shuttingDown) {
        return;
    }
    _serialFailureScheduled = true;
    _inputWatchdog.stop();
#ifdef Q_OS_ANDROID
    if (_readNotifier) {
        // Keep deletion/close outside the activated callback, but prevent a
        // valid tail frame from re-arming CH9 or firing CH10 first.
        _readNotifier->setEnabled(false);
    }
#endif
    _resetInput(false);
    QTimer::singleShot(0, this, [this, message]() {
        _serialFailureScheduled = false;
        if (_shuttingDown) {
            return;
        }
        // The protocol requires three freq=0 frames. Keep this best-effort
        // shutdown even for read/watchdog failures while the fd still exists.
        _closeSerial(true);
        _setLastError(message);
        qCWarning(UniRcChannelLog) << message;
        if (_shouldRun() && !_reconnectTimer.isActive()) {
            _reconnectTimer.start();
        }
    });
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
