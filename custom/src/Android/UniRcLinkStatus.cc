/****************************************************************************
 *
 * UniRC SDK link information with independent freshness tracking.
 *
 ****************************************************************************/

#include "UniRcLinkStatus.h"

UniRcLinkStatus::UniRcLinkStatus(QObject *parent)
    : QObject(parent)
{
    _expiryTimer.setSingleShot(true);
    _expiryTimer.setInterval(kDataTimeoutMs);
    _expiryTimer.setTimerType(Qt::PreciseTimer);
    connect(&_expiryTimer, &QTimer::timeout, this, &UniRcLinkStatus::invalidate);
}

int UniRcLinkStatus::qualityLevel() const
{
    // The manual's medium/strong descriptions overlap at 10; include 10
    // in the strong tier, leaving the medium tier at [5, 10).
    return _signalQuality >= 10 ? 2 : (_signalQuality >= 5 ? 1 : 0);
}

void UniRcLinkStatus::update(const UniRcProtocol::LinkInfo &linkInfo)
{
    if (linkInfo.signalStrength < 0 || linkInfo.signalStrength > 100
        || linkInfo.signalQuality < -15 || linkInfo.signalQuality > 30) {
        return;
    }

    // A repeated sample still proves freshness even when no property changes.
    _expiryTimer.start();
    const bool changed = !_available
        || _strengthPercent != linkInfo.signalStrength
        || _signalQuality != linkInfo.signalQuality;
    _available = true;
    _strengthPercent = linkInfo.signalStrength;
    _signalQuality = linkInfo.signalQuality;
    if (changed) {
        emit statusChanged();
    }
}

void UniRcLinkStatus::invalidate()
{
    _expiryTimer.stop();
    if (!_available) {
        return;
    }

    _available = false;
    _strengthPercent = 0;
    _signalQuality = 0;
    emit statusChanged();
}
