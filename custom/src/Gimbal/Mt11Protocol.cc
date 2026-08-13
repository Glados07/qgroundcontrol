/****************************************************************************
 *
 * UniPod MT11 SDK wire-protocol helpers.
 * Protocol source: UniPod MT11 SDK V0.1.0, sections 1, 2, 4 and 5.
 *
 ****************************************************************************/

#include "Mt11Protocol.h"

#include <QtCore/QtMath>

namespace {

constexpr quint8 kHeader0 = 0x55;
constexpr quint8 kHeader1 = 0x66;
constexpr quint8 kRequestControl = 0x01;
constexpr quint8 kAckControl = 0x02;
constexpr quint8 kTakePhotoFunction = 0;
constexpr quint8 kToggleVideoRecordingFunction = 2;

bool isValidMainStream(quint8 source)
{
    return source <= Mt11Protocol::VideoSourceZoomAndWide;
}

bool isValidSubStream(quint8 source)
{
    return source == Mt11Protocol::VideoSourceZoom
        || source == Mt11Protocol::VideoSourceWide
        || source == Mt11Protocol::VideoSourceThermal
        || source == Mt11Protocol::VideoSourceNone;
}

} // namespace

QByteArray Mt11Protocol::manualZoomPacket(qint8 direction)
{
    if (direction < -1 || direction > 1) {
        return {};
    }

    QByteArray payload;
    payload.append(static_cast<char>(direction));
    return _encode(CommandManualZoom, payload);
}

QByteArray Mt11Protocol::absoluteZoomPacket(double zoomLevel)
{
    if (!qIsFinite(zoomLevel) || zoomLevel < 1.0 || zoomLevel > 30.0) {
        return {};
    }

    const int tenths = qRound(zoomLevel * 10.0);
    QByteArray payload;
    payload.append(static_cast<char>((tenths / 10) & 0xff));
    payload.append(static_cast<char>((tenths % 10) & 0xff));
    return _encode(CommandAbsoluteZoom, payload);
}

QByteArray Mt11Protocol::requestMaximumZoomPacket()
{
    return _encode(CommandMaximumZoomValue, {});
}

QByteArray Mt11Protocol::requestCurrentZoomPacket()
{
    return _encode(CommandCurrentZoomValue, {});
}

QByteArray Mt11Protocol::requestCameraSystemStatusPacket()
{
    return _encode(CommandCameraSystemInfo, {});
}

QByteArray Mt11Protocol::takePhotoPacket()
{
    return _photoAndRecordPacket(kTakePhotoFunction);
}

QByteArray Mt11Protocol::toggleVideoRecordingPacket()
{
    return _photoAndRecordPacket(kToggleVideoRecordingFunction);
}

QByteArray Mt11Protocol::requestVideoModePacket()
{
    return _encode(CommandVideoMode, {});
}

QByteArray Mt11Protocol::setThermalModePacket(bool thermalOnMainStream)
{
    // MT11 supports the two complementary single-image combinations used by
    // this UI: main zoom/sub thermal and main thermal/sub zoom.
    QByteArray payload;
    payload.append(static_cast<char>(thermalOnMainStream
                                         ? VideoSourceThermal
                                         : VideoSourceZoom));
    payload.append(static_cast<char>(thermalOnMainStream
                                         ? VideoSourceZoom
                                         : VideoSourceThermal));
    return _encode(CommandSetVideoMode, payload);
}

Mt11Protocol::DecodedPacket Mt11Protocol::decodePacket(
    const QByteArray& packet)
{
    DecodedPacket result;
    if (packet.size() < 10) {
        return result;
    }

    const auto at = [&packet](int index) {
        return static_cast<quint8>(packet.at(index));
    };
    if (at(0) != kHeader0 || at(1) != kHeader1
        || (at(2) & 0xfc) != 0) {
        return result;
    }

    const quint16 payloadLength = static_cast<quint16>(at(3))
        | (static_cast<quint16>(at(4)) << 8);
    const int expectedLength = 10 + static_cast<int>(payloadLength);
    if (packet.size() != expectedLength) {
        return result;
    }

    const quint16 packetCrc = static_cast<quint16>(at(expectedLength - 2))
        | (static_cast<quint16>(at(expectedLength - 1)) << 8);
    if (packetCrc != _crc16(packet.left(expectedLength - 2))) {
        return result;
    }

    result.valid = true;
    result.control = at(2);
    result.sequence = static_cast<quint16>(at(5))
        | (static_cast<quint16>(at(6)) << 8);
    result.command = at(7);
    result.payload = packet.mid(8, payloadLength);
    return result;
}

bool Mt11Protocol::decodeDatagram(const QByteArray& datagram,
                                  QList<DecodedPacket>* packets)
{
    if (!packets || datagram.isEmpty()) {
        return false;
    }

    QList<DecodedPacket> decodedPackets;
    int offset = 0;
    while (offset < datagram.size()) {
        if (datagram.size() - offset < 10) {
            return false;
        }
        const auto at = [&datagram, offset](int index) {
            return static_cast<quint8>(datagram.at(offset + index));
        };
        if (at(0) != kHeader0 || at(1) != kHeader1) {
            return false;
        }
        const quint16 payloadLength = static_cast<quint16>(at(3))
            | (static_cast<quint16>(at(4)) << 8);
        const int frameLength = 10 + static_cast<int>(payloadLength);
        if (frameLength > datagram.size() - offset) {
            return false;
        }

        const DecodedPacket decoded =
            decodePacket(datagram.mid(offset, frameLength));
        if (!decoded.valid) {
            return false;
        }
        decodedPackets.append(decoded);
        offset += frameLength;
    }

    *packets = decodedPackets;
    return true;
}

bool Mt11Protocol::isAckPacket(const DecodedPacket& packet)
{
    return packet.valid && packet.control == kAckControl;
}

bool Mt11Protocol::parseManualZoomAckPayload(const QByteArray& payload,
                                             double* zoomLevel)
{
    if (!zoomLevel || payload.size() != 2) {
        return false;
    }
    const quint16 tenths =
        static_cast<quint16>(static_cast<quint8>(payload.at(0)))
        | (static_cast<quint16>(static_cast<quint8>(payload.at(1))) << 8);
    const double parsedZoom = tenths / 10.0;
    if (parsedZoom < 1.0 || parsedZoom > 30.0) {
        return false;
    }
    *zoomLevel = parsedZoom;
    return true;
}

bool Mt11Protocol::parseAbsoluteZoomAckPayload(const QByteArray& payload,
                                               bool* accepted)
{
    if (!accepted || payload.size() != 1) {
        return false;
    }
    const quint8 result = static_cast<quint8>(payload.at(0));
    if (result > 1) {
        return false;
    }
    *accepted = result == 1;
    return true;
}

bool Mt11Protocol::parseZoomValuePayload(const QByteArray& payload,
                                         double minimumZoom,
                                         double maximumZoom,
                                         double* zoomLevel)
{
    if (!zoomLevel || payload.size() != 2
        || !qIsFinite(minimumZoom) || !qIsFinite(maximumZoom)
        || minimumZoom > maximumZoom) {
        return false;
    }
    const quint8 integerPart = static_cast<quint8>(payload.at(0));
    const quint8 decimalPart = static_cast<quint8>(payload.at(1));
    if (decimalPart > 9) {
        return false;
    }
    const double parsedZoom = integerPart + decimalPart / 10.0;
    if (parsedZoom < minimumZoom || parsedZoom > maximumZoom) {
        return false;
    }
    *zoomLevel = parsedZoom;
    return true;
}

bool Mt11Protocol::parseCameraSystemStatusPayload(
    const QByteArray& payload,
    CameraSystemStatus* status)
{
    if (!status || payload.size() < 8) {
        return false;
    }

    CameraSystemStatus parsed;
    parsed.hdrStatus = static_cast<quint8>(payload.at(1));
    parsed.recordingStatus = static_cast<quint8>(payload.at(3));
    parsed.gimbalMotionMode = static_cast<quint8>(payload.at(4));
    parsed.gimbalMountingDirection = static_cast<quint8>(payload.at(5));
    parsed.videoOutputStatus = static_cast<quint8>(payload.at(6));
    parsed.zoomLinkage = static_cast<quint8>(payload.at(7));
    *status = parsed;
    return true;
}

bool Mt11Protocol::parseFunctionFeedbackPayload(const QByteArray& payload,
                                                quint8* infoType)
{
    if (!infoType || payload.size() != 1) {
        return false;
    }
    const quint8 parsedType = static_cast<quint8>(payload.at(0));
    if (parsedType > 6) {
        return false;
    }
    *infoType = parsedType;
    return true;
}

bool Mt11Protocol::parseVideoModePayload(const QByteArray& payload,
                                         VideoMode* mode)
{
    if (!mode || payload.size() != 2) {
        return false;
    }
    const quint8 mainStream = static_cast<quint8>(payload.at(0));
    const quint8 subStream = static_cast<quint8>(payload.at(1));
    if (!isValidMainStream(mainStream) || !isValidSubStream(subStream)) {
        return false;
    }
    mode->mainStream = mainStream;
    mode->subStream = subStream;
    return true;
}

QByteArray Mt11Protocol::_photoAndRecordPacket(quint8 functionType)
{
    QByteArray payload;
    payload.append(static_cast<char>(functionType));
    return _encode(CommandPhotoAndRecord, payload);
}

QByteArray Mt11Protocol::_encode(Command command, const QByteArray& payload)
{
    QByteArray packet;
    packet.reserve(10 + payload.size());
    packet.append(static_cast<char>(kHeader0));
    packet.append(static_cast<char>(kHeader1));
    packet.append(static_cast<char>(kRequestControl));
    _appendLittleEndian16(packet, static_cast<quint16>(payload.size()));
    // The production SDK examples use the fixed sequence 0 for commands.
    _appendLittleEndian16(packet, 0);
    packet.append(static_cast<char>(command));
    packet.append(payload);
    _appendLittleEndian16(packet, _crc16(packet));
    return packet;
}

quint16 Mt11Protocol::_crc16(const QByteArray& bytes)
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

void Mt11Protocol::_appendLittleEndian16(QByteArray& bytes, quint16 value)
{
    bytes.append(static_cast<char>(value & 0xff));
    bytes.append(static_cast<char>((value >> 8) & 0xff));
}
