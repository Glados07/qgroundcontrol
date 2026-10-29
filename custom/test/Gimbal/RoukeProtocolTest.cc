/****************************************************************************
 *
 * Rouke M400 framing and continuous byte-stream relay regression tests.
 *
 ****************************************************************************/

#include "RoukeProtocol.h"

#include <QtCore/QJsonDocument>
#include <QtTest/QTest>

namespace {

// Independent raw-frame fixture also permits invalid JSON and oversized frames.
QByteArray rawFrame(const QByteArray &json)
{
    QByteArray frame = QByteArray::fromHex("f0e0");
    frame.append(static_cast<char>(json.size() & 0xff));
    frame.append(static_cast<char>((json.size() >> 8) & 0xff));
    frame.append(json);
    frame.append(QByteArray::fromHex("00f1e1"));
    return frame;
}

QJsonObject envelope()
{
    return {
        {QStringLiteral("source"), QStringLiteral("msdk")},
        {QStringLiteral("target"), QStringLiteral("psdk")},
        {QStringLiteral("type"), QStringLiteral("req")},
        {QStringLiteral("cmd"), QStringLiteral("lr1_focus")},
        {QStringLiteral("msg"), QString()},
        {QStringLiteral("code"), 0},
        {QStringLiteral("data"), QJsonObject()},
    };
}

} // namespace

class RoukeProtocolTest : public QObject
{
    Q_OBJECT

private slots:
    void documentedRequest();
    void utf8AndAcknowledgement();
    void relayCapacityBoundary();
    void documentedCommandCapacity();
    void lengthFieldBoundary();
    void streamReassemblesAcrossEveryBoundary();
    void streamSplitsUtf8AndHandlesConcatenatedFrames();
    void streamResynchronizesAfterLossAndCorruption();
    void streamRecoversFromInterruptedHeader();
    void streamStorageIsBoundedAndResetClearsPartialData();
    void rejectsMalformedFrames();
    void rejectsInvalidEnvelopes();
};

void RoukeProtocolTest::documentedRequest()
{
    const QByteArray json = QByteArrayLiteral("{\"cmd\":\"lr1_record\",\"code\":0,\"data\":{\"action\":0},\"msg\":\"\",\"source\":\"msdk\",\"target\":\"psdk\",\"type\":\"req\"}");
    QString error = QStringLiteral("previous error");
    const QByteArray frame = RoukeProtocol::encodeRequest(QStringLiteral("lr1_record"),
                                                         {{QStringLiteral("action"), 0}}, &error);
    QCOMPARE(frame, rawFrame(json));
    QCOMPARE(frame.size(), 110);
    QVERIFY(error.isEmpty());

    QJsonObject decoded;
    QVERIFY(RoukeProtocol::decodeFrame(frame, &decoded, &error));
    QCOMPARE(decoded.value(QStringLiteral("data")).toObject().value(QStringLiteral("action")).toInt(), 0);
    QCOMPARE(decoded.value(QStringLiteral("source")).toString(), QStringLiteral("msdk"));
    QCOMPARE(decoded.value(QStringLiteral("target")).toString(), QStringLiteral("psdk"));
    QVERIFY(error.isEmpty());
}

void RoukeProtocolTest::utf8AndAcknowledgement()
{
    QJsonObject message = envelope();
    message.insert(QStringLiteral("source"), QStringLiteral("psdk"));
    message.insert(QStringLiteral("target"), QStringLiteral("msdk"));
    message.insert(QStringLiteral("type"), QStringLiteral("ack"));
    message.insert(QStringLiteral("code"), 1);
    message.insert(QStringLiteral("msg"), QString::fromUtf8("对焦失败"));

    const QByteArray json = QJsonDocument(message).toJson(QJsonDocument::Compact);
    const QByteArray frame = RoukeProtocol::encodeFrame(message);
    QCOMPARE(static_cast<quint8>(frame.at(2)), static_cast<quint8>(json.size()));
    QCOMPARE(static_cast<quint8>(frame.at(3)), quint8(0));
    QCOMPARE(frame.size(), json.size() + 7);
    QVERIFY(json.size() > QString::fromUtf8(json).size());
    QJsonObject decoded;
    QVERIFY(RoukeProtocol::decodeFrame(frame, &decoded));
    QCOMPARE(decoded, message);

    message.insert(QStringLiteral("source"), QStringLiteral("osdk"));
    message.insert(QStringLiteral("cmd"), QStringLiteral("heartbeat"));
    message.insert(QStringLiteral("type"), QStringLiteral("req"));
    QVERIFY(RoukeProtocol::decodeFrame(RoukeProtocol::encodeFrame(message), &decoded));
    QCOMPARE(decoded, message);
}

void RoukeProtocolTest::relayCapacityBoundary()
{
    QJsonObject message = envelope();
    const int emptySize = RoukeProtocol::encodeFrame(message).size();
    message.insert(QStringLiteral("msg"), QString(128 - emptySize, QLatin1Char('x')));
    QString error;
    const QByteArray boundary = RoukeProtocol::encodeFrame(message, &error);
    QCOMPARE(boundary.size(), 128);
    QCOMPARE(static_cast<quint8>(boundary.at(2)), quint8(121));
    QCOMPARE(static_cast<quint8>(boundary.at(3)), quint8(0));
    QJsonObject decoded;
    QVERIFY(RoukeProtocol::decodeFrame(boundary, &decoded));
    QCOMPARE(decoded, message);

    QCOMPARE(RoukeProtocol::splitFrame(boundary), QList<QByteArray>({boundary}));

    for (const int size : {129, 152, 256, 257, 1024}) {
        message.insert(QStringLiteral("msg"), QString(size - emptySize, QLatin1Char('x')));
        const QByteArray frame = RoukeProtocol::encodeFrame(message, &error);
        QCOMPARE(frame.size(), size);
        QVERIFY(error.isEmpty());
        const auto chunks = RoukeProtocol::splitFrame(frame, &error);
        QVERIFY(error.isEmpty());
        QCOMPARE(chunks.size(), (size + 127) / 128);
        QByteArray joined;
        for (int i = 0; i < chunks.size(); ++i) {
            QCOMPARE(chunks.at(i).size(), qMin(128, size - i * 128));
            joined += chunks.at(i);
        }
        QCOMPARE(joined, frame);
        QVERIFY(RoukeProtocol::decodeFrame(joined, &decoded));
        QCOMPARE(decoded, message);
        if (size == 152) {
            QCOMPARE(chunks.at(0).size(), 128);
            QCOMPARE(chunks.at(1).size(), 24);
        }
    }
    QVERIFY(RoukeProtocol::splitFrame(boundary.left(127), &error).isEmpty());
    QVERIFY(!error.isEmpty());
}

void RoukeProtocolTest::documentedCommandCapacity()
{
    // Preserve every field; a complete long command is split only after encoding.
    QString error;
    const QByteArray speedFrame = RoukeProtocol::encodeRequest(QStringLiteral("gimbal_speed_ctrl"),
        {{QStringLiteral("yaw_speed"), 10}, {QStringLiteral("pitch_speed"), 5}, {QStringLiteral("roll_speed"), 0}}, &error);
    QCOMPARE(speedFrame.size(), 152);
    QVERIFY(error.isEmpty());
    const auto speedChunks = RoukeProtocol::splitFrame(speedFrame);
    QCOMPARE(speedChunks.at(0).size(), 128);
    QCOMPARE(speedChunks.at(1).size(), 24);
    RoukeProtocol::StreamDecoder stream;
    QVERIFY(stream.append(speedChunks.at(0)).isEmpty());
    QCOMPARE(stream.bufferedSize(), 128);
    const auto messages = stream.append(speedChunks.at(1));
    QCOMPARE(messages.size(), 1);
    QCOMPARE(messages.at(0).value(QStringLiteral("cmd")).toString(), QStringLiteral("gimbal_speed_ctrl"));
    QCOMPARE(messages.at(0).value(QStringLiteral("data")).toObject().value(QStringLiteral("yaw_speed")).toInt(), 10);
    QVERIFY(!stream.hasPendingData());
    QCOMPARE(RoukeProtocol::encodeRequest(QStringLiteral("gimbal_absolute_angle"),
        {{QStringLiteral("yaw_angle"), 900}, {QStringLiteral("pitch_angle"), 450}, {QStringLiteral("roll_angle"), 0}}, &error).size(), 159);
    QCOMPARE(RoukeProtocol::encodeRequest(QStringLiteral("lr1_distance_threshold"),
        {{QStringLiteral("distance_cm"), 500}}, &error).size(), 129);

    QCOMPARE(RoukeProtocol::encodeRequest(QStringLiteral("lr1_switch_view"), {{QStringLiteral("view_type"), 4}}).size(), 118);
    QCOMPARE(RoukeProtocol::encodeRequest(QStringLiteral("gimbal_center")).size(), 103);
    QCOMPARE(RoukeProtocol::encodeRequest(QStringLiteral("lr1_white_balance"),
        {{QStringLiteral("action"), 1}, {QStringLiteral("value"), 14}}).size(), 128);
    QVERIFY(!RoukeProtocol::encodeRequest(QStringLiteral("lr1_take_photo")).isEmpty());
}

void RoukeProtocolTest::lengthFieldBoundary()
{
    QJsonObject message = envelope();
    const int emptyJsonSize = QJsonDocument(message).toJson(QJsonDocument::Compact).size();
    // 0xe0f0 is intentional: binary length bytes can equal the frame header.
    for (const int jsonLength : {0xe0f0, RoukeProtocol::MaxJsonLength}) {
        message.insert(QStringLiteral("msg"), QString(jsonLength - emptyJsonSize, QLatin1Char('x')));
        const QByteArray frame = RoukeProtocol::encodeFrame(message);
        QCOMPARE(frame.size(), jsonLength + RoukeProtocol::FrameOverhead);
        QCOMPARE(static_cast<quint8>(frame.at(2)), static_cast<quint8>(jsonLength & 0xff));
        QCOMPARE(static_cast<quint8>(frame.at(3)), static_cast<quint8>(jsonLength >> 8));
        RoukeProtocol::StreamDecoder stream;
        QList<QJsonObject> decoded;
        const auto chunks = RoukeProtocol::splitFrame(frame);
        for (const auto &chunk : chunks) {
            decoded += stream.append(chunk);
            QVERIFY(stream.bufferedSize() <= RoukeProtocol::MaxFrameLength);
        }
        QCOMPARE(decoded, QList<QJsonObject>({message}));
        QVERIFY(!stream.hasPendingData());
    }
    message.insert(QStringLiteral("msg"), QString(RoukeProtocol::MaxJsonLength + 1 - emptyJsonSize, QLatin1Char('x')));
    QString error;
    QVERIFY(RoukeProtocol::encodeFrame(message, &error).isEmpty());
    QVERIFY(error.contains(QStringLiteral("65536")));
    QJsonObject decoded;
    QVERIFY(!RoukeProtocol::decodeFrame(rawFrame(QJsonDocument(message).toJson(QJsonDocument::Compact)), &decoded));
}

void RoukeProtocolTest::streamReassemblesAcrossEveryBoundary()
{
    auto message = envelope();
    message.insert(QStringLiteral("msg"), QString(500, QLatin1Char('x')));
    const QByteArray frame = RoukeProtocol::encodeFrame(message);
    for (int boundary = 1; boundary < frame.size(); ++boundary) {
        RoukeProtocol::StreamDecoder stream;
        QVERIFY(stream.append(frame.left(boundary)).isEmpty());
        QVERIFY(stream.hasPendingData());
        QCOMPARE(stream.append(frame.mid(boundary)), QList<QJsonObject>({message}));
        QVERIFY(!stream.hasPendingData());
    }
}

void RoukeProtocolTest::streamSplitsUtf8AndHandlesConcatenatedFrames()
{
    auto message = envelope();
    message.insert(QStringLiteral("source"), QStringLiteral("psdk"));
    message.insert(QStringLiteral("target"), QStringLiteral("msdk"));
    message.insert(QStringLiteral("type"), QStringLiteral("ack"));
    message.insert(QStringLiteral("msg"), QString::fromUtf8("柔克负载返回的较长中文说明").repeated(20));
    const QByteArray frame = RoukeProtocol::encodeFrame(message);
    RoukeProtocol::StreamDecoder stream;
    QList<QJsonObject> messages;
    // Every multibyte character is deliberately spread over separate appends.
    for (const char byte : frame) {
        messages += stream.append(QByteArray(1, byte));
    }
    QCOMPARE(messages, QList<QJsonObject>({message}));
    const auto other = envelope();
    const QByteArray otherFrame = RoukeProtocol::encodeFrame(other);
    QCOMPARE(stream.append(frame + otherFrame + frame), QList<QJsonObject>({message, other, message}));
    QVERIFY(!stream.hasPendingData());
}

void RoukeProtocolTest::streamResynchronizesAfterLossAndCorruption()
{
    const auto message = envelope();
    const QByteArray frame = RoukeProtocol::encodeFrame(message);
    RoukeProtocol::StreamDecoder stream;
    QVERIFY(stream.append(QByteArrayLiteral("noise") + frame.left(1)).isEmpty());
    QCOMPARE(stream.bufferedSize(), 1);
    QCOMPARE(stream.append(frame.mid(1)), QList<QJsonObject>({message}));

    auto longMessage = message;
    longMessage.insert(QStringLiteral("msg"), QString(500, QLatin1Char('x')));
    const auto longFrame = RoukeProtocol::encodeFrame(longMessage);
    QVERIFY(stream.append(longFrame.left(128)).isEmpty());
    // Missing middle bytes plus a later full header must not block fresh frames.
    QCOMPARE(stream.append(longFrame.mid(256) + frame), QList<QJsonObject>({message}));
    QVERIFY(!stream.hasPendingData());

    auto corruptLength = frame;
    corruptLength[2] = static_cast<char>(0xff);
    corruptLength[3] = static_cast<char>(0xff);
    QVERIFY(stream.append(corruptLength).isEmpty());
    QVERIFY(stream.append(frame.left(1)).isEmpty());
    QCOMPARE(stream.append(frame.mid(1)), QList<QJsonObject>({message}));

    auto corruptTrailer = frame;
    corruptTrailer[corruptTrailer.size() - 1] = '\0';
    QCOMPARE(stream.append(corruptTrailer + rawFrame(QByteArrayLiteral("{bad}")) + frame), QList<QJsonObject>({message}));
    QVERIFY(!stream.hasPendingData());
}

void RoukeProtocolTest::streamRecoversFromInterruptedHeader()
{
    auto message = envelope();
    const auto shortFrame = RoukeProtocol::encodeFrame(message);
    const int emptyJsonSize = QJsonDocument(message).toJson(QJsonDocument::Compact).size();
    // Include lengths whose bytes resemble JSON prefixes and the frame marker.
    for (const int jsonLength : {emptyJsonSize, 0x7b20, 0x227b, 0xe0f0, RoukeProtocol::MaxJsonLength}) {
        message.insert(QStringLiteral("msg"), QString(jsonLength - emptyJsonSize, QLatin1Char('x')));
        const auto nextFrame = RoukeProtocol::encodeFrame(message);
        for (const int interruptedSize : {2, 3}) {
            RoukeProtocol::StreamDecoder stream;
            QVERIFY(stream.append(shortFrame.left(interruptedSize)).isEmpty());
            QCOMPARE(stream.append(nextFrame), QList<QJsonObject>({message}));
            QVERIFY(!stream.hasPendingData());
        }
    }
    RoukeProtocol::StreamDecoder stream;
    const auto prettyJson = QJsonDocument(envelope()).toJson(QJsonDocument::Indented);
    QCOMPARE(stream.append(rawFrame(QByteArrayLiteral(" \r\n\t") + prettyJson)), QList<QJsonObject>({envelope()}));
}

void RoukeProtocolTest::streamStorageIsBoundedAndResetClearsPartialData()
{
    const auto message = envelope();
    const QByteArray frame = RoukeProtocol::encodeFrame(message);
    RoukeProtocol::StreamDecoder stream;
    QVERIFY(stream.append(QByteArray(RoukeProtocol::MaxFrameLength * 3, 'x')).isEmpty());
    QVERIFY(!stream.hasPendingData());
    QVERIFY(stream.append(frame.left(50)).isEmpty());
    stream.reset();
    QVERIFY(!stream.hasPendingData());
    QVERIFY(stream.append(frame.mid(50)).isEmpty());
    QCOMPARE(stream.append(frame), QList<QJsonObject>({message}));

    const int count = RoukeProtocol::MaxFrameLength / frame.size() + 10;
    const auto messages = stream.append(frame.repeated(count));
    QCOMPARE(messages.size(), count);
    for (const auto &decoded : messages) QCOMPARE(decoded, message);
    QVERIFY(!stream.hasPendingData());
}

void RoukeProtocolTest::rejectsMalformedFrames()
{
    const QByteArray frame = RoukeProtocol::encodeRequest(QStringLiteral("lr1_focus"));
    const QJsonObject sentinel = {{QStringLiteral("unchanged"), true}};
    QJsonObject decoded = sentinel;
    QString error;
    for (int size = 0; size < frame.size(); ++size) {
        QVERIFY(!RoukeProtocol::decodeFrame(frame.left(size), &decoded, &error));
        QCOMPARE(decoded, sentinel);
        QVERIFY(!error.isEmpty());
    }
    for (const int index : {0, 1, 2, 3, int(frame.size() - 3), int(frame.size() - 2), int(frame.size() - 1)}) {
        QByteArray corrupt = frame;
        corrupt[index] = static_cast<char>(static_cast<quint8>(corrupt.at(index)) ^ 0x01);
        QVERIFY(!RoukeProtocol::decodeFrame(corrupt, &decoded));
        QCOMPARE(decoded, sentinel);
    }
    QVERIFY(!RoukeProtocol::decodeFrame(frame + QByteArray(1, '\0'), &decoded));
    QVERIFY(!RoukeProtocol::decodeFrame(frame + frame, &decoded));
    QVERIFY(!RoukeProtocol::decodeFrame(frame, nullptr));
    for (const QByteArray &json : {QByteArray(), QByteArrayLiteral("null"), QByteArrayLiteral("[]"), QByteArrayLiteral("{bad}"), QByteArrayLiteral("{} {}")}) {
        QVERIFY(!RoukeProtocol::decodeFrame(rawFrame(json), &decoded));
    }
    QByteArray invalidUtf8 = QJsonDocument(envelope()).toJson(QJsonDocument::Compact);
    invalidUtf8.replace(QByteArrayLiteral("\"msg\":\"\""), QByteArrayLiteral("\"msg\":\"") + QByteArray::fromHex("c080") + QByteArrayLiteral("\""));
    QVERIFY(!RoukeProtocol::decodeFrame(rawFrame(invalidUtf8), &decoded));
    QCOMPARE(decoded, sentinel);
}

void RoukeProtocolTest::rejectsInvalidEnvelopes()
{
    const QJsonObject valid = envelope();
    for (auto it = valid.constBegin(); it != valid.constEnd(); ++it) {
        QJsonObject missing = valid;
        missing.remove(it.key());
        QString error;
        QVERIFY(RoukeProtocol::encodeFrame(missing, &error).isEmpty());
        QVERIFY(!error.isEmpty());
        QJsonObject decoded;
        QVERIFY(!RoukeProtocol::decodeFrame(rawFrame(QJsonDocument(missing).toJson(QJsonDocument::Compact)), &decoded));
    }
    const QList<QPair<QString, QJsonValue>> invalid = {
        {QStringLiteral("source"), QStringLiteral("qgc")},
        {QStringLiteral("target"), 42},
        {QStringLiteral("type"), QStringLiteral("response")},
        {QStringLiteral("cmd"), QStringLiteral(" ")},
        {QStringLiteral("cmd"), 12},
        {QStringLiteral("msg"), 0},
        {QStringLiteral("code"), true},
        {QStringLiteral("code"), -1},
        {QStringLiteral("code"), 0.5},
        {QStringLiteral("code"), 2},
        {QStringLiteral("data"), QStringLiteral("{}")},
    };
    for (const auto &entry : invalid) {
        QJsonObject malformed = valid;
        malformed.insert(entry.first, entry.second);
        QVERIFY(RoukeProtocol::encodeFrame(malformed).isEmpty());
        QJsonObject decoded;
        QVERIFY(!RoukeProtocol::decodeFrame(rawFrame(QJsonDocument(malformed).toJson(QJsonDocument::Compact)), &decoded));
    }
    QVERIFY(RoukeProtocol::encodeRequest(QString()).isEmpty());
}

QTEST_GUILESS_MAIN(RoukeProtocolTest)
#include "RoukeProtocolTest.moc"
