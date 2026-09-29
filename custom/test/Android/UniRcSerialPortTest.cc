#include "UniRcSerialPort.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QElapsedTimer>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>

#ifdef Q_OS_UNIX
#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace {
qint64 confirmedBytes(const QSignalSpy &spy)
{
    qint64 total = 0;
    for (const auto &arguments : spy) {
        total += arguments.first().toLongLong();
    }
    return total;
}
}

class UniRcSerialPortTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void closedPortHasNoPendingData();
    void unsupportedHostReportsError();
    void openFailureReportsSystemError();
    void configuresUartAndReadsBeforeNotification();
    void writeNotificationIsDeferred();
    void backpressurePreservesByteOrder();
    void closeDiscardsQueueAndStaleNotifications();
    void oversizedWriteFailsAndCloses();
    void hangupClosesAndReportsError();

private:
#ifdef Q_OS_UNIX
    QByteArray _readMaster();
    QByteArray _slavePath;
    int _master = -1;
#endif
};

void UniRcSerialPortTest::init()
{
#ifdef Q_OS_UNIX
    _master = ::posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
    QVERIFY2(_master >= 0, "Cannot create a pseudo-terminal master");
    QVERIFY(::grantpt(_master) == 0);
    QVERIFY(::unlockpt(_master) == 0);
    const char *slavePath = ::ptsname(_master);
    QVERIFY(slavePath);
    _slavePath = slavePath;
#endif
}

void UniRcSerialPortTest::cleanup()
{
#ifdef Q_OS_UNIX
    if (_master >= 0) {
        ::close(_master);
        _master = -1;
    }
#endif
}

void UniRcSerialPortTest::closedPortHasNoPendingData()
{
    UniRcSerialPort port;
    QSignalSpy errorSpy(&port, &UniRcSerialPort::errorOccurred);
    QVERIFY(!port.isOpen());
    QCOMPARE(port.bytesToWrite(), qint64(0));
    QVERIFY(port.readAll().isEmpty());
    QCOMPARE(port.write("closed"), qint64(-1));
    QCOMPARE(errorSpy.count(), 1);
    QVERIFY(port.errorString().contains(QStringLiteral("not open")));
    port.close();
    port.close();
    QCOMPARE(port.bytesToWrite(), qint64(0));
}

void UniRcSerialPortTest::unsupportedHostReportsError()
{
#ifdef Q_OS_UNIX
    QSKIP("The unsupported-platform stub is tested on non-Unix hosts");
#else
    UniRcSerialPort port;
    QSignalSpy errorSpy(&port, &UniRcSerialPort::errorOccurred);
    QVERIFY(!port.open());
    QVERIFY(!port.isOpen());
    QCOMPARE(errorSpy.count(), 1);
    QVERIFY(port.errorString().contains(QStringLiteral("/dev/ttyHS2")));
    QVERIFY(port.errorString().contains(QStringLiteral("Unix/Android")));
#endif
}

void UniRcSerialPortTest::openFailureReportsSystemError()
{
#ifdef Q_OS_UNIX
    UniRcSerialPort port;
    QSignalSpy errorSpy(&port, &UniRcSerialPort::errorOccurred);
    QVERIFY(!port._openDevice("/dev/unirc-test-missing-directory/uart"));
    QVERIFY(!port.isOpen());
    QCOMPARE(errorSpy.count(), 1);
    QVERIFY(port.errorString().contains(QStringLiteral("open failed")));
    QVERIFY(port.errorString().contains(QStringLiteral("errno %1").arg(ENOENT)));
    QVERIFY(port._openDevice(_slavePath.constData()));
    QVERIFY(port.errorString().isEmpty());
#else
    QSKIP("Requires Unix pseudo-terminals");
#endif
}

void UniRcSerialPortTest::configuresUartAndReadsBeforeNotification()
{
#ifdef Q_OS_UNIX
    UniRcSerialPort port;
    QVERIFY(port._openDevice(_slavePath.constData()));
    termios attributes{};
    QVERIFY(::tcgetattr(port._descriptor, &attributes) == 0);
    QCOMPARE(::cfgetispeed(&attributes), speed_t(B115200));
    QCOMPARE(::cfgetospeed(&attributes), speed_t(B115200));
    QCOMPARE(attributes.c_cflag & CSIZE, tcflag_t(CS8));
    QCOMPARE(attributes.c_cflag & (PARENB | CSTOPB), tcflag_t(0));
#ifdef CRTSCTS
    QCOMPARE(attributes.c_cflag & CRTSCTS, tcflag_t(0));
#endif
    QCOMPARE(attributes.c_iflag & (IXON | IXOFF | IXANY), tcflag_t(0));
    QCOMPARE(attributes.c_lflag & (ICANON | ECHO | ISIG), tcflag_t(0));
    QVERIFY(::fcntl(port._descriptor, F_GETFL) & O_NONBLOCK);

    const QByteArray payload = QByteArray::fromHex("0055660a0d1113ff7f");
    QCOMPARE(::write(_master, payload.constData(), payload.size()),
             ssize_t(payload.size()));
    QByteArray received;
    QElapsedTimer timeout;
    timeout.start();
    // Deliberately do not dispatch readyRead: watchdog polling must work too.
    while (received.size() < payload.size() && timeout.elapsed() < 1000) {
        received += port.readAll();
        QTest::qSleep(1);
    }
    QCOMPARE(received, payload);
    QVERIFY(port.isOpen());
    QVERIFY(port.readAll().isEmpty());

    // Subsequent input must also reach the ordinary event-driven reader.
    received.clear();
    QSignalSpy readySpy(&port, &UniRcSerialPort::readyRead);
    connect(&port, &UniRcSerialPort::readyRead, &port, [&]() {
        received += port.readAll();
    });
    QCOMPARE(::write(_master, payload.constData(), payload.size()),
             ssize_t(payload.size()));
    QTRY_COMPARE(received, payload);
    QVERIFY(readySpy.count() > 0);
#else
    QSKIP("Requires Unix pseudo-terminals");
#endif
}

void UniRcSerialPortTest::writeNotificationIsDeferred()
{
#ifdef Q_OS_UNIX
    UniRcSerialPort port;
    QVERIFY(port._openDevice(_slavePath.constData()));
    QSignalSpy writtenSpy(&port, &UniRcSerialPort::bytesWritten);
    const QByteArray payload = QByteArray::fromHex("55660101000000420552b0");
    QCOMPARE(port.write(payload), qint64(payload.size()));
    QCOMPARE(writtenSpy.count(), 0);
    QTRY_COMPARE(confirmedBytes(writtenSpy), qint64(payload.size()));
    QCOMPARE(_readMaster(), payload);
    QCOMPARE(port.bytesToWrite(), qint64(0));
#else
    QSKIP("Requires Unix pseudo-terminals");
#endif
}

void UniRcSerialPortTest::backpressurePreservesByteOrder()
{
#ifdef Q_OS_UNIX
    UniRcSerialPort port;
    QVERIFY(port._openDevice(_slavePath.constData()));
    QSignalSpy writtenSpy(&port, &UniRcSerialPort::bytesWritten);
    QByteArray payload(60 * 1024, '\0');
    for (int index = 0; index < payload.size(); ++index) {
        payload[index] = static_cast<char>(index % 256);
    }
    QCOMPARE(port.write(payload), qint64(payload.size()));
    QVERIFY2(port.bytesToWrite() > 0,
             "The pseudo-terminal must apply backpressure for this test");
    QCOMPARE(writtenSpy.count(), 0);

    QByteArray received;
    QElapsedTimer timeout;
    timeout.start();
    while (received.size() < payload.size() && timeout.elapsed() < 3000) {
        received += _readMaster();
        QCoreApplication::processEvents();
        QTest::qWait(1);
    }
    QCOMPARE(received, payload);
    QCOMPARE(port.bytesToWrite(), qint64(0));
    QTRY_COMPARE(confirmedBytes(writtenSpy), qint64(payload.size()));
#else
    QSKIP("Requires Unix pseudo-terminals");
#endif
}

void UniRcSerialPortTest::closeDiscardsQueueAndStaleNotifications()
{
#ifdef Q_OS_UNIX
    UniRcSerialPort port;
    QVERIFY(port._openDevice(_slavePath.constData()));
    QSignalSpy writtenSpy(&port, &UniRcSerialPort::bytesWritten);
    QCOMPARE(port.write(QByteArray(60 * 1024, 'x')), qint64(60 * 1024));
    QVERIFY(port.bytesToWrite() > 0);
    port.close();
    QVERIFY(!port.isOpen());
    QCOMPARE(port.bytesToWrite(), qint64(0));
    QVERIFY(!port._readNotifier);
    QVERIFY(!port._writeNotifier);
    _readMaster();
    QVERIFY(port._openDevice(_slavePath.constData()));
    QCOMPARE(port.write("new"), qint64(3));
    QTRY_COMPARE(confirmedBytes(writtenSpy), qint64(3));
    QCOMPARE(writtenSpy.count(), 1);
#else
    QSKIP("Requires Unix pseudo-terminals");
#endif
}

void UniRcSerialPortTest::oversizedWriteFailsAndCloses()
{
#ifdef Q_OS_UNIX
    UniRcSerialPort port;
    QVERIFY(port._openDevice(_slavePath.constData()));
    QSignalSpy errorSpy(&port, &UniRcSerialPort::errorOccurred);
    QCOMPARE(port.write(QByteArray(64 * 1024 + 1, 'x')), qint64(-1));
    QVERIFY(!port.isOpen());
    QCOMPARE(port.bytesToWrite(), qint64(0));
    QCOMPARE(errorSpy.count(), 1);
    QVERIFY(port.errorString().contains(QStringLiteral("buffer limit")));
#else
    QSKIP("Requires Unix pseudo-terminals");
#endif
}

void UniRcSerialPortTest::hangupClosesAndReportsError()
{
#ifdef Q_OS_UNIX
    UniRcSerialPort port;
    QVERIFY(port._openDevice(_slavePath.constData()));
    QSignalSpy errorSpy(&port, &UniRcSerialPort::errorOccurred);
    ::close(_master);
    _master = -1;
    QVERIFY(port.readAll().isEmpty());
    QVERIFY(!port.isOpen());
    QCOMPARE(errorSpy.count(), 1);
    QVERIFY(!port.errorString().isEmpty());
#else
    QSKIP("Requires Unix pseudo-terminals");
#endif
}

#ifdef Q_OS_UNIX
QByteArray UniRcSerialPortTest::_readMaster()
{
    QByteArray result;
    char buffer[4096];
    while (true) {
        const auto count = ::read(_master, buffer, sizeof(buffer));
        if (count > 0) {
            result.append(buffer, static_cast<int>(count));
        } else if (count < 0 && errno == EINTR) {
            continue;
        } else {
            return result;
        }
    }
}
#endif

QTEST_GUILESS_MAIN(UniRcSerialPortTest)

#include "UniRcSerialPortTest.moc"
