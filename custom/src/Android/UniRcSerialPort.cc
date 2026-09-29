/****************************************************************************
 *
 * UniRC 10 Pro onboard UART2 transport (independent of Android USB serial).
 *
 ****************************************************************************/

#include "UniRcSerialPort.h"

#include <QtCore/QSocketNotifier>
#include <QtCore/QTimer>

#include <cerrno>
#include <cstring>

#ifdef Q_OS_UNIX
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace {
constexpr char kDevicePath[] = "/dev/ttyHS2";
}

UniRcSerialPort::UniRcSerialPort(QObject *parent)
    : QObject(parent)
{
}

UniRcSerialPort::~UniRcSerialPort()
{
    close();
}

bool UniRcSerialPort::open()
{
    return _openDevice(kDevicePath);
}

bool UniRcSerialPort::_openDevice(const char *devicePath)
{
    if (isOpen()) {
        return true;
    }

    _errorString.clear();

#ifdef Q_OS_UNIX
    int flags = O_RDWR | O_NOCTTY | O_NONBLOCK;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
    _descriptor = ::open(devicePath, flags);
    if (_descriptor < 0) {
        _failSystemCall("open", errno);
        return false;
    }

    if (::fcntl(_descriptor, F_SETFD, FD_CLOEXEC) < 0) {
        _failSystemCall("fcntl(FD_CLOEXEC)", errno);
        return false;
    }

    termios attributes{};
    if (::tcgetattr(_descriptor, &attributes) < 0) {
        _failSystemCall("tcgetattr", errno);
        return false;
    }

    // Binary, raw 115200 8N1. In particular, disable both software flow
    // control and RTS/CTS so arbitrary SDK payload bytes pass unchanged.
    attributes.c_iflag = 0;
    attributes.c_oflag = 0;
    attributes.c_lflag = 0;
    attributes.c_cflag = CS8 | CLOCAL | CREAD;
    attributes.c_cc[VMIN] = 1;
    attributes.c_cc[VTIME] = 0;
    if (::cfsetispeed(&attributes, B115200) < 0
        || ::cfsetospeed(&attributes, B115200) < 0) {
        _failSystemCall("cfsetspeed(115200)", errno);
        return false;
    }
    if (::tcsetattr(_descriptor, TCSANOW, &attributes) < 0) {
        _failSystemCall("tcsetattr(115200 8N1)", errno);
        return false;
    }
    if (::tcflush(_descriptor, TCIOFLUSH) < 0) {
        _failSystemCall("tcflush", errno);
        return false;
    }

    _readNotifier = new QSocketNotifier(_descriptor,
                                        QSocketNotifier::Read,
                                        this);
    _writeNotifier = new QSocketNotifier(_descriptor,
                                         QSocketNotifier::Write,
                                         this);
    _writeNotifier->setEnabled(false);
    connect(_readNotifier, &QSocketNotifier::activated, this, [this]() {
        emit readyRead();
    });
    connect(_writeNotifier, &QSocketNotifier::activated, this, [this]() {
        _flushWriteBuffer();
    });
    return true;
#else
    Q_UNUSED(devicePath)
    _fail(QStringLiteral("UniRC UART2 %1 (115200 8N1) requires Unix/Android")
              .arg(QString::fromLatin1(kDevicePath)));
    return false;
#endif
}

void UniRcSerialPort::close()
{
    // Invalidate queued bytesWritten callbacks before another session opens.
    ++_session;
    for (auto **notifier : {&_readNotifier, &_writeNotifier}) {
        if (*notifier) {
            (*notifier)->setEnabled(false);
            (*notifier)->disconnect(this);
            (*notifier)->deleteLater();
            *notifier = nullptr;
        }
    }

#ifdef Q_OS_UNIX
    if (_descriptor >= 0) {
        // Do not retry close on EINTR: the descriptor may already be closed.
        ::close(_descriptor);
    }
#endif
    _descriptor = -1;
    // Shutdown is best effort and never waits for the UART to drain.
    _writeBuffer.clear();
}

qint64 UniRcSerialPort::write(const QByteArray &bytes)
{
    if (!isOpen()) {
        _fail(QStringLiteral("UniRC UART2 %1 is not open")
                  .arg(QString::fromLatin1(kDevicePath)));
        return -1;
    }
    if (bytes.isEmpty()) {
        return 0;
    }
    if (bytes.size() > kMaximumWriteBufferSize - _writeBuffer.size()) {
        _fail(QStringLiteral("UniRC UART2 %1 write buffer limit exceeded")
                  .arg(QString::fromLatin1(kDevicePath)));
        return -1;
    }

    _writeBuffer.append(bytes);
    // Try immediately, including when the controller sends its final disable
    // packets just before close(). EAGAIN leaves the remainder for notifier.
    return _flushWriteBuffer() ? bytes.size() : -1;
}

QByteArray UniRcSerialPort::readAll()
{
    QByteArray result;
#ifdef Q_OS_UNIX
    if (!isOpen()) {
        return result;
    }

    char buffer[4096];
    while (true) {
        const auto received = ::read(_descriptor, buffer, sizeof(buffer));
        if (received > 0) {
            result.append(buffer, static_cast<int>(received));
            continue;
        }
        if (received == 0) {
            _fail(QStringLiteral("UniRC UART2 %1 reached end of stream")
                      .arg(QString::fromLatin1(kDevicePath)));
            break;
        }
        const int errorNumber = errno;
        if (errorNumber == EINTR) {
            continue;
        }
        if (errorNumber != EAGAIN && errorNumber != EWOULDBLOCK) {
            _failSystemCall("read", errorNumber);
        }
        break;
    }
#endif
    return result;
}

bool UniRcSerialPort::_flushWriteBuffer()
{
#ifdef Q_OS_UNIX
    qint64 written = 0;
    while (!_writeBuffer.isEmpty()) {
        const auto sent = ::write(_descriptor,
                                  _writeBuffer.constData(),
                                  static_cast<size_t>(_writeBuffer.size()));
        if (sent > 0) {
            written += sent;
            _writeBuffer.remove(0, static_cast<int>(sent));
            continue;
        }
        if (sent < 0) {
            const int errorNumber = errno;
            if (errorNumber == EINTR) {
                continue;
            }
            if (errorNumber != EAGAIN && errorNumber != EWOULDBLOCK) {
                _failSystemCall("write", errorNumber);
                return false;
            }
        }
        // A zero write or EAGAIN is retried only on write readiness.
        break;
    }

    _writeNotifier->setEnabled(!_writeBuffer.isEmpty());
    if (written > 0) {
        const quint64 session = _session;
        QTimer::singleShot(0, this, [this, written, session]() {
            if (isOpen() && session == _session) {
                emit bytesWritten(written);
            }
        });
    }
    return true;
#else
    return false;
#endif
}

void UniRcSerialPort::_fail(const QString &message)
{
    _errorString = message;
    close();
    emit errorOccurred();
}

void UniRcSerialPort::_failSystemCall(const char *operation, int errorNumber)
{
    _fail(QStringLiteral("UniRC UART2 %1: %2 failed (errno %3: %4)")
              .arg(QString::fromLatin1(kDevicePath),
                   QString::fromLatin1(operation))
              .arg(errorNumber)
              .arg(QString::fromLocal8Bit(std::strerror(errorNumber))));
}
