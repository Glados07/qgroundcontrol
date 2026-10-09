/****************************************************************************
 *
 * UniRC 10 Pro SDK framing helpers for channels and link information.
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
    static constexpr quint8 CommandLinkInfo = 0x44;
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

    using Channels = std::array<qint16, ChannelCount>;

    struct LinkInfo {
        double videoUpKbps = 0.0;
        double videoDownMbps = 0.0;
        quint8 channel = 0;
        qint16 signalStrength = 0;
        qint8 signalQuality = 0;
    };

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
    static QByteArray linkInfoRequestPacket(quint16 sequence = 0);
    static DecodedPacket decodePacket(const QByteArray& packet);
    static bool parseChannelData(const DecodedPacket& packet,
                                 Channels* channels);
    static bool parseLinkInfo(const DecodedPacket& packet, LinkInfo* linkInfo);
private:
    static quint16 _crc16Xmodem(const QByteArray& bytes);
    static void _appendLittleEndian16(QByteArray& bytes, quint16 value);
};
