/****************************************************************************
 *
 * UniRC SDK link information with independent freshness tracking.
 *
 ****************************************************************************/

#pragma once

#include "UniRcProtocol.h"

#include <QtCore/QObject>
#include <QtCore/QTimer>

class UniRcLinkStatus : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY statusChanged)
    Q_PROPERTY(int strengthPercent READ strengthPercent NOTIFY statusChanged)
    Q_PROPERTY(int strengthLevel READ strengthLevel NOTIFY statusChanged)
    Q_PROPERTY(int signalQuality READ signalQuality NOTIFY statusChanged)
    Q_PROPERTY(int qualityLevel READ qualityLevel NOTIFY statusChanged)

public:
    explicit UniRcLinkStatus(QObject *parent = nullptr);

    bool available() const { return _available; }
    int strengthPercent() const { return _strengthPercent; }
    int strengthLevel() const { return _strengthPercent / 20; }
    int signalQuality() const { return _signalQuality; }
    int qualityLevel() const;

    void update(const UniRcProtocol::LinkInfo &linkInfo);
    void invalidate();

signals:
    void statusChanged();

private:
    static constexpr int kDataTimeoutMs = 3000;

    QTimer _expiryTimer;
    int _strengthPercent = 0;
    int _signalQuality = 0;
    bool _available = false;
};
