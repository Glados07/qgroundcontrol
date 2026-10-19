#include "FuelCellControllerManager.h"
#include "TestDoubles.h"

#include <QtCore/QMetaProperty>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

// Use QGC's actual MAVLink wrapper and generated wire codec, including shared channels.
mavlink_status_t m_mavlink_status[MAVLINK_COMM_NUM_BUFFERS]{};
mavlink_status_t *mavlink_get_channel_status(uint8_t channel) { return &m_mavlink_status[channel]; }

namespace {
using Manager = FuelCellControllerManager;

mavlink_fuel_cell_status_t status(int connection = FUEL_CELL_CONNECTION_STATE_OK)
{
    mavlink_fuel_cell_status_t result{};
    result.bus_voltage = 245;
    result.bus_current = 80;
    result.requested_power = 500;
    result.soc = 755;
    result.device_state = FUEL_CELL_DEVICE_STATE_RUNNING;
    result.connection_state = connection;
    return result;
}

void receive(Vehicle &vehicle, const mavlink_fuel_cell_status_t &sample, int system = -1, int component = -1)
{
    mavlink_message_t message{};
    mavlink_msg_fuel_cell_status_encode(system < 0 ? vehicle.id() : system,
                                       component < 0 ? vehicle.compId() : component, &message, &sample);
    emit vehicle.mavlinkMessageReceived(message);
}

mavlink_fuel_cell_control_t control(const mavlink_message_t &message)
{
    mavlink_fuel_cell_control_t result{};
    mavlink_msg_fuel_cell_control_decode(&message, &result);
    return result;
}
}

class FuelCellControllerManagerTest : public QObject
{
    Q_OBJECT
private:
    std::shared_ptr<LinkInterface> _link;

    void bind(Vehicle &vehicle)
    {
        vehicle.vehicleLinkManager()->setPrimaryLink(_link);
        MultiVehicleManager::instance()->setActiveVehicle(&vehicle);
    }

    void ready(Manager &manager, Vehicle &vehicle)
    {
        manager._timer.stop();
        receive(vehicle, status());
    }

    void retry(Manager &manager)
    {
        manager._lastStatusAt = manager._clock.elapsed();
        manager._lastSendAt = manager._clock.elapsed() - 500;
        manager._poll();
    }

private slots:
    void init()
    {
        std::memset(m_mavlink_status, 0, sizeof(m_mavlink_status));
        MAVLinkProtocol::instance()->systemId = 255;
        _link = std::make_shared<LinkInterface>();
    }

    void cleanup()
    {
        MultiVehicleManager::instance()->setActiveVehicle(nullptr);
        _link.reset();
    }

    void noTelemetryIsUnknownAndBusPropertiesAreReadOnly()
    {
        Vehicle vehicle;
        bind(vehicle);
        Manager manager;
        manager._timer.stop();
        QVERIFY(manager.vehicleAvailable());
        QVERIFY(!manager.telemetryAvailable());
        QVERIFY(!manager.telemetryFresh());
        QCOMPARE(manager.connectionState(), Manager::Unknown);
        QVERIFY(std::isnan(manager.busVoltage()));
        QVERIFY(std::isnan(manager.socPercent()));
        QCOMPARE(manager.fcsState(), -1);
        QCOMPARE(manager.acceptedCommand(), -1);
        QVERIFY(!manager.canControl());
        QVERIFY(!manager.canEmergencyStop());
        QVERIFY(!manager.requestEmergencyStopFcs(manager.sessionRevision()));
        QVERIFY(vehicle.sent.isEmpty());
        for (const auto *name : {"busVoltage", "busCurrent", "requestedPower", "socPercent", "acceptedCommand"}) {
            const int index = manager.metaObject()->indexOfProperty(name);
            QVERIFY(index >= 0);
            QVERIFY(!manager.metaObject()->property(index).isWritable());
        }
    }

    void telemetryScalingAndThreeConnectionStates()
    {
        Vehicle vehicle;
        bind(vehicle);
        Manager manager;
        manager._timer.stop();
        receive(vehicle, status(FUEL_CELL_CONNECTION_STATE_NEVER_CONNECTED));
        QVERIFY(manager.telemetryAvailable());
        QVERIFY(manager.telemetryFresh());
        QCOMPARE(manager.connectionState(), Manager::NeverConnected);
        QCOMPARE(manager.busVoltage(), 24.5);
        QCOMPARE(manager.busCurrent(), 8.0);
        QCOMPARE(manager.requestedPower(), 500.0);
        QVERIFY(!manager.deviceDataValid());
        QVERIFY(std::isnan(manager.socPercent()));
        receive(vehicle, status());
        QCOMPARE(manager.connectionState(), Manager::Connected);
        QCOMPARE(manager.socPercent(), 75.5);
        QCOMPARE(manager.fcsState(), int(FUEL_CELL_DEVICE_STATE_RUNNING));
        QVERIFY(manager.canControl());
        QVERIFY(manager.canEmergencyStop());
        receive(vehicle, status(FUEL_CELL_CONNECTION_STATE_ERROR_OCCURRED));
        QCOMPARE(manager.connectionState(), Manager::Disconnected);
        QVERIFY(!manager.canControl());
        QVERIFY(!manager.canEmergencyStop());
        QCOMPARE(manager.fcsState(), -1);
        receive(vehicle, status());
        QCOMPARE(manager.connectionState(), Manager::Connected);
        QVERIFY(vehicle.sent.isEmpty()); // Connecting or recovering never sends a command.
    }

    void unrelatedAndMalformedMessagesDoNotRefreshTelemetry()
    {
        Vehicle vehicle(23, 7);
        bind(vehicle);
        Manager manager;
        manager._timer.stop();
        receive(vehicle, status(), 24, 7);
        receive(vehicle, status(), 23, 8);
        QVERIFY(!manager.telemetryAvailable());
        auto sample = status();
        sample.soc = 1001;
        receive(vehicle, sample);
        sample = status();
        sample.device_state = 13;
        receive(vehicle, sample);
        sample = status();
        sample.connection_state = 3;
        receive(vehicle, sample);
        sample = status();
        sample.control_command = 3;
        receive(vehicle, sample);
        QVERIFY(!manager.telemetryAvailable());
        sample = {}; // Zero is real valid data when the device is connected.
        sample.connection_state = FUEL_CELL_CONNECTION_STATE_OK;
        receive(vehicle, sample);
        QCOMPARE(manager.socPercent(), 0.0);
        QCOMPARE(manager.busVoltage(), 0.0);
        QCOMPARE(manager.fcsState(), 0);
    }

    void controlWireMessages_data()
    {
        QTest::addColumn<int>("command");
        QTest::newRow("start") << int(FUEL_CELL_CONTROL_COMMAND_ON);
        QTest::newRow("stop") << int(FUEL_CELL_CONTROL_COMMAND_OFF);
        QTest::newRow("FCS-emergency-stop") << int(FUEL_CELL_CONTROL_COMMAND_EMERGENCY_STOP);
    }

    void controlWireMessages()
    {
        QFETCH(int, command);
        Vehicle vehicle(23, 7);
        bind(vehicle);
        Manager manager;
        ready(manager, vehicle);
        const auto revision = manager.sessionRevision();
        const bool sent = command == FUEL_CELL_CONTROL_COMMAND_ON ? manager.requestStartFcs(revision)
                          : command == FUEL_CELL_CONTROL_COMMAND_OFF ? manager.requestStopFcs(revision)
                          : manager.requestEmergencyStopFcs(revision);
        QVERIFY(sent);
        QCOMPARE(vehicle.sent.size(), 1);
        const auto &packet = vehicle.sent.last();
        QCOMPARE(packet.msgid, uint32_t(MAVLINK_MSG_ID_FUEL_CELL_CONTROL));
        QCOMPARE(packet.sysid, uint8_t(255));
        QCOMPARE(packet.compid, uint8_t(MAV_COMP_ID_MISSIONPLANNER));
        uint8_t bytes[MAVLINK_MAX_PACKET_LEN];
        const auto length = mavlink_msg_to_send_buffer(bytes, &packet);
        QCOMPARE(bytes[0], uint8_t(MAVLINK_STX));
        mavlink_message_t parsed{};
        mavlink_status_t parserStatus{};
        int decoded = 0;
        for (unsigned i = 0; i < length; ++i) {
            decoded += mavlink_parse_char(MAVLINK_COMM_15, bytes[i], &parsed, &parserStatus) == MAVLINK_FRAMING_OK;
        }
        QCOMPARE(decoded, 1);
        const auto request = control(parsed);
        QVERIFY(request.request_id != 0);
        QCOMPARE(request.target_system, uint8_t(23));
        QCOMPARE(request.target_component, uint8_t(7));
        QCOMPARE(request.control_command, uint8_t(command));
        QCOMPARE(manager.requestState(), Manager::Pending);
        QCOMPARE(manager.requestedCommand(), command);
        QVERIFY(!manager.canControl());
        QCOMPARE(manager.busVoltage(), 24.5);
        QCOMPARE(manager.acceptedCommand(), 0); // Requested command is not confirmed telemetry.
    }

    void acknowledgementAndActualFcsStateAreIndependent()
    {
        Vehicle vehicle;
        bind(vehicle);
        Manager manager;
        ready(manager, vehicle);
        QVERIFY(manager.requestStartFcs(manager.sessionRevision()));
        auto sample = status();
        sample.response_request_id = control(vehicle.sent.last()).request_id;
        sample.control_command = FUEL_CELL_CONTROL_COMMAND_ON;
        sample.device_state = FUEL_CELL_DEVICE_STATE_STARTING;
        receive(vehicle, sample);
        QCOMPARE(manager.requestState(), Manager::Accepted);
        QCOMPARE(manager.acceptedCommand(), int(FUEL_CELL_CONTROL_COMMAND_ON));
        QCOMPARE(manager.fcsState(), int(FUEL_CELL_DEVICE_STATE_STARTING));
        sample.device_state = FUEL_CELL_DEVICE_STATE_RUNNING;
        receive(vehicle, sample);
        QCOMPARE(manager.fcsState(), int(FUEL_CELL_DEVICE_STATE_RUNNING));
        QCOMPARE(vehicle.sent.size(), 1);
    }

    void consoleCanReplaceAnAcceptedCommand()
    {
        Vehicle vehicle;
        bind(vehicle);
        Manager manager;
        ready(manager, vehicle);
        QVERIFY(manager.requestStartFcs(manager.sessionRevision()));
        auto sample = status();
        sample.response_request_id = control(vehicle.sent.last()).request_id;
        sample.control_command = FUEL_CELL_CONTROL_COMMAND_OFF;
        sample.bus_voltage = 300;
        receive(vehicle, sample);
        QCOMPARE(manager.requestState(), Manager::Accepted);
        QCOMPARE(manager.acceptedCommand(), int(FUEL_CELL_CONTROL_COMMAND_OFF));
        QCOMPARE(manager.busVoltage(), 30.0);
        QVERIFY(manager.lastError().isEmpty());
    }

    void refusalResults_data()
    {
        QTest::addColumn<int>("result");
        for (int result : {MAV_RESULT_TEMPORARILY_REJECTED, MAV_RESULT_DENIED, MAV_RESULT_UNSUPPORTED, MAV_RESULT_FAILED}) {
            QTest::newRow(qPrintable(QString::number(result))) << result;
        }
    }

    void refusalResults()
    {
        QFETCH(int, result);
        Vehicle vehicle;
        bind(vehicle);
        Manager manager;
        ready(manager, vehicle);
        QVERIFY(manager.requestStartFcs(manager.sessionRevision()));
        auto sample = status(result == MAV_RESULT_TEMPORARILY_REJECTED ? FUEL_CELL_CONNECTION_STATE_ERROR_OCCURRED
                             : FUEL_CELL_CONNECTION_STATE_OK);
        sample.response_request_id = control(vehicle.sent.last()).request_id;
        sample.control_result = result;
        receive(vehicle, sample);
        QCOMPARE(manager.requestState(), Manager::Failed);
        QVERIFY(!manager.lastError().isEmpty());
        QVERIFY(!manager.commandPending());
        retry(manager);
        QCOMPARE(vehicle.sent.size(), 1);
    }

    void emergencyStopPreemptsOrdinaryRequestAndIgnoresItsLateAck()
    {
        Vehicle vehicle;
        bind(vehicle);
        Manager manager;
        ready(manager, vehicle);
        const auto revision = manager.sessionRevision();
        QVERIFY(manager.requestStartFcs(revision));
        const auto old = control(vehicle.sent.last());
        QVERIFY(manager.canEmergencyStop());
        QVERIFY(manager.requestEmergencyStopFcs(revision));
        const auto emergency = control(vehicle.sent.last());
        QVERIFY(emergency.request_id != old.request_id);
        QCOMPARE(emergency.control_command, uint8_t(FUEL_CELL_CONTROL_COMMAND_EMERGENCY_STOP));
        QVERIFY(!manager.canEmergencyStop());
        QVERIFY(!manager.requestEmergencyStopFcs(revision));
        QVERIFY(!manager.requestStopFcs(revision));
        auto sample = status();
        sample.response_request_id = old.request_id;
        sample.control_command = old.control_command;
        receive(vehicle, sample);
        QCOMPARE(manager.requestState(), Manager::Pending);
        QCOMPARE(manager.requestedCommand(), int(FUEL_CELL_CONTROL_COMMAND_EMERGENCY_STOP));
        retry(manager);
        QCOMPARE(vehicle.sent.size(), 3);
        QCOMPARE(control(vehicle.sent.last()).request_id, emergency.request_id);
        QCOMPARE(control(vehicle.sent.last()).control_command, emergency.control_command);
        sample.response_request_id = emergency.request_id;
        sample.control_command = emergency.control_command;
        sample.device_state = FUEL_CELL_DEVICE_STATE_EMERGENCY_STOPPING;
        receive(vehicle, sample);
        QCOMPARE(manager.requestState(), Manager::Accepted);
        QCOMPARE(manager.fcsState(), int(FUEL_CELL_DEVICE_STATE_EMERGENCY_STOPPING));
        sample.device_state = FUEL_CELL_DEVICE_STATE_EMERGENCY_STOP_COMPLETE;
        receive(vehicle, sample);
        QCOMPARE(manager.fcsState(), int(FUEL_CELL_DEVICE_STATE_EMERGENCY_STOP_COMPLETE));
    }

    void retriesKeepPayloadAndSourceAndStopAfterThreeSends()
    {
        Vehicle vehicle;
        bind(vehicle);
        Manager manager;
        ready(manager, vehicle);
        QVERIFY(manager.requestStopFcs(manager.sessionRevision()));
        const auto first = vehicle.sent.last();
        MAVLinkProtocol::instance()->systemId = 254;
        retry(manager);
        retry(manager);
        retry(manager);
        QCOMPARE(vehicle.sent.size(), 3);
        for (const auto &packet : std::as_const(vehicle.sent)) {
            QCOMPARE(packet.sysid, first.sysid);
            QCOMPARE(packet.compid, first.compid);
            // MAVLink 2 trims OFF=0; bytes past len may contain the changing frame checksum.
            QCOMPARE(packet.len, first.len);
            QCOMPARE(std::memcmp(_MAV_PAYLOAD(&packet), _MAV_PAYLOAD(&first), first.len), 0);
        }
        manager._firstSendAt = manager._clock.elapsed() - 2000;
        manager._poll();
        QCOMPARE(manager.requestState(), Manager::Unconfirmed);
        QVERIFY(!manager.commandPending());
        auto sample = status();
        sample.response_request_id = control(first).request_id;
        receive(vehicle, sample);
        QCOMPARE(manager.requestState(), Manager::Unconfirmed); // A late ACK cannot rewrite a completed transaction.
        QCOMPARE(vehicle.sent.size(), 3);
    }

    void telemetryTimeoutInvalidatesDisplayAndCancelsRetries()
    {
        Vehicle vehicle;
        bind(vehicle);
        Manager manager;
        ready(manager, vehicle);
        QSignalSpy spy(&manager, &Manager::statusChanged);
        QVERIFY(manager.requestStartFcs(manager.sessionRevision()));
        manager._lastStatusAt = manager._clock.elapsed() - 1000;
        manager._poll();
        QVERIFY(!spy.isEmpty());
        QVERIFY(manager.telemetryAvailable());
        QVERIFY(!manager.telemetryFresh());
        QCOMPARE(manager.connectionState(), Manager::Unknown);
        QCOMPARE(manager.requestState(), Manager::Unconfirmed);
        QVERIFY(std::isnan(manager.busVoltage()));
        QVERIFY(std::isnan(manager.socPercent()));
        QVERIFY(!manager.requestEmergencyStopFcs(manager.sessionRevision()));
        receive(vehicle, status());
        QVERIFY(manager.telemetryFresh());
        QCOMPARE(vehicle.sent.size(), 1);
    }

    void deviceLossCancelsButMatchingAckIsHandledFirst()
    {
        Vehicle vehicle;
        bind(vehicle);
        Manager manager;
        ready(manager, vehicle);
        QVERIFY(manager.requestStartFcs(manager.sessionRevision()));
        receive(vehicle, status(FUEL_CELL_CONNECTION_STATE_ERROR_OCCURRED));
        QCOMPARE(manager.requestState(), Manager::Unconfirmed);
        receive(vehicle, status());
        QVERIFY(manager.requestStopFcs(manager.sessionRevision()));
        auto sample = status(FUEL_CELL_CONNECTION_STATE_ERROR_OCCURRED);
        sample.response_request_id = control(vehicle.sent.last()).request_id;
        receive(vehicle, sample);
        QCOMPARE(manager.requestState(), Manager::Accepted);
        QCOMPARE(manager.connectionState(), Manager::Disconnected);
    }

    void vehicleChangeRejectsOldDialogsAndOldReplies()
    {
        Vehicle first(1), second(2);
        bind(first);
        Manager manager;
        ready(manager, first);
        const auto revision = manager.sessionRevision();
        QVERIFY(manager.requestStartFcs(revision));
        const auto old = control(first.sent.last());
        bind(second);
        QVERIFY(manager.sessionRevision() != revision);
        QVERIFY(!manager.telemetryAvailable());
        QCOMPARE(manager.requestState(), Manager::Unconfirmed);
        auto sample = status();
        sample.response_request_id = old.request_id;
        receive(first, sample);
        QVERIFY(!manager.telemetryAvailable());
        receive(second, status());
        QVERIFY(!manager.requestEmergencyStopFcs(revision));
        QVERIFY(second.sent.isEmpty());
        QVERIFY(manager.requestStopFcs(manager.sessionRevision()));
        QCOMPARE(control(second.sent.last()).target_system, uint8_t(2));
    }

    void communicationRecoveryNeedsNewTelemetryAndUserAction()
    {
        Vehicle vehicle;
        bind(vehicle);
        Manager manager;
        ready(manager, vehicle);
        const auto revision = manager.sessionRevision();
        QVERIFY(manager.requestStartFcs(revision));
        vehicle.vehicleLinkManager()->setCommunicationLost(true);
        QCOMPARE(manager.requestState(), Manager::Unconfirmed);
        QVERIFY(manager.sessionRevision() != revision);
        receive(vehicle, status());
        QVERIFY(!manager.telemetryFresh());
        vehicle.vehicleLinkManager()->setCommunicationLost(false);
        QVERIFY(!manager.telemetryFresh());
        receive(vehicle, status());
        QVERIFY(manager.canControl());
        QCOMPARE(vehicle.sent.size(), 1);
    }

    void primaryLinkCanChangeDuringRetry()
    {
        Vehicle vehicle;
        bind(vehicle);
        Manager manager;
        ready(manager, vehicle);
        QVERIFY(manager.requestStartFcs(manager.sessionRevision()));
        const auto id = control(vehicle.sent.last()).request_id;
        auto secondLink = std::make_shared<LinkInterface>();
        secondLink->channel = 1;
        vehicle.vehicleLinkManager()->setPrimaryLink(secondLink);
        retry(manager);
        QCOMPARE(vehicle.sent.size(), 2);
        QCOMPARE(control(vehicle.sent.last()).request_id, id);
    }

    void transportGates_data()
    {
        QTest::addColumn<int>("mode");
        QTest::newRow("MAVLink-1") << 0;
        QTest::newRow("log-replay") << 1;
        QTest::newRow("link-disconnected") << 2;
        QTest::newRow("no-channel") << 3;
        QTest::newRow("no-primary-link") << 4;
        QTest::newRow("offline-editing") << 5;
    }

    void transportGates()
    {
        QFETCH(int, mode);
        Vehicle vehicle;
        bind(vehicle);
        Manager manager;
        ready(manager, vehicle);
        switch (mode) {
        case 0: m_mavlink_status[0].flags |= MAVLINK_STATUS_FLAG_OUT_MAVLINK1; break;
        case 1: _link->replay = true; break;
        case 2: _link->connected = false; break;
        case 3: _link->channelSet = false; break;
        case 4: vehicle.vehicleLinkManager()->setPrimaryLink({}); break;
        case 5: vehicle.offline = true; break;
        }
        manager._poll();
        QVERIFY(!manager.canControl());
        QVERIFY(!manager.canEmergencyStop());
        QVERIFY(!manager.requestStartFcs(manager.sessionRevision()));
        QVERIFY(!manager.requestEmergencyStopFcs(manager.sessionRevision()));
        QVERIFY(vehicle.sent.isEmpty());
    }

    void sendFailureBeforeAndAfterSubmission()
    {
        Vehicle vehicle;
        bind(vehicle);
        Manager manager;
        ready(manager, vehicle);
        vehicle.sendSucceeds = false;
        QVERIFY(!manager.requestStartFcs(manager.sessionRevision()));
        QCOMPARE(manager.requestState(), Manager::Failed);
        QVERIFY(vehicle.sent.isEmpty());
        vehicle.sendSucceeds = true;
        QVERIFY(manager.requestStartFcs(manager.sessionRevision()));
        vehicle.sendSucceeds = false;
        retry(manager);
        QCOMPARE(manager.requestState(), Manager::Unconfirmed);
        QCOMPARE(vehicle.sent.size(), 1);
    }

    void synchronousAckSeesPendingRequest()
    {
        Vehicle vehicle;
        bind(vehicle);
        Manager manager;
        ready(manager, vehicle);
        vehicle.onSend = [&vehicle](const mavlink_message_t &packet) {
            const auto request = control(packet);
            auto sample = status();
            sample.response_request_id = request.request_id;
            sample.control_command = request.control_command;
            receive(vehicle, sample);
        };
        QVERIFY(manager.requestEmergencyStopFcs(manager.sessionRevision()));
        QCOMPARE(manager.requestState(), Manager::Accepted);
        QVERIFY(!manager.commandPending());
    }

    void vehicleDestructionAllLinksRemovedAndShutdown()
    {
        auto vehicle = std::make_unique<Vehicle>();
        bind(*vehicle);
        Manager manager;
        ready(manager, *vehicle);
        QVERIFY(manager.requestStartFcs(manager.sessionRevision()));
        vehicle.reset();
        QVERIFY(!manager.vehicleAvailable());
        QVERIFY(!manager.telemetryAvailable());
        QCOMPARE(manager.requestState(), Manager::Unconfirmed);
        QVERIFY(manager._vehicleConnections.isEmpty());
        Vehicle next;
        bind(next);
        ready(manager, next);
        emit next.vehicleLinkManager()->allLinksRemoved(&next);
        QVERIFY(!manager.vehicleAvailable());
        bind(next);
        ready(manager, next);
        manager.shutdown();
        QVERIFY(!manager._timer.isActive());
        bind(next);
        receive(next, status());
        QVERIFY(!manager.vehicleAvailable());
        QVERIFY(!manager.telemetryAvailable());
    }

    void requestIdsWrapAndSkipTheFlightControllersCachedId()
    {
        Vehicle vehicle;
        bind(vehicle);
        Manager manager;
        ready(manager, vehicle);
        manager._nextRequestId = std::numeric_limits<quint32>::max() - 1;
        QVERIFY(manager.requestStartFcs(manager.sessionRevision()));
        QCOMPARE(control(vehicle.sent.last()).request_id, std::numeric_limits<quint32>::max());
        auto sample = status();
        sample.response_request_id = control(vehicle.sent.last()).request_id;
        receive(vehicle, sample);
        sample.response_request_id = 1;
        receive(vehicle, sample);
        QVERIFY(manager.requestStopFcs(manager.sessionRevision()));
        QCOMPARE(control(vehicle.sent.last()).request_id, quint32(2));
    }
};

QTEST_GUILESS_MAIN(FuelCellControllerManagerTest)
#include "FuelCellControllerManagerTest.moc"
