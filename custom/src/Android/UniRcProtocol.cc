/****************************************************************************
 *
 * UniRC 10 Pro SDK framing helpers for periodic remote-control channels.
 *
 ****************************************************************************/

#include "UniRcProtocol.h"

namespace {
constexpr quint8 kHeader0 = 0x55;
constexpr quint8 kHeader1 = 0x66;
constexpr quint8 kRequestControl = 0x01;
constexpr quint8 kPeriodicResponseControl = 0x00;
constexpr int kMinimumFrameSize = 10;
constexpr int kMaximumPayloadSize = 512;

quint8 byteAt(const QByteArray& bytes, int index)
{
    return static_cast<quint8>(bytes.at(index));
}

int headerOffset(const QByteArray& bytes, int from = 0)
{
    static const QByteArray header = QByteArray::fromHex("5566");
    return bytes.indexOf(header, from);
}
}

QByteArray UniRcProtocol::channelDataRequestPacket(quint8 frequencyCode,
                                                   quint16 sequence)
{
    if (frequencyCode > 7) {
        return QByteArray();
    }

    QByteArray packet;
    packet.reserve(kMinimumFrameSize + 1);
    packet.append(static_cast<char>(kHeader0));
    packet.append(static_cast<char>(kHeader1));
    packet.append(static_cast<char>(kRequestControl));
    _appendLittleEndian16(packet, 1);
    _appendLittleEndian16(packet, sequence);
    packet.append(static_cast<char>(CommandChannelData));
    packet.append(static_cast<char>(frequencyCode));
    _appendLittleEndian16(packet, _crc16Xmodem(packet));
    return packet;
}

UniRcProtocol::DecodedPacket UniRcProtocol::decodePacket(
    const QByteArray& packet)
{
    DecodedPacket decoded;
    if (packet.size() < kMinimumFrameSize
        || byteAt(packet, 0) != kHeader0
        || byteAt(packet, 1) != kHeader1) {
        return decoded;
    }

    const quint8 control = byteAt(packet, 2);
    if ((control & 0xfc) != 0) {
        return decoded;
    }

    const quint16 payloadLength = static_cast<quint16>(byteAt(packet, 3))
        | (static_cast<quint16>(byteAt(packet, 4)) << 8);
    if (payloadLength > kMaximumPayloadSize
        || packet.size() != kMinimumFrameSize + payloadLength) {
        return decoded;
    }

    const int crcOffset = packet.size() - 2;
    const quint16 receivedCrc = static_cast<quint16>(byteAt(packet, crcOffset))
        | (static_cast<quint16>(byteAt(packet, crcOffset + 1)) << 8);
    if (receivedCrc != _crc16Xmodem(packet.left(crcOffset))) {
        return decoded;
    }

    decoded.valid = true;
    decoded.control = control;
    decoded.sequence = static_cast<quint16>(byteAt(packet, 5))
        | (static_cast<quint16>(byteAt(packet, 6)) << 8);
    decoded.command = byteAt(packet, 7);
    decoded.payload = packet.mid(8, payloadLength);
    return decoded;
}

bool UniRcProtocol::parseChannelData(const DecodedPacket& packet,
                                     Channels* channels)
{
    if (!channels
        || !packet.valid
        || packet.control != kPeriodicResponseControl
        || packet.command != CommandChannelData
        || packet.payload.size() != ChannelCount * 2) {
        return false;
    }

    Channels parsed{};
    for (int index = 0; index < ChannelCount; ++index) {
        const int offset = index * 2;
        const quint16 raw = static_cast<quint16>(byteAt(packet.payload, offset))
            | (static_cast<quint16>(byteAt(packet.payload, offset + 1)) << 8);
        parsed[static_cast<std::size_t>(index)] = static_cast<qint16>(raw);
    }

    *channels = parsed;
    return true;
}

QList<UniRcProtocol::DecodedPacket>
UniRcProtocol::StreamParser::append(const QByteArray& bytes)
{
    QList<DecodedPacket> packets;
    if (!bytes.isEmpty()) {
        _buffer.append(bytes);
    }

    while (!_buffer.isEmpty()) {
        const int start = headerOffset(_buffer);
        if (start < 0) {
            const bool keepPossibleHeader =
                static_cast<quint8>(_buffer.back()) == kHeader0;
            _buffer = keepPossibleHeader
                ? QByteArray(1, static_cast<char>(kHeader0))
                : QByteArray();
            break;
        }
        if (start > 0) {
            _buffer.remove(0, start);
        }
        if (_buffer.size() < kMinimumFrameSize) {
            break;
        }

        const quint16 payloadLength =
            static_cast<quint16>(byteAt(_buffer, 3))
            | (static_cast<quint16>(byteAt(_buffer, 4)) << 8);
        if (payloadLength > kMaximumPayloadSize) {
            _buffer.remove(0, 1);
            continue;
        }

        const int frameSize = kMinimumFrameSize + payloadLength;
        if (_buffer.size() < frameSize) {
            // A damaged length can otherwise hold a complete later frame in
            // the buffer indefinitely. Prefer a later complete, CRC-valid
            // frame; if there is none, retain the partial candidate.
            bool resynchronized = false;
            int laterStart = headerOffset(_buffer, 1);
            while (laterStart >= 0) {
                if (_buffer.size() - laterStart < kMinimumFrameSize) {
                    break;
                }
                const quint16 laterPayloadLength =
                    static_cast<quint16>(byteAt(_buffer, laterStart + 3))
                    | (static_cast<quint16>(byteAt(_buffer, laterStart + 4)) << 8);
                const int laterFrameSize = kMinimumFrameSize
                    + laterPayloadLength;
                if (laterPayloadLength <= kMaximumPayloadSize
                    && _buffer.size() - laterStart >= laterFrameSize
                    && decodePacket(_buffer.mid(laterStart, laterFrameSize)).valid) {
                    _buffer.remove(0, laterStart);
                    resynchronized = true;
                    break;
                }
                laterStart = headerOffset(_buffer, laterStart + 1);
            }
            if (resynchronized) {
                continue;
            }
            break;
        }

        const DecodedPacket decoded = decodePacket(_buffer.left(frameSize));
        if (!decoded.valid) {
            _buffer.remove(0, 1);
            continue;
        }

        packets.append(decoded);
        _buffer.remove(0, frameSize);
    }

    return packets;
}

void UniRcProtocol::StreamParser::reset()
{
    _buffer.clear();
}

quint16 UniRcProtocol::_crc16Xmodem(const QByteArray& bytes)
{
    quint16 crc = 0;
    for (const char byte : bytes) {
        crc ^= static_cast<quint16>(static_cast<quint8>(byte)) << 8;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x8000)
                ? static_cast<quint16>((crc << 1) ^ 0x1021)
                : static_cast<quint16>(crc << 1);
        }
    }
    return crc;
}

void UniRcProtocol::_appendLittleEndian16(QByteArray& bytes, quint16 value)
{
    bytes.append(static_cast<char>(value & 0xff));
    bytes.append(static_cast<char>((value >> 8) & 0xff));
}
