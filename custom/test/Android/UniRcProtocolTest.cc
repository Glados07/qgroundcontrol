#include "Ch10GimbalActionState.h"
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

UniRcChannelPolicy::Result updatePolicy(UniRcChannelPolicy& policy,
                                        qint16 channel9,
                                        qint16 channel10,
                                        bool zoomDirectionReversed = false)
{
    return policy.update(1500, 1500, channel9, channel10,
                         zoomDirectionReversed);
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
    void zoomPolicyCanReverseDirection();
    void ch10PolicyUsesReleasedToPressedEdges();
    void manualAttitudeDeadband_data();
    void manualAttitudeDeadband();
    void gimbalActionStateTransitions();
    void gimbalActionStateAlternatesOnlyAfterDispatch();
    void sameFrameManualInputPrecedesCh10();
    void ch9DoesNotChangeGimbalAction();
    void invalidAttitudeValuesAreIgnored();
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

    auto result = updatePolicy(policy, 1950, 1050);
    QVERIFY(result.channelsValid);
    QVERIFY(!result.zoomDirectionChanged);
    QCOMPARE(result.zoomDirection, 0);

    result = updatePolicy(policy, 1500, 1050);
    QVERIFY(result.channelsValid);
    QCOMPARE(result.zoomDirection, 0);

    result = updatePolicy(policy, 1525, 1050);
    QVERIFY(!result.zoomDirectionChanged);
    QCOMPARE(result.zoomDirection, 0);
    result = updatePolicy(policy, 1526, 1050);
    QVERIFY(result.zoomDirectionChanged);
    QCOMPARE(result.zoomDirection, 1);

    result = updatePolicy(policy, 1475, 1050);
    QVERIFY(result.zoomDirectionChanged);
    QCOMPARE(result.zoomDirection, 0);
    result = updatePolicy(policy, 1474, 1050);
    QVERIFY(result.zoomDirectionChanged);
    QCOMPARE(result.zoomDirection, -1);

    result = policy.linkLost();
    QVERIFY(!result.channelsValid);
    QVERIFY(result.zoomDirectionChanged);
    QCOMPARE(result.zoomDirection, 0);

    result = updatePolicy(policy, 1050, 1050);
    QVERIFY(result.channelsValid);
    QVERIFY(!result.zoomDirectionChanged);
    QCOMPARE(result.zoomDirection, 0);
    updatePolicy(policy, 1500, 1050);
    result = updatePolicy(policy, 1050, 1050);
    QVERIFY(result.zoomDirectionChanged);
    QCOMPARE(result.zoomDirection, -1);
}

void UniRcProtocolTest::zoomPolicyCanReverseDirection()
{
    UniRcChannelPolicy policy;

    // A deflected wheel still cannot start before a neutral sample.
    auto result = updatePolicy(policy, 1950, 1050, true);
    QVERIFY(result.channelsValid);
    QVERIFY(!result.zoomDirectionChanged);
    QCOMPARE(result.zoomDirection, 0);

    result = updatePolicy(policy, 1500, 1050, true);
    QVERIFY(result.channelsValid);
    QCOMPARE(result.zoomDirection, 0);

    result = updatePolicy(policy, 1525, 1050, true);
    QVERIFY(!result.zoomDirectionChanged);
    QCOMPARE(result.zoomDirection, 0);
    result = updatePolicy(policy, 1526, 1050, true);
    QVERIFY(result.zoomDirectionChanged);
    QCOMPARE(result.zoomDirection, -1);

    result = updatePolicy(policy, 1475, 1050, true);
    QVERIFY(result.zoomDirectionChanged);
    QCOMPARE(result.zoomDirection, 0);

    result = updatePolicy(policy, 1474, 1050, true);
    QVERIFY(result.zoomDirectionChanged);
    QCOMPARE(result.zoomDirection, 1);

    // Runtime setting changes use the same linkLost/reset path: an active
    // direction stops, then the wheel must pass through neutral again.
    result = policy.linkLost();
    QVERIFY(result.zoomDirectionChanged);
    QCOMPARE(result.zoomDirection, 0);
    result = updatePolicy(policy, 1050, 1050, true);
    QVERIFY(!result.zoomDirectionChanged);
    QCOMPARE(result.zoomDirection, 0);
    updatePolicy(policy, 1500, 1050, true);
    result = updatePolicy(policy, 1050, 1050, true);
    QVERIFY(result.zoomDirectionChanged);
    QCOMPARE(result.zoomDirection, 1);

    // CH10 remains edge-triggered and independent from zoom reversal.
    updatePolicy(policy, 1500, 1050, true);
    result = updatePolicy(policy, 1500, 1950, true);
    QVERIFY(result.ch10Pressed);
}

void UniRcProtocolTest::ch10PolicyUsesReleasedToPressedEdges()
{
    UniRcChannelPolicy policy;

    auto result = updatePolicy(policy, 1500, 1950);
    QVERIFY(result.channelsValid);
    QVERIFY(!result.ch10Pressed);

    result = updatePolicy(policy, 1500, 1250);
    QVERIFY(!result.ch10Pressed);
    result = updatePolicy(policy, 1500, 1750);
    QVERIFY(result.ch10Pressed);
    result = updatePolicy(policy, 1500, 1950);
    QVERIFY(!result.ch10Pressed);
    result = updatePolicy(policy, 1500, 1500);
    QVERIFY(!result.ch10Pressed);
    result = updatePolicy(policy, 1500, 1251);
    QVERIFY(!result.ch10Pressed);
    result = updatePolicy(policy, 1500, 1250);
    QVERIFY(!result.ch10Pressed);
    result = updatePolicy(policy, 1500, 1750);
    QVERIFY(result.ch10Pressed);

    policy.linkLost();
    result = updatePolicy(policy, 1500, 1950);
    QVERIFY(!result.ch10Pressed);
    updatePolicy(policy, 1500, 1050);
    result = updatePolicy(policy, 1500, 1950);
    QVERIFY(result.ch10Pressed);
}

void UniRcProtocolTest::manualAttitudeDeadband_data()
{
    QTest::addColumn<qint16>("value");
    QTest::addColumn<bool>("manualInput");

    QTest::newRow("below-neutral-range") << qint16(1399) << true;
    QTest::newRow("lower-inclusive-boundary") << qint16(1400) << false;
    QTest::newRow("center") << qint16(1500) << false;
    QTest::newRow("upper-inclusive-boundary") << qint16(1600) << false;
    QTest::newRow("above-neutral-range") << qint16(1601) << true;
}

void UniRcProtocolTest::manualAttitudeDeadband()
{
    QFETCH(qint16, value);
    QFETCH(bool, manualInput);

    UniRcChannelPolicy ch7Policy;
    const auto ch7Result = ch7Policy.update(value, 1500, 1500, 1050);
    QCOMPARE(ch7Result.manualAttitudeInputDetected, manualInput);

    UniRcChannelPolicy ch8Policy;
    const auto ch8Result = ch8Policy.update(1500, value, 1500, 1050);
    QCOMPARE(ch8Result.manualAttitudeInputDetected, manualInput);
}

void UniRcProtocolTest::gimbalActionStateTransitions()
{
    using Action = Ch10GimbalActionState::Action;

    Ch10GimbalActionState state;
    QVERIFY(state.nextAction() == Action::Recenter);

    QVERIFY(state.recenterCommandDispatched());
    QVERIFY(state.nextAction() == Action::Pitch90);

    QVERIFY(state.yawLockCommandDispatched());
    QVERIFY(state.nextAction() == Action::Recenter);

    QVERIFY(state.recenterCommandDispatched());
    QVERIFY(state.pitch90CommandDispatched());
    QVERIFY(state.nextAction() == Action::Recenter);

    QVERIFY(state.recenterCommandDispatched());
    QVERIFY(state.manualAttitudeInputDetected());
    QVERIFY(state.nextAction() == Action::Recenter);

    QVERIFY(!state.pitch90CommandDispatched());
    QVERIFY(!state.yawLockCommandDispatched());
    QVERIFY(state.nextAction() == Action::Recenter);

    QVERIFY(state.recenterCommandDispatched());
    QVERIFY(state.reset());
    QVERIFY(state.nextAction() == Action::Recenter);
}

void UniRcProtocolTest::gimbalActionStateAlternatesOnlyAfterDispatch()
{
    using Action = Ch10GimbalActionState::Action;

    Ch10GimbalActionState state;
    const Action expectedActions[] = {
        Action::Recenter,
        Action::Pitch90,
        Action::Recenter,
        Action::Pitch90,
    };
    for (const Action expectedAction : expectedActions) {
        QVERIFY(state.nextAction() == expectedAction);
        if (expectedAction == Action::Recenter) {
            state.recenterCommandDispatched();
        } else {
            state.pitch90CommandDispatched();
        }
    }
    QVERIFY(state.nextAction() == Action::Recenter);

    // Selecting an action is read-only. Without a dispatched event, merely
    // reading the selection cannot advance it.
    state.recenterCommandDispatched();
    const Action selectedButNotDispatched = state.nextAction();
    QVERIFY(selectedButNotDispatched == Action::Pitch90);
    QVERIFY(state.nextAction() == selectedButNotDispatched);
}

void UniRcProtocolTest::sameFrameManualInputPrecedesCh10()
{
    using Action = Ch10GimbalActionState::Action;

    UniRcChannelPolicy policy;
    Ch10GimbalActionState state;
    updatePolicy(policy, 1500, 1050);
    state.recenterCommandDispatched();
    QVERIFY(state.nextAction() == Action::Pitch90);

    auto result = policy.update(1399, 1500, 1950, 1750);
    QVERIFY(result.manualAttitudeInputDetected);
    QVERIFY(result.zoomDirectionChanged);
    QCOMPARE(result.zoomDirection, 1);
    QVERIFY(result.ch10Pressed);
    state.manualAttitudeInputDetected();
    QVERIFY(state.nextAction() == Action::Recenter);
    state.recenterCommandDispatched();
    QVERIFY(state.nextAction() == Action::Pitch90);

    result = policy.update(1400, 1600, 1500, 1050);
    QVERIFY(!result.manualAttitudeInputDetected);
    result = policy.update(1400, 1600, 1500, 1750);
    QVERIFY(result.ch10Pressed);
    QVERIFY(!result.manualAttitudeInputDetected);
    QVERIFY(state.nextAction() == Action::Pitch90);
    state.pitch90CommandDispatched();
    // Recreate Pitch90 as the next action for the independent CH8 case.
    state.recenterCommandDispatched();
    QVERIFY(state.nextAction() == Action::Pitch90);

    policy.update(1500, 1500, 1500, 1050);
    result = policy.update(1500, 1601, 1500, 1750);
    QVERIFY(result.manualAttitudeInputDetected);
    QVERIFY(result.ch10Pressed);
    state.manualAttitudeInputDetected();
    QVERIFY(state.nextAction() == Action::Recenter);
}

void UniRcProtocolTest::ch9DoesNotChangeGimbalAction()
{
    using Action = Ch10GimbalActionState::Action;

    UniRcChannelPolicy policy;
    Ch10GimbalActionState state;
    state.recenterCommandDispatched();
    QVERIFY(state.nextAction() == Action::Pitch90);

    auto result = policy.update(1400, 1600, 1500, 1050);
    QVERIFY(!result.manualAttitudeInputDetected);
    QVERIFY(state.nextAction() == Action::Pitch90);

    result = policy.update(1500, 1500, 1950, 1050);
    QVERIFY(result.zoomDirectionChanged);
    QCOMPARE(result.zoomDirection, 1);
    QVERIFY(!result.manualAttitudeInputDetected);
    QVERIFY(state.nextAction() == Action::Pitch90);

    result = policy.update(1600, 1400, 1050, 1050);
    QVERIFY(result.zoomDirectionChanged);
    QCOMPARE(result.zoomDirection, -1);
    QVERIFY(!result.manualAttitudeInputDetected);
    QVERIFY(state.nextAction() == Action::Pitch90);
}

void UniRcProtocolTest::invalidAttitudeValuesAreIgnored()
{
    const qint16 invalidValues[] = {0, 899, 2101};
    for (const qint16 value : invalidValues) {
        QVERIFY(!UniRcChannelPolicy::isManualAttitudeInput(value));

        UniRcChannelPolicy policy;
        auto result = policy.update(value, 1500, 1500, 1050);
        QVERIFY(result.channelsValid);
        QVERIFY(!result.manualAttitudeInputDetected);
        result = policy.update(value, 1500, 1500, 1750);
        QVERIFY(result.ch10Pressed);
        QVERIFY(!result.manualAttitudeInputDetected);

        UniRcChannelPolicy ch8Policy;
        result = ch8Policy.update(1500, value, 1500, 1050);
        QVERIFY(result.channelsValid);
        QVERIFY(!result.manualAttitudeInputDetected);
    }
}

void UniRcProtocolTest::policyRejectsUnreasonableValues()
{
    UniRcChannelPolicy policy;
    updatePolicy(policy, 1500, 1050);
    auto result = updatePolicy(policy, 1950, 1050);
    QCOMPARE(result.zoomDirection, 1);

    result = policy.update(1399, 1500, 899, 1050);
    QVERIFY(!result.channelsValid);
    QVERIFY(result.zoomDirectionChanged);
    QCOMPARE(result.zoomDirection, 0);
    QVERIFY(!result.manualAttitudeInputDetected);

    result = policy.update(1500, 1601, 1500, 2101);
    QVERIFY(!result.channelsValid);
    QVERIFY(!result.ch10Pressed);
    QVERIFY(!result.manualAttitudeInputDetected);

    // Both controls must observe their safe position again after invalid data.
    result = updatePolicy(policy, 1950, 1950);
    QVERIFY(result.channelsValid);
    QCOMPARE(result.zoomDirection, 0);
    QVERIFY(!result.ch10Pressed);
    updatePolicy(policy, 1500, 1050);
    result = updatePolicy(policy, 1950, 1950);
    QCOMPARE(result.zoomDirection, 1);
    QVERIFY(result.ch10Pressed);
}

QTEST_APPLESS_MAIN(UniRcProtocolTest)

#include "UniRcProtocolTest.moc"
