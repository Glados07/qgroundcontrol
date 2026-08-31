/****************************************************************************
 *
 * UniRC 10 Pro SDK framing helpers for periodic remote-control channels.
 *
 ****************************************************************************/

#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QList>
#include <QtCore/QtGlobal>

#include <array>

class UniRcProtocol
{
public:
    static constexpr quint8 CommandChannelData = 0x42;
    static constexpr quint8 FrequencyOff = 0;
    static constexpr quint8 Frequency20Hz = 5;
    static constexpr int ChannelCount = 16;

    struct DecodedPacket {
        bool valid = false;
        quint8 control = 0;
        quint16 sequence = 0;
        quint8 command = 0;
        QByteArray payload;
    };

    struct PeriodicStreamInspection {
        bool recognized = false;
        int frameCount = 0;
        int leadingBytes = 0;
        int trailingBytes = 0;
        DecodedPacket lastPacket;
    };

    using Channels = std::array<qint16, ChannelCount>;

    class StreamParser
    {
    public:
        QList<DecodedPacket> append(const QByteArray& bytes);
        void reset();

    private:
        QByteArray _buffer;
    };

    static QByteArray channelDataRequestPacket(
        quint8 frequencyCode = Frequency20Hz,
        quint16 sequence = 0);
    static DecodedPacket decodePacket(const QByteArray& packet);
    static bool parseChannelData(const DecodedPacket& packet,
                                 Channels* channels);
    static PeriodicStreamInspection inspectPeriodicChannelStream(
        const QByteArray& bytes,
        int minimumFrames = 3);

private:
    static quint16 _crc16Xmodem(const QByteArray& bytes);
    static void _appendLittleEndian16(QByteArray& bytes, quint16 value);
};
