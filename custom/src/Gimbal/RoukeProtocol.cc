/****************************************************************************
 *
 * Rouke M400 JSON framing. Source: 控制命令M400.md, sections 1 and 2.
 *
 ****************************************************************************/

#include "RoukeProtocol.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonParseError>

namespace {

void setError(QString *error, const QString &reason)
{
    if (error) {
        *error = reason;
    }
}

bool validEndpoint(const QJsonValue &value)
{
    const QString endpoint = value.toString();
    return endpoint == QStringLiteral("msdk")
        || endpoint == QStringLiteral("psdk")
        || endpoint == QStringLiteral("osdk");
}

bool validateEnvelope(const QJsonObject &message, QString *error)
{
    const QString type = message.value(QStringLiteral("type")).toString();
    const QJsonValue code = message.value(QStringLiteral("code"));
    if (!validEndpoint(message.value(QStringLiteral("source")))
        || !validEndpoint(message.value(QStringLiteral("target")))
        || (type != QStringLiteral("req") && type != QStringLiteral("ack"))
        || message.value(QStringLiteral("cmd")).toString().trimmed().isEmpty()
        || !message.value(QStringLiteral("msg")).isString()
        || !code.isDouble() || (code.toDouble() != 0.0 && code.toDouble() != 1.0)
        || !message.value(QStringLiteral("data")).isObject()) {
        setError(error, QStringLiteral("Invalid Rouke JSON envelope"));
        return false;
    }
    return true;
}

} // namespace

QByteArray RoukeProtocol::encodeRequest(const QString &command, const QJsonObject &data, QString *error)
{
    return encodeFrame({
        {QStringLiteral("source"), QStringLiteral("msdk")},
        {QStringLiteral("target"), QStringLiteral("psdk")},
        {QStringLiteral("type"), QStringLiteral("req")},
        {QStringLiteral("cmd"), command},
        {QStringLiteral("msg"), QString()},
        {QStringLiteral("code"), 0},
        {QStringLiteral("data"), data},
    }, error);
}

QByteArray RoukeProtocol::encodeFrame(const QJsonObject &message, QString *error)
{
    setError(error, QString());
    if (!validateEnvelope(message, error)) {
        return {};
    }

    const QByteArray json = QJsonDocument(message).toJson(QJsonDocument::Compact);
    if (json.size() > MaxJsonLength) {
        setError(error, QStringLiteral("Rouke JSON requires %1 bytes; the LE16 length allows %2")
                           .arg(json.size()).arg(MaxJsonLength));
        return {};
    }

    QByteArray frame;
    frame.reserve(json.size() + FrameOverhead);
    frame.append(static_cast<char>(0xf0));
    frame.append(static_cast<char>(0xe0));
    // Length counts UTF-8 JSON bytes only, excluding the seven framing bytes.
    frame.append(static_cast<char>(json.size() & 0xff));
    frame.append(static_cast<char>((json.size() >> 8) & 0xff));
    frame.append(json);
    frame.append(static_cast<char>(0x00));
    frame.append(static_cast<char>(0xf1));
    frame.append(static_cast<char>(0xe1));
    return frame;
}

bool RoukeProtocol::decodeFrame(const QByteArray &frame, QJsonObject *message, QString *error)
{
    setError(error, QString());
    if (!message) {
        setError(error, QStringLiteral("Missing Rouke message output"));
        return false;
    }
    if (frame.size() < FrameOverhead || frame.size() > MaxFrameLength) {
        setError(error, QStringLiteral("Invalid Rouke frame size"));
        return false;
    }
    if (static_cast<quint8>(frame.at(0)) != 0xf0
        || static_cast<quint8>(frame.at(1)) != 0xe0
        || static_cast<quint8>(frame.at(frame.size() - 3)) != 0x00
        || static_cast<quint8>(frame.at(frame.size() - 2)) != 0xf1
        || static_cast<quint8>(frame.at(frame.size() - 1)) != 0xe1) {
        setError(error, QStringLiteral("Invalid Rouke frame header, checksum or trailer"));
        return false;
    }

    const int jsonLength = static_cast<quint8>(frame.at(2))
        | (static_cast<quint8>(frame.at(3)) << 8);
    if (jsonLength != frame.size() - FrameOverhead) {
        setError(error, QStringLiteral("Rouke JSON length does not match frame size"));
        return false;
    }
    const QByteArray json = frame.mid(4, jsonLength);
    // Qt versions differ in how permissively the JSON parser treats malformed
    // UTF-8. A lossless round trip rejects replacement of invalid byte sequences.
    if (QString::fromUtf8(json.constData(), json.size()).toUtf8() != json) {
        setError(error, QStringLiteral("Rouke JSON is not valid UTF-8"));
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setError(error, QStringLiteral("Invalid Rouke JSON object"));
        return false;
    }
    const QJsonObject decoded = document.object();
    if (!validateEnvelope(decoded, error)) {
        return false;
    }
    *message = decoded;
    return true;
}

QList<QByteArray> RoukeProtocol::splitFrame(const QByteArray &frame, QString *error)
{
    QJsonObject decoded;
    if (!decodeFrame(frame, &decoded, error)) {
        return {};
    }
    QList<QByteArray> chunks;
    chunks.reserve((frame.size() + MaxRelayDataLength - 1) / MaxRelayDataLength);
    for (int offset = 0; offset < frame.size(); offset += MaxRelayDataLength) {
        chunks.append(frame.mid(offset, MaxRelayDataLength));
    }
    return chunks;
}

QList<QJsonObject> RoukeProtocol::StreamDecoder::append(const QByteArray &bytes)
{
    const QByteArray header = QByteArray::fromHex("f0e0");
    QList<QJsonObject> messages;
    // Read bounded portions even if a caller supplies many concatenated frames.
    // After each portion the parser consumes complete frames or retains at most
    // one incomplete frame. A maximum-length complete frame always makes progress.
    qint64 offset = 0;
    while (offset < bytes.size()) {
        const int count = static_cast<int>(qMin<qint64>(MaxFrameLength - _buffer.size(), bytes.size() - offset));
        _buffer.append(bytes.constData() + offset, count);
        offset += count;

        while (!_buffer.isEmpty()) {
            const int start = static_cast<int>(_buffer.indexOf(header));
            if (start < 0) {
                // A split header may end the current chunk with F0.
                _buffer = _buffer.endsWith(static_cast<char>(0xf0)) ? header.left(1) : QByteArray();
                break;
            }
            if (start > 0) {
                _buffer.remove(0, start);
            }
            if (_buffer.size() < 4) {
                break;
            }
            const int jsonLength = static_cast<quint8>(_buffer.at(2))
                | (static_cast<quint8>(_buffer.at(3)) << 8);
            const int frameLength = jsonLength + FrameOverhead;
            // A frame interrupted in its header/length may leave the next F0 E0
            // at offset 2 or 3. Reject an impossible object prefix before waiting
            // for the apparent old length (which can become 0xe0f0).
            int jsonStart = 4;
            const int jsonEnd = qMin<int>(static_cast<int>(_buffer.size()), 4 + jsonLength);
            while (jsonStart < jsonEnd
                   && (_buffer.at(jsonStart) == ' ' || _buffer.at(jsonStart) == '\t'
                       || _buffer.at(jsonStart) == '\r' || _buffer.at(jsonStart) == '\n')) {
                ++jsonStart;
            }
            if (jsonLength == 0 || (jsonStart < jsonEnd && _buffer.at(jsonStart) != '{')) {
                _buffer.remove(0, 2);
                continue;
            }
            // Length bytes can coincidentally resemble a valid object prefix.
            // Resolve an overlapping header only with a fully validated frame;
            // never treat the valid binary length 0xe0f0 alone as a restart.
            const int overlap = static_cast<int>(_buffer.indexOf(header, 2));
            if (overlap >= 2 && overlap < 4 && _buffer.size() >= overlap + 4) {
                const int candidateLength = static_cast<quint8>(_buffer.at(overlap + 2))
                    | (static_cast<quint8>(_buffer.at(overlap + 3)) << 8);
                const int candidateSize = candidateLength + FrameOverhead;
                QJsonObject candidate;
                if (_buffer.size() >= overlap + candidateSize
                    && decodeFrame(_buffer.mid(overlap, candidateSize), &candidate)) {
                    _buffer.remove(0, overlap);
                    continue;
                }
            }
            // Raw F0 E0 cannot occur inside valid UTF-8 JSON or the trailer.
            // Search after the binary length: its two bytes CAN equal F0 E0.
            // A new header inside an unfinished frame resynchronizes after loss
            // or corruption instead of trusting the old length indefinitely.
            const int nextHeader = static_cast<int>(_buffer.indexOf(header, 4));
            if (nextHeader >= 0 && nextHeader < frameLength) {
                _buffer.remove(0, nextHeader);
                continue;
            }
            if (_buffer.size() < frameLength) {
                break;
            }
            QJsonObject object;
            if (decodeFrame(_buffer.left(frameLength), &object)) {
                messages.append(object);
                _buffer.remove(0, frameLength);
            } else {
                _buffer.remove(0, 2);
            }
        }
    }
    return messages;
}
