#pragma once

#include <QtCore/QObject>
#include <QtCore/QTimer>
#include <QtCore/QVariantMap>

#include <optional>

#include "MAVLinkLib.h"

class FuelCellTelemetry : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int systemId READ systemId WRITE setSystemId NOTIFY systemIdChanged)
    Q_PROPERTY(QVariantMap values READ values NOTIFY valuesChanged)

public:
    explicit FuelCellTelemetry(QObject *parent = nullptr);

    int systemId() const { return _systemId; }
    void setSystemId(int systemId);
    QVariantMap values() const { return _values; }

signals:
    void systemIdChanged();
    void valuesChanged();

private:
    void _receive(const mavlink_message_t &message);
    void _clear();

    QTimer _expiry;
    QVariantMap _values;
    std::optional<uint32_t> _lastSampleTime;
    int _systemId = 0;
};
