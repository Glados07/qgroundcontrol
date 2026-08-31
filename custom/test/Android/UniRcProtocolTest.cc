#include "UniRcChannelPolicy.h"
#include "UniRcProtocol.h"

#include <QtTest/QTest>

namespace {
quint16 testCrc16Xmodem(const QByteArray& bytes)
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

void appendLe16(QByteArray& bytes, quint16 value)
{
    bytes.append(static_cast<char>(value & 0xff));
    bytes.append(static_cast<char>((value >> 8) & 0xff));
}

QByteArray makeFrame(quint8 control,
                     quint8 command,
                     const QByteArray& payload,
                     quint16 sequence = 0x1234)
{
    QByteArray frame = QByteArray::fromHex("5566");
    frame.append(static_cast<char>(control));
    appendLe16(frame, static_cast<quint16>(payload.size()));
    appendLe16(frame, sequence);
    frame.append(static_cast<char>(command));
    frame.append(payload);
    appendLe16(frame, testCrc16Xmodem(frame));
    return frame;
}

QByteArray makeChannelFrame(const UniRcProtocol::Channels& channels,
                            quint8 control = 0)
{
    QByteArray payload;
    payload.reserve(UniRcProtocol::ChannelCount * 2);
    for (const qint16 channel : channels) {
        appendLe16(payload, static_cast<quint16>(channel));
    }
    return makeFrame(control, UniRcProtocol::CommandChannelData, payload);
}
}

class UniRcProtocolTest : public QObject
{
    Q_OBJECT

private slots:
    void exact20HzRequest();
    void decodeChannelResponse();
    void decodeDocumentedChannelResponse();
    void rejectInvalidFramesAndResponses();
    void streamParserHandlesPartialAndMultipleFrames();
    void streamParserResynchronizes();
    void zoomPolicyRequiresNeutralAfterStartupAndLoss();
    void centerPolicyUsesReleasedToPressedEdges();
    void policyRejectsUnreasonableValues();
};

void UniRcProtocolTest::exact20HzRequest()
{
    QCOMPARE(UniRcProtocol::channelDataRequestPacket(2),
             QByteArray::fromHex("556601010000004202b5c0"));
    QCOMPARE(UniRcProtocol::channelDataRequestPacket(),
             QByteArray::fromHex("55660101000000420552b0"));
    QCOMPARE(UniRcProtocol::channelDataRequestPacket(
                 UniRcProtocol::FrequencyOff),
             QByteArray::fromHex("556601010000004200f7e0"));
    QVERIFY(UniRcProtocol::channelDataRequestPacket(8).isEmpty());

    const auto decoded = UniRcProtocol::decodePacket(
        UniRcProtocol::channelDataRequestPacket(5, 0x5678));
    QVERIFY(decoded.valid);
    QCOMPARE(decoded.control, quint8(1));
    QCOMPARE(decoded.sequence, quint16(0x5678));
    QCOMPARE(decoded.command, quint8(0x42));
    QCOMPARE(decoded.payload, QByteArray::fromHex("05"));
}

void UniRcProtocolTest::decodeChannelResponse()
{
    UniRcProtocol::Channels expected = {
        1050, 1100, 1200, 1300,
        1400, 1500, 1600, 1700,
        1950, 1050, 1510, 1490,
        1800, 1900, 1000, 2000,
    };
    const auto packet = UniRcProtocol::decodePacket(
        makeChannelFrame(expected));
    QVERIFY(packet.valid);
    QCOMPARE(packet.control, quint8(0));
    QCOMPARE(packet.sequence, quint16(0x1234));

    UniRcProtocol::Channels actual{};
    QVERIFY(UniRcProtocol::parseChannelData(packet, &actual));
    for (int index = 0; index < UniRcProtocol::ChannelCount; ++index) {
        QCOMPARE(actual[static_cast<std::size_t>(index)],
                 expected[static_cast<std::size_t>(index)]);
    }
    QCOMPARE(actual[8], qint16(1950));
    QCOMPARE(actual[9], qint16(1050));
}

void UniRcProtocolTest::decodeDocumentedChannelResponse()
{
    const QByteArray documentedResponse = QByteArray::fromHex(
        "5566002000990042"
        "dc05dc05dc05dc05dc05dc05dc05dc05"
        "dc05dc05dc051a04dc05dc051a041a04"
        "ff88");
    const UniRcProtocol::DecodedPacket packet =
        UniRcProtocol::decodePacket(documentedResponse);
    QVERIFY(packet.valid);
    QCOMPARE(packet.control, quint8(0));
    QCOMPARE(packet.sequence, quint16(0x0099));
    QCOMPARE(packet.command, quint8(0x42));
    QCOMPARE(packet.payload.size(), 32);

    UniRcProtocol::Channels channels {};
    QVERIFY(UniRcProtocol::parseChannelData(packet, &channels));
    for (int index = 0; index < 11; ++index) {
        QCOMPARE(channels[static_cast<std::size_t>(index)], qint16(1500));
    }
    QCOMPARE(channels[11], qint16(1050));
    QCOMPARE(channels[12], qint16(1500));
    QCOMPARE(channels[13], qint16(1500));
    QCOMPARE(channels[14], qint16(1050));
    QCOMPARE(channels[15], qint16(1050));
}

void UniRcProtocolTest::rejectInvalidFramesAndResponses()
{
    UniRcProtocol::Channels channels{};
    channels.fill(1500);
    const QByteArray validFrame = makeChannelFrame(channels);

    QByteArray badCrc = validFrame;
    badCrc[badCrc.size() - 1] = static_cast<char>(
        static_cast<quint8>(badCrc.at(badCrc.size() - 1)) ^ 0x01);
    QVERIFY(!UniRcProtocol::decodePacket(badCrc).valid);

    QByteArray reservedControl = validFrame;
    reservedControl[2] = static_cast<char>(0x04);
    const quint16 repairedCrc = testCrc16Xmodem(
        reservedControl.left(reservedControl.size() - 2));
    reservedControl[reservedControl.size() - 2] =
        static_cast<char>(repairedCrc & 0xff);
    reservedControl[reservedControl.size() - 1] =
        static_cast<char>((repairedCrc >> 8) & 0xff);
    QVERIFY(!UniRcProtocol::decodePacket(reservedControl).valid);

    const auto request = UniRcProtocol::decodePacket(
        UniRcProtocol::channelDataRequestPacket());
    QVERIFY(request.valid);
    QVERIFY(!UniRcProtocol::parseChannelData(request, &channels));

    const auto ackControl = UniRcProtocol::decodePacket(
        makeChannelFrame(channels, 0x02));
    QVERIFY(ackControl.valid);
    QVERIFY(!UniRcProtocol::parseChannelData(ackControl, &channels));

    const auto shortPayload = UniRcProtocol::decodePacket(
        makeFrame(0, UniRcProtocol::CommandChannelData,
                  QByteArray(30, '\0')));
    QVERIFY(shortPayload.valid);
    QVERIFY(!UniRcProtocol::parseChannelData(shortPayload, &channels));
    QVERIFY(!UniRcProtocol::parseChannelData(
        UniRcProtocol::decodePacket(validFrame), nullptr));
}

void UniRcProtocolTest::streamParserHandlesPartialAndMultipleFrames()
{
    UniRcProtocol::Channels firstChannels{};
    firstChannels.fill(1500);
    firstChannels[8] = 1050;
    firstChannels[9] = 1950;
    const QByteArray firstFrame = makeChannelFrame(firstChannels);

    UniRcProtocol::Channels secondChannels = firstChannels;
    secondChannels[8] = 1950;
    secondChannels[9] = 1050;
    const QByteArray secondFrame = makeChannelFrame(secondChannels);

    UniRcProtocol::StreamParser parser;
    QVERIFY(parser.append(firstFrame.left(7)).isEmpty());
    const QList<UniRcProtocol::DecodedPacket> packets =
        parser.append(firstFrame.mid(7) + secondFrame);
    QCOMPARE(packets.size(), 2);

    UniRcProtocol::Channels decoded{};
    QVERIFY(UniRcProtocol::parseChannelData(packets.at(0), &decoded));
    QCOMPARE(decoded[8], qint16(1050));
    QCOMPARE(decoded[9], qint16(1950));
    QVERIFY(UniRcProtocol::parseChannelData(packets.at(1), &decoded));
    QCOMPARE(decoded[8], qint16(1950));
    QCOMPARE(decoded[9], qint16(1050));

    parser.reset();
    QVERIFY(parser.append(QByteArray::fromHex("5566")).isEmpty());
}

void UniRcProtocolTest::streamParserResynchronizes()
{
    UniRcProtocol::Channels channels{};
    channels.fill(1500);
    const QByteArray validFrame = makeChannelFrame(channels);

    QByteArray corruptFrame = validFrame;
    corruptFrame[20] = static_cast<char>(
        static_cast<quint8>(corruptFrame.at(20)) ^ 0x10);

    UniRcProtocol::StreamParser parser;
    const QByteArray noise = QByteArray::fromHex("00112255334455");
    const QList<UniRcProtocol::DecodedPacket> afterCorrupt =
        parser.append(noise + corruptFrame + validFrame);
    QCOMPARE(afterCorrupt.size(), 1);
    QVERIFY(afterCorrupt.first().valid);

    // The first candidate advertises a 500-byte body but is truncated. The
    // parser must still discover the complete valid frame behind it.
    parser.reset();
    const QByteArray bogusLength = QByteArray::fromHex("556600f401000042");
    const QList<UniRcProtocol::DecodedPacket> afterBogusLength =
        parser.append(bogusLength + validFrame);
    QCOMPARE(afterBogusLength.size(), 1);
    QVERIFY(afterBogusLength.first().valid);
}

void UniRcProtocolTest::zoomPolicyRequiresNeutralAfterStartupAndLoss()
{
    UniRcChannelPolicy policy;

    auto result = policy.update(1950, 1050);
    QVERIFY(result.channelsValid);
    QVERIFY(!result.zoomDirectionChanged);
    QCOMPARE(result.zoomDirection, 0);

    result = policy.update(1500, 1050);
    QVERIFY(result.channelsValid);
    QCOMPARE(result.zoomDirection, 0);

    result = policy.update(1525, 1050);
    QVERIFY(!result.zoomDirectionChanged);
    QCOMPARE(result.zoomDirection, 0);
    result = policy.update(1526, 1050);
    QVERIFY(result.zoomDirectionChanged);
    QCOMPARE(result.zoomDirection, 1);

    result = policy.update(1475, 1050);
    QVERIFY(result.zoomDirectionChanged);
    QCOMPARE(result.zoomDirection, 0);
    result = policy.update(1474, 1050);
    QVERIFY(result.zoomDirectionChanged);
    QCOMPARE(result.zoomDirection, -1);

    result = policy.linkLost();
    QVERIFY(!result.channelsValid);
    QVERIFY(result.zoomDirectionChanged);
    QCOMPARE(result.zoomDirection, 0);

    result = policy.update(1050, 1050);
    QVERIFY(result.channelsValid);
    QVERIFY(!result.zoomDirectionChanged);
    QCOMPARE(result.zoomDirection, 0);
    policy.update(1500, 1050);
    result = policy.update(1050, 1050);
    QVERIFY(result.zoomDirectionChanged);
    QCOMPARE(result.zoomDirection, -1);
}

void UniRcProtocolTest::centerPolicyUsesReleasedToPressedEdges()
{
    UniRcChannelPolicy policy;

    auto result = policy.update(1500, 1950);
    QVERIFY(result.channelsValid);
    QVERIFY(!result.centerRequested);

    result = policy.update(1500, 1250);
    QVERIFY(!result.centerRequested);
    result = policy.update(1500, 1750);
    QVERIFY(result.centerRequested);
    result = policy.update(1500, 1950);
    QVERIFY(!result.centerRequested);
    result = policy.update(1500, 1500);
    QVERIFY(!result.centerRequested);
    result = policy.update(1500, 1251);
    QVERIFY(!result.centerRequested);
    result = policy.update(1500, 1250);
    QVERIFY(!result.centerRequested);
    result = policy.update(1500, 1750);
    QVERIFY(result.centerRequested);

    policy.linkLost();
    result = policy.update(1500, 1950);
    QVERIFY(!result.centerRequested);
    policy.update(1500, 1050);
    result = policy.update(1500, 1950);
    QVERIFY(result.centerRequested);
}

void UniRcProtocolTest::policyRejectsUnreasonableValues()
{
    UniRcChannelPolicy policy;
    policy.update(1500, 1050);
    auto result = policy.update(1950, 1050);
    QCOMPARE(result.zoomDirection, 1);

    result = policy.update(899, 1050);
    QVERIFY(!result.channelsValid);
    QVERIFY(result.zoomDirectionChanged);
    QCOMPARE(result.zoomDirection, 0);

    result = policy.update(1500, 2101);
    QVERIFY(!result.channelsValid);
    QVERIFY(!result.centerRequested);

    // Both controls must observe their safe position again after invalid data.
    result = policy.update(1950, 1950);
    QVERIFY(result.channelsValid);
    QCOMPARE(result.zoomDirection, 0);
    QVERIFY(!result.centerRequested);
    policy.update(1500, 1050);
    result = policy.update(1950, 1950);
    QCOMPARE(result.zoomDirection, 1);
    QVERIFY(result.centerRequested);
}

QTEST_APPLESS_MAIN(UniRcProtocolTest)

#include "UniRcProtocolTest.moc"
