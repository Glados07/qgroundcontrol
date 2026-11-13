/****************************************************************************
 *
 * Rouke M400 JSON framing for VK_PAYLOAD_DATA_RELAY (MAVLink 53022).
 *
 ****************************************************************************/

#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QJsonObject>
#include <QtCore/QList>
#include <QtCore/QString>

namespace RoukeProtocol {

constexpr int MaxRelayDataLength = 128;
constexpr int FrameOverhead = 7;
constexpr int MaxJsonLength = 65535;
constexpr int MaxFrameLength = MaxJsonLength + FrameOverhead;

// The LE16 length belongs to the complete UTF-8 JSON, independently of the
// 128-byte capacity of each MAVLink relay message.
QByteArray encodeRequest(const QString &command, const QJsonObject &data = {}, QString *error = nullptr);
QByteArray encodeFrame(const QJsonObject &message, QString *error = nullptr);

// Split a valid complete frame into consecutive bytes for data[128]. No extra
// fragment fields or repeated inner headers are added; data_len is each size.
QList<QByteArray> splitFrame(const QByteArray &frame, QString *error = nullptr);

// Accepts exactly one frame (without MAVLink array padding). The output remains
// unchanged on failure. Requests, acknowledgements and pushed messages share
// the documented JSON envelope; command-specific fields belong to the caller.
bool decodeFrame(const QByteArray &frame, QJsonObject *message, QString *error = nullptr);

// One ordered stream per sender and payload address. The transport owns idle
// expiry and endpoint changes. Invalid bytes are discarded up to the next
// header; this parser does not reorder, retransmit or recover missing bytes.
class StreamDecoder
{
public:
    QList<QJsonObject> append(const QByteArray &bytes);
    void reset() { _buffer.clear(); }
    bool hasPendingData() const { return !_buffer.isEmpty(); }
    int bufferedSize() const { return static_cast<int>(_buffer.size()); }

private:
    QByteArray _buffer;
};

} // namespace RoukeProtocol
