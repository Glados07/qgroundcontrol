/****************************************************************************
 * A8 RTSP session health policy. No decoder selection or clock adjustment.
 ****************************************************************************/

#pragma once

#include <QtCore/QString>
#include <QtCore/QtGlobal>

#include <array>

class A8RtspRecoveryPolicy
{
public:
    struct Progress {
        quint64 rtpPackets = 0;
        quint64 mediaBuffers = 0;
        qint64 lastRtpMs = -1;
        qint64 lastMediaMs = -1;
        qint64 lastClockJumpMs = -1;
    };
    enum class Action { None, ClockJumpStall, MediaStall, RateLimited };
    struct SenderReport {
        quint64 ntp = 0;
        quint32 rtp = 0;
    };

    static bool matches(const QString &uri, const QString &a8Host, const QString &mt11Host);
    static bool senderReport(const unsigned char *bytes, qsizetype size, quint32 ssrc, SenderReport &report);
    // RTCP uses NTP 32.32 and a wrapping 90 kHz RTP timestamp. All local
    // times supplied to this class must come from one monotonic clock.
    static bool clockJump(quint64 previousNtp, quint32 previousRtp, qint64 previousArrivalMs,
                          quint64 ntp, quint32 rtp, qint64 arrivalMs);

    void begin(const QString &uri, quint64 generation, qint64 nowMs);
    void displayed(quint64 generation, qint64 nowMs);
    void stop();
    Action evaluate(const Progress &progress, qint64 nowMs, bool foreground, bool recording);

private:
    QString _budgetUri;
    quint64 _generation = 0;
    bool _displayed = false;
    bool _requested = false;
    bool _limitedReported = false;
    qint64 _eligibleSinceMs = 0;
    std::array<qint64, 2> _recoveries{{-1, -1}};
};
