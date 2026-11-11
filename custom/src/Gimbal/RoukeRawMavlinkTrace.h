/****************************************************************************
 * Opt-in, bounded observation of already CRC-validated 53022 messages.
 * This helper does not accept payload commands or change parser state.
 ****************************************************************************/
#pragma once

#include "MAVLinkLib.h"
#include <QtCore/QByteArray>
#include <QtCore/QStringList>
#include <array>
#include <cstring>

class RoukeRawMavlinkTrace
{
public:
    static constexpr quint64 SampleLimit = 240;
    static constexpr int ChunkBytes = 48;
    struct Sample {
        bool log = false;
        quint64 ingress = 0;
        quint64 number = 0;
        QString match;
    };
    struct Counters {
        quint64 valid53022 = 0;
        quint64 reserveA1 = 0;
        quint64 reserveC1 = 0;
        quint64 a1Heartbeat = 0;
        quint64 c1AckTail = 0;
        quint64 peerRejected = 0;
        quint64 emitted = 0;
        quint64 suppressed = 0;
    };

    static const QByteArray &a1HeartbeatBytes()
    {
        static const QByteArray bytes = QByteArray::fromHex("f0e06f00")
            + QByteArrayLiteral("{\"cmd\":\"heartbeat\",\"code\":0,\"data\":{\"isOsdkOnline\":true},\"msg\":\"\",\"source\":\"psdk\",\"target\":\"msdk\",\"type\":\"req\"}")
            + QByteArray::fromHex("00f1e1");
        return bytes;
    }
    static const QByteArray &c1AckTailBytes()
    {
        static const QByteArray bytes = QByteArray::fromHex(
            "22736f75726365223a227073646b222c22746172676574223a226d73646b222c2274797065223a2261636b227d00f1e1");
        return bytes;
    }

    Sample observe(const mavlink_message_t &message, qint64 nowMs, bool peerMatches = true)
    {
        if (message.magic != MAVLINK_STX || message.msgid != MAVLINK_MSG_ID_VK_PAYLOAD_DATA_RELAY) return {};
        mavlink_vk_payload_data_relay_t relay{};
        mavlink_msg_vk_payload_data_relay_decode(&message, &relay);
        ++_counts.valid53022;
        if (!peerMatches) ++_counts.peerRejected;
        if (relay.reserve == 0xa1) ++_counts.reserveA1;
        if (relay.reserve == 0xc1) ++_counts.reserveC1;
        const auto exact = [&relay](const QByteArray &expected) {
            return relay.payload_addr == 0 && relay.data_len == expected.size()
                && std::memcmp(relay.data, expected.constData(), static_cast<std::size_t>(expected.size())) == 0;
        };
        const bool a1 = relay.reserve == 0xa1 && exact(a1HeartbeatBytes());
        const bool c1 = relay.reserve == 0xc1 && exact(c1AckTailBytes());
        if (a1) ++_counts.a1Heartbeat;
        if (c1) ++_counts.c1AckTail;
        const int bucket = relay.reserve == 0 ? 0 : relay.reserve == 0xa1 ? 1 : relay.reserve == 0xc1 ? 2 : 3;
        const bool log = _counts.emitted < SampleLimit
            && (_lastSamples[static_cast<std::size_t>(bucket)] < 0
                || nowMs - _lastSamples[static_cast<std::size_t>(bucket)] >= 1000);
        if (log) {
            _lastSamples[static_cast<std::size_t>(bucket)] = nowMs;
            ++_counts.emitted;
        } else ++_counts.suppressed;
        return {log, _counts.valid53022, _counts.emitted,
                a1 ? QStringLiteral("a1-heartbeat") : c1 ? QStringLiteral("c1-ack-tail") : QStringLiteral("none")};
    }

    QStringList sampleLines(const Sample &sample, const mavlink_message_t &message,
                            const QString &lane, quint64 session, const QString &context,
                            const QByteArray &wire = {}) const
    {
        if (!sample.log) return {};
        mavlink_vk_payload_data_relay_t relay{};
        mavlink_msg_vk_payload_data_relay_decode(&message, &relay);
        const int copied = qMin(int(relay.data_len), 128);
        const QString tag = QStringLiteral(" lane=%1 session=%2 sample=%3").arg(lane).arg(session).arg(sample.number);
        QStringList lines;
        lines << QStringLiteral("ROUKE_WIRE_RX") + tag + context
            + QStringLiteral(" msgid=53022 ingress=%1 sys=%2 comp=%3 seq=%4 len=%5 checksum=%6 payload=%7 reserve=%8 dataLen=%9 copiedData=%10 sampleMatch=%11 crc=ok representation=%12")
                  .arg(sample.ingress).arg(message.sysid).arg(message.compid).arg(message.seq).arg(message.len)
                  .arg(message.checksum, 4, 16, QLatin1Char('0')).arg(relay.payload_addr)
                  .arg(relay.reserve, 2, 16, QLatin1Char('0')).arg(relay.data_len).arg(copied).arg(sample.match)
                  .arg(wire.isEmpty() ? QStringLiteral("decoded") : QStringLiteral("udp-bytes"));
        const auto append = [&lines, &tag](const QString &marker, const QByteArray &bytes) {
            for (int offset = 0; offset < bytes.size(); offset += ChunkBytes) {
                lines << marker + tag + QStringLiteral(" offset=%1 hex=%2").arg(offset)
                    .arg(QString::fromLatin1(bytes.mid(offset, ChunkBytes).toHex()));
            }
        };
        append(QStringLiteral("ROUKE_WIRE_DATA"), QByteArray(reinterpret_cast<const char *>(relay.data), copied));
        // Native observers own decoded fields, not the original transport bytes.
        // Never call mavlink_msg_to_send_buffer here: it may trim trailing zeros.
        if (!wire.isEmpty()) append(QStringLiteral("ROUKE_WIRE_FRAME"), wire);
        else append(QStringLiteral("ROUKE_WIRE_PAYLOAD"), QByteArray(_MAV_PAYLOAD(&message), message.len));
        return lines;
    }

    QString summary(const QString &lane, quint64 session) const
    {
        return QStringLiteral("ROUKE_WIRE_TOTAL lane=%1 session=%2 valid53022=%3 reserveA1=%4 reserveC1=%5 a1Heartbeat=%6 c1AckTail=%7 peerRejected=%8 emitted=%9 suppressed=%10 cap=%11")
            .arg(lane).arg(session).arg(_counts.valid53022).arg(_counts.reserveA1).arg(_counts.reserveC1)
            .arg(_counts.a1Heartbeat).arg(_counts.c1AckTail).arg(_counts.peerRejected)
            .arg(_counts.emitted).arg(_counts.suppressed).arg(SampleLimit);
    }
    const Counters &counters() const { return _counts; }
    void reset() { *this = {}; }

private:
    Counters _counts;
    std::array<qint64, 4> _lastSamples{{-1, -1, -1, -1}};
};
