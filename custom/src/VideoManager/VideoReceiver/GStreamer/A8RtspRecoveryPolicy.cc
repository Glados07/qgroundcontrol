/****************************************************************************
 * A8 RTSP session health policy.
 ****************************************************************************/

#include "A8RtspRecoveryPolicy.h"

#include <QtCore/QUrl>

namespace {
QString normalizedHost(QString host)
{
    host = host.trimmed().toLower();
    if (host.startsWith(QLatin1Char('[')) && host.endsWith(QLatin1Char(']'))) {
        host = host.mid(1, host.size() - 2);
    }
    return host;
}

qint64 ntpMilliseconds(quint64 ntp)
{
    return static_cast<qint64>((ntp >> 32) * 1000
                              + ((ntp & 0xffffffffULL) * 1000 >> 32));
}

quint32 read32(const unsigned char *p)
{
    return (quint32(p[0]) << 24) | (quint32(p[1]) << 16) | (quint32(p[2]) << 8) | p[3];
}
}

bool A8RtspRecoveryPolicy::matches(const QString &uri, const QString &a8Host, const QString &mt11Host)
{
    const QUrl url(uri.trimmed());
    const QString scheme = url.scheme().toLower();
    const QString host = normalizedHost(url.host());
    // An ambiguous endpoint is deliberately excluded, including when the
    // operator assigns both SDKs the same host. Never identify a camera by
    // Video 1/2, the .264 suffix, or the chosen MediaCodec factory.
    return url.isValid() && !host.isEmpty()
        && (scheme == QStringLiteral("rtsp") || scheme == QStringLiteral("rtsps"))
        && host == normalizedHost(a8Host) && host != normalizedHost(mt11Host);
}

bool A8RtspRecoveryPolicy::senderReport(const unsigned char *bytes, qsizetype size,
                                       quint32 ssrc, SenderReport &report)
{
    if (!bytes || size < 4 || size % 4 != 0) {
        return false;
    }
    SenderReport candidate;
    bool found = false;
    for (qsizetype offset = 0; offset < size;) {
        if (size - offset < 4) {
            return false;
        }
        const auto *p = bytes + offset;
        const qsizetype length = ((quint32(p[2]) << 8 | p[3]) + 1) * 4;
        if ((p[0] >> 6) != 2 || length > size - offset) {
            return false;
        }
        qsizetype payloadLength = length;
        if (p[0] & 0x20) {
            const quint8 padding = p[length - 1];
            if (offset + length != size || padding == 0 || padding > length - 4) {
                return false;
            }
            payloadLength -= padding;
        }
        if (p[1] == 200) {
            if (payloadLength < 28 + 24 * (p[0] & 0x1f)) {
                return false;
            }
            if (read32(p + 4) == ssrc) {
                candidate.ntp = (quint64(read32(p + 8)) << 32) | read32(p + 12);
                candidate.rtp = read32(p + 16);
                found = true;
            }
        }
        offset += length;
    }
    if (found) {
        report = candidate;
    }
    return found;
}

bool A8RtspRecoveryPolicy::clockJump(quint64 previousNtp, quint32 previousRtp,
                                    qint64 previousArrivalMs, quint64 ntp,
                                    quint32 rtp, qint64 arrivalMs)
{
    if (!previousNtp || !ntp || previousArrivalMs < 0 || arrivalMs < previousArrivalMs) {
        return false;
    }
    const qint64 elapsed = arrivalMs - previousArrivalMs;
    // Do not compare reports across a long observation gap.
    if (elapsed > 30000) {
        return false;
    }
    const qint64 ntpDelta = ntpMilliseconds(ntp) - ntpMilliseconds(previousNtp);
    const quint32 wrappedDelta = rtp - previousRtp;
    const qint64 rtpDelta = wrappedDelta <= 0x7fffffffU
        ? static_cast<qint64>(wrappedDelta)
        : static_cast<qint64>(wrappedDelta) - 0x100000000LL;
    return qAbs(ntpDelta - elapsed) > 30000
        || qAbs(rtpDelta - elapsed * 90) > 30000 * 90;
}

void A8RtspRecoveryPolicy::begin(const QString &uri, quint64 generation, qint64 nowMs)
{
    if (uri != _budgetUri) {
        _budgetUri = uri;
        _recoveries = {{-1, -1}};
    }
    _generation = generation;
    _displayed = false;
    _requested = false;
    _limitedReported = false;
    _eligibleSinceMs = nowMs;
}

void A8RtspRecoveryPolicy::displayed(quint64 generation, qint64 nowMs)
{
    if (generation != 0 && generation == _generation && !_displayed) {
        _displayed = true;
        _eligibleSinceMs = nowMs;
    }
}

void A8RtspRecoveryPolicy::stop()
{
    _generation = 0;
    _displayed = false;
    // Keep the retry budget across stop/start of the same URI.
}

A8RtspRecoveryPolicy::Action A8RtspRecoveryPolicy::evaluate(
    const Progress &progress, qint64 nowMs, bool foreground, bool recording)
{
    if (!foreground || recording) {
        _eligibleSinceMs = nowMs;
        return Action::None;
    }
    if (!_generation || !_displayed || _requested || nowMs < _eligibleSinceMs
        || !progress.rtpPackets || !progress.mediaBuffers
        || progress.lastMediaMs < 0 || progress.lastMediaMs > nowMs
        || progress.lastRtpMs < 0 || progress.lastRtpMs > nowMs) {
        return Action::None;
    }

    const bool recentJump = progress.lastClockJumpMs >= _eligibleSinceMs
        && progress.lastClockJumpMs <= nowMs && nowMs - progress.lastClockJumpMs <= 10000;
    // A single bad SR is NOT a restart reason. The fast path additionally
    // requires both raw video RTP and parsed media to have stopped.
    const bool clockStall = recentJump && nowMs - progress.lastRtpMs >= 2000;
    const qint64 thresholdMs = clockStall ? 2000 : 6000;
    if (nowMs - _eligibleSinceMs < thresholdMs || nowMs - progress.lastMediaMs < thresholdMs) {
        return Action::None;
    }

    int slot = -1;
    for (int i = 0; i < static_cast<int>(_recoveries.size()); ++i) {
        if (_recoveries[i] < 0 || nowMs - _recoveries[i] >= 60000) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        if (_limitedReported) {
            return Action::None;
        }
        _limitedReported = true;
        return Action::RateLimited;
    }
    _recoveries[slot] = nowMs;
    _requested = true;
    return clockStall ? Action::ClockJumpStall : Action::MediaStall;
}
