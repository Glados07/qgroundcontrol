#pragma once

#include <QtCore/QObject>
#include <QtCore/QTimer>
#include <QtCore/QVariantMap>
#include <QtNetwork/QUdpSocket>

#include <optional>

#include "MAVLinkLib.h"

class FuelCellTelemetry : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int systemId READ systemId WRITE setSystemId NOTIFY systemIdChanged)
    Q_PROPERTY(QVariantMap values READ values NOTIFY valuesChanged)

public:
    explicit FuelCellTelemetry(QObject *parent = nullptr);
    FuelCellTelemetry(const QHostAddress &boxAddress, quint16 boxPort, QObject *parent = nullptr);

    int systemId() const { return _systemId; }
    void setSystemId(int systemId);
    QVariantMap values() const { return _values; }

signals:
    void systemIdChanged();
    void valuesChanged();

private:
    void _requestSample();
    void _readSamples();
    void _receive(const mavlink_message_t &message);
    void _clear();

    const QHostAddress _boxAddress;
    const quint16 _boxPort;
    QUdpSocket _socket;
    QTimer _poll;
    QTimer _expiry;
    QVariantMap _values;
    std::optional<uint32_t> _lastSampleTime;
    int _systemId = 0;
};
