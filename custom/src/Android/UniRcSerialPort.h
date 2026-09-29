/****************************************************************************
 *
 * UniRC 10 Pro onboard UART2 transport (independent of Android USB serial).
 *
 ****************************************************************************/

#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QObject>
#include <QtCore/QString>

class QSocketNotifier;

class UniRcSerialPort : public QObject
{
    Q_OBJECT

public:
    explicit UniRcSerialPort(QObject *parent = nullptr);
    ~UniRcSerialPort() override;

    // All methods and signals belong to this object's event-loop thread.
    bool open();
    void close();
    bool isOpen() const { return _descriptor >= 0; }
    qint64 write(const QByteArray &bytes);
    QByteArray readAll();
    qint64 bytesToWrite() const { return _writeBuffer.size(); }
    QString errorString() const { return _errorString; }

signals:
    void readyRead();
    // Queued until after write() returns; counts bytes accepted by the OS.
    void bytesWritten(qint64 bytes);
    void errorOccurred();

private:
    friend class UniRcSerialPortTest;

    bool _openDevice(const char *devicePath);
    bool _flushWriteBuffer();
    void _fail(const QString &message);
    void _failSystemCall(const char *operation, int errorNumber);

    static constexpr int kMaximumWriteBufferSize = 64 * 1024;

    QSocketNotifier *_readNotifier = nullptr;
    QSocketNotifier *_writeNotifier = nullptr;
    QByteArray _writeBuffer;
    QString _errorString;
    quint64 _session = 0;
    int _descriptor = -1;
};
