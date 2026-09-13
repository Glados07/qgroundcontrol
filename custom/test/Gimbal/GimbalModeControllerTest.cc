#include "GimbalModeController.h"
#include "TestDoubles.h"
#include <QtTest/QTest>
#include <QtTest/QSignalSpy>

namespace {
void attitude(Vehicle &vehicle, bool locked, quint32 boot = 100, quint8 component = 154, quint8 device = 0)
{
    mavlink_gimbal_device_attitude_status_t status{};
    status.time_boot_ms = boot;
    status.flags = 12 | (locked ? GIMBAL_DEVICE_FLAGS_YAW_LOCK : 0);
    status.gimbal_device_id = device;
    mavlink_message_t message{};
    mavlink_msg_gimbal_device_attitude_status_encode(vehicle.id(), component, &message, &status);
    emit vehicle.mavlinkMessageReceived(message);
}
}

class GimbalModeControllerTest : public QObject {
    Q_OBJECT
private slots:
    void init() { MultiVehicleManager::instance()->vehicles()->setCount(1); }
    void cleanup() { MultiVehicleManager::instance()->setActiveVehicle(nullptr); }

    void reconnectKeepsPhysicalLockNotDefaultFalse()
    {
        Vehicle vehicle;
        MultiVehicleManager::instance()->setActiveVehicle(&vehicle);
        GimbalControlManager camera;
        GimbalModeController mode(&camera);
        auto *gimbal = vehicle.gimbalController()->activeGimbal();
        QVERIFY(!mode.known());
        attitude(vehicle, false); // Actual legacy failure: flags say Follow.
        QVERIFY(!mode.known());
        QTRY_VERIFY(camera.queries > 0);
        emit camera.gimbalModeReceived(camera.lastRequest, 0); // Actual A8 Lock.
        QCOMPARE(mode.mode(), int(GimbalModeController::Locked));
        QVERIFY(gimbal->yawLock());
        gimbal->setYawLock(false); // Native controller decodes another stale flag.
        attitude(vehicle, false, 200);
        QVERIFY(gimbal->yawLock());
        const auto oldRequest = camera.lastRequest;
        vehicle.vehicleLinkManager()->setCommunicationLost(true);
        QVERIFY(!mode.known());
        emit camera.gimbalModeReceived(oldRequest, 1);
        QVERIFY(!mode.known());
        vehicle.vehicleLinkManager()->setCommunicationLost(false);
        attitude(vehicle, false, 300);
        QVERIFY(!mode.known());
        QTRY_VERIFY(camera.lastRequest != oldRequest);
        emit camera.gimbalModeReceived(oldRequest, 1);
        QVERIFY(!mode.known());
        emit camera.gimbalModeReceived(camera.lastRequest, 0);
        QVERIFY(mode.yawLocked());
        QVERIFY(gimbal->yawLock());
        QCOMPARE(vehicle.queryCount, 0); // No MAVLink posture/configure commands.
    }

    void vehicleReplacementAndEndpointChangeDiscardReplies()
    {
        Vehicle first, second;
        MultiVehicleManager::instance()->setActiveVehicle(&first);
        GimbalControlManager camera;
        GimbalModeController mode(&camera);
        QTRY_VERIFY(camera.queries > 0);
        const auto old = camera.lastRequest;
        MultiVehicleManager::instance()->setActiveVehicle(&second);
        emit camera.gimbalModeReceived(old, 0);
        QVERIFY(!mode.known());
        QTRY_VERIFY(camera.lastRequest != old);
        emit camera.gimbalModeReceived(camera.lastRequest, 0);
        QVERIFY(mode.yawLocked());
        const auto beforeEndpoint = camera.lastRequest;
        emit camera.gimbalModeEndpointChanged();
        QVERIFY(!mode.known());
        emit camera.gimbalModeReceived(beforeEndpoint, 0);
        QVERIFY(!mode.known());
        QTRY_VERIFY(camera.lastRequest != beforeEndpoint);
        emit camera.gimbalModeReceived(camera.lastRequest, 1);
        QVERIFY(mode.known());
        QVERIFY(!mode.yawLocked());
        QVERIFY(!second.gimbalController()->activeGimbal()->yawLock());
    }

    void unknownFollowLockAndFpvAreDistinct()
    {
        Vehicle vehicle;
        MultiVehicleManager::instance()->setActiveVehicle(&vehicle);
        GimbalControlManager camera;
        GimbalModeController mode(&camera);
        for (const auto raw : {0, 1, 2, 255}) {
            const int queries = camera.queries;
            emit camera.gimbalModeEndpointChanged();
            QVERIFY(!mode.known());
            QTRY_VERIFY(camera.queries > queries);
            emit camera.gimbalModeReceived(camera.lastRequest, raw);
            const int expected = raw == 0 ? GimbalModeController::Locked
                : raw == 1 ? GimbalModeController::Follow : raw == 2 ? GimbalModeController::Fpv : GimbalModeController::Unknown;
            QCOMPARE(mode.mode(), expected);
        }
    }

    void sdkTimeoutMustNotFallBackToLegacyFlags()
    {
        Vehicle vehicle;
        MultiVehicleManager::instance()->setActiveVehicle(&vehicle);
        GimbalControlManager camera;
        GimbalModeController mode(&camera);
        QTRY_VERIFY(camera.queries > 0);
        emit camera.gimbalModeReceived(camera.lastRequest, 0);
        QVERIFY(mode.known());
        QTRY_VERIFY_WITH_TIMEOUT(!mode.known(), 4500);
        attitude(vehicle, false);
        QVERIFY(!mode.known());
        emit camera.gimbalModeReceived(camera.lastRequest, 0);
        QVERIFY(mode.yawLocked());
        camera.setEnabled(false);
        QVERIFY(!mode.known());
        attitude(vehicle, false, 200);
        QVERIFY(!mode.known());
    }

    void ambiguousRoutesCannotUseTheSingleA8Endpoint()
    {
        Vehicle vehicle;
        MultiVehicleManager::instance()->setActiveVehicle(&vehicle);
        GimbalControlManager camera;
        GimbalModeController mode(&camera);
        QTRY_VERIFY(camera.queries > 0);
        const auto request = camera.lastRequest;
        MultiVehicleManager::instance()->vehicles()->setCount(2);
        emit camera.gimbalModeReceived(request, 0);
        QVERIFY(!mode.known());
        QTest::qWait(300);
        QCOMPARE(camera.lastRequest, request);
        MultiVehicleManager::instance()->vehicles()->setCount(1);
        vehicle.gimbalController()->gimbals()->setCount(2);
        emit camera.gimbalModeReceived(request, 0);
        QVERIFY(!mode.known());
        QTest::qWait(300);
        QCOMPARE(camera.lastRequest, request);
    }

    void standardDeviceUsesExactFreshMavlinkRoute()
    {
        Vehicle vehicle;
        auto *gimbal = vehicle.gimbalController()->activeGimbal();
        gimbal->deviceId()->setRawValue(155);
        MultiVehicleManager::instance()->setActiveVehicle(&vehicle);
        GimbalControlManager camera;
        GimbalModeController mode(&camera);
        attitude(vehicle, true);
        QVERIFY(!mode.known());
        QTRY_VERIFY(vehicle.queryCount > 0);
        QCOMPARE(vehicle.lastCommand, MAV_CMD_REQUEST_MESSAGE);
        QCOMPARE(vehicle.lastParam, float(MAVLINK_MSG_ID_GIMBAL_DEVICE_ATTITUDE_STATUS));
        attitude(vehicle, true, 100, 155);
        QVERIFY(mode.yawLocked());
        attitude(vehicle, false, 99, 155);
        QVERIFY(mode.yawLocked());
        attitude(vehicle, false, 100, 155);
        QVERIFY(mode.yawLocked());
        attitude(vehicle, false, 101, 155);
        QVERIFY(mode.known());
        QVERIFY(!mode.yawLocked());
        QCOMPARE(camera.queries, 0);
        gimbal->deviceId()->setRawValue(2);
        QVERIFY(!mode.known());
        attitude(vehicle, true, 102, 2, 0); // No standalone fallback for ids 1..6.
        QVERIFY(!mode.known());
        attitude(vehicle, true, 103, 1, 2);
        QVERIFY(mode.yawLocked());
    }

    void destroyedVehicleAndGimbalCannotReceiveOldMode()
    {
        auto *vehicle = new Vehicle;
        MultiVehicleManager::instance()->setActiveVehicle(vehicle);
        GimbalControlManager camera;
        GimbalModeController mode(&camera);
        QTRY_VERIFY(camera.queries > 0);
        const auto request = camera.lastRequest;
        delete vehicle;
        emit camera.gimbalModeReceived(request, 0);
        QVERIFY(!mode.known());
        QVERIFY(!mode.gimbal());
    }

    void lockFollowRoundTripRequiresPhysicalCommandAndReadback()
    {
        Vehicle vehicle;
        MultiVehicleManager::instance()->setActiveVehicle(&vehicle);
        GimbalControlManager camera;
        GimbalModeController mode(&camera);
        auto *gimbal = vehicle.gimbalController()->activeGimbal();
        QTRY_VERIFY(camera.queries > 0);
        emit camera.gimbalModeReceived(camera.lastRequest, camera.physicalMode);
        QVERIFY(mode.yawLocked());
        for (const bool target : {false, true, false}) {
            const auto oldRequest = camera.lastRequest;
            const int writes = camera.modeWrites;
            gimbal->pitchRate = 10;
            gimbal->yawRate = 20;
            QVERIFY(mode.requestYawLock(target, mode.sessionRevision()));
            QVERIFY(mode.commandPending());
            QCOMPARE(gimbal->pitchRate, 0.f);
            QCOMPARE(gimbal->yawRate, 0.f);
            QCOMPARE(gimbal->yawLock(), target); // native commands use desired mode
            QCOMPARE(mode.yawLocked(), !target); // UI still uses actual mode
            QVERIFY(!mode.requestYawLock(!target, mode.sessionRevision())); // no repeated clicks
            QCOMPARE(camera.modeWrites, writes); // no SDK write before ACK
            // Native legacy flags and pre-command SDK replies cannot revert target.
            gimbal->setYawLock(!target);
            emit camera.gimbalModeReceived(oldRequest, target ? 1 : 0);
            QCOMPARE(gimbal->yawLock(), target);
            vehicle.ack(); // manager ACK alone does not change actual UI state
            QCOMPARE(camera.modeWrites, writes + 1);
            QCOMPARE(camera.lastTargetLocked, target);
            QVERIFY(mode.commandPending());
            QCOMPARE(mode.yawLocked(), !target);
            QTRY_VERIFY(camera.lastRequest != oldRequest);
            emit camera.gimbalModeReceived(camera.lastRequest, camera.physicalMode);
            QVERIFY(!mode.commandPending());
            QCOMPARE(mode.yawLocked(), target);
        }
        QCOMPARE(vehicle.gimbalController()->rateSends, 3);
    }

    void explicitTargetSurvivesExpiredSampleDuringOwnershipWait()
    {
        Vehicle vehicle;
        MultiVehicleManager::instance()->setActiveVehicle(&vehicle);
        GimbalControlManager camera;
        GimbalModeController mode(&camera);
        QTRY_VERIFY(camera.queries > 0);
        emit camera.gimbalModeReceived(camera.lastRequest, 0);
        const auto clickSession = mode.sessionRevision();
        QTRY_VERIFY_WITH_TIMEOUT(!mode.known(), 4500);
        QCOMPARE(mode.sessionRevision(), clickSession); // expiry is not reconnection
        QVERIFY(mode.requestYawLock(false, clickSession)); // saved Follow click, not another toggle
        vehicle.ack();
        QCOMPARE(camera.modeWrites, 1);
        QVERIFY(!camera.lastTargetLocked);
    }

    void deniedLostOwnershipBusyOrUnsentCannotWriteA8()
    {
        Vehicle vehicle;
        MultiVehicleManager::instance()->setActiveVehicle(&vehicle);
        GimbalControlManager camera;
        GimbalModeController mode(&camera);
        auto *gimbal = vehicle.gimbalController()->activeGimbal();
        QSignalSpy errors(&mode, &GimbalModeController::commandFailed);
        gimbal->haveControl = false;
        QVERIFY(!mode.requestYawLock(false, mode.sessionRevision()));
        gimbal->haveControl = true;
        vehicle.commandPending = true;
        QVERIFY(!mode.requestYawLock(false, mode.sessionRevision()));
        vehicle.commandPending = false;
        QVERIFY(mode.requestYawLock(false, mode.sessionRevision()));
        vehicle.ack(MAV_RESULT_DENIED);
        QVERIFY(!mode.commandPending());
        QVERIFY(mode.requestYawLock(false, mode.sessionRevision()));
        gimbal->othersHaveControl = true;
        vehicle.ack();
        QVERIFY(!mode.commandPending());
        gimbal->othersHaveControl = false;
        vehicle.gimbalController()->onSend = []() {};
        QVERIFY(!mode.requestYawLock(false, mode.sessionRevision()));
        QCOMPARE(camera.modeWrites, 0);
        QCOMPARE(errors.count(), 5);
    }

    void disconnectEndpointOrSupersedingActionCancelsLateAck()
    {
        Vehicle vehicle, replacement;
        MultiVehicleManager::instance()->setActiveVehicle(&vehicle);
        GimbalControlManager camera;
        GimbalModeController mode(&camera);
        QVERIFY(mode.requestYawLock(true, mode.sessionRevision()));
        vehicle.vehicleLinkManager()->setCommunicationLost(true);
        vehicle.ack();
        QVERIFY(!mode.commandPending());
        vehicle.vehicleLinkManager()->setCommunicationLost(false);
        QVERIFY(mode.requestYawLock(true, mode.sessionRevision()));
        emit camera.gimbalModeEndpointChanged();
        vehicle.ack();
        QVERIFY(!mode.commandPending());
        QVERIFY(mode.requestYawLock(true, mode.sessionRevision()));
        mode.cancelModeCommand();
        vehicle.ack();
        QVERIFY(!mode.commandPending());
        QVERIFY(mode.requestYawLock(true, mode.sessionRevision()));
        MultiVehicleManager::instance()->setActiveVehicle(&replacement);
        vehicle.ack();
        QVERIFY(!mode.commandPending());
        QVERIFY(!replacement.gimbalController()->activeGimbal()->yawLock());
        QCOMPARE(camera.modeWrites, 0);
    }

    void wrongAckSendFailureAndUnchangedFeedbackAreNotSuccess()
    {
        Vehicle vehicle;
        MultiVehicleManager::instance()->setActiveVehicle(&vehicle);
        GimbalControlManager camera;
        GimbalModeController mode(&camera);
        QSignalSpy errors(&mode, &GimbalModeController::commandFailed);
        QTRY_VERIFY(camera.queries > 0);
        emit camera.gimbalModeReceived(camera.lastRequest, 0);
        QVERIFY(mode.requestYawLock(false, mode.sessionRevision()));
        vehicle.ack(MAV_RESULT_ACCEPTED, 0, 154); // wrong component
        QCOMPARE(camera.modeWrites, 0);
        QVERIFY(mode.commandPending());
        camera.writeSucceeds = false;
        vehicle.ack();
        QVERIFY(!mode.commandPending());
        QCOMPARE(errors.count(), 1);
        QVERIFY(mode.yawLocked());
        camera.writeSucceeds = true;
        camera.applyModeWrite = false; // UDP send succeeds, A8 does not execute
        QVERIFY(mode.requestYawLock(false, mode.sessionRevision()));
        vehicle.ack();
        const auto old = camera.lastRequest;
        QTRY_VERIFY(camera.lastRequest != old);
        emit camera.gimbalModeReceived(camera.lastRequest, 0);
        QVERIFY(mode.commandPending());
        QVERIFY(mode.yawLocked());
        QVERIFY(!vehicle.gimbalController()->activeGimbal()->yawLock());
        QTRY_VERIFY_WITH_TIMEOUT(!mode.commandPending(), 6000);
        QCOMPARE(errors.count(), 2);
        QCOMPARE(camera.modeWrites, 2); // no automatic write retry
    }

    void ackTimeoutAndStandardDeviceDoNotUsePrivateSdk()
    {
        Vehicle vehicle;
        auto *gimbal = vehicle.gimbalController()->activeGimbal();
        gimbal->deviceId()->setRawValue(155);
        MultiVehicleManager::instance()->setActiveVehicle(&vehicle);
        GimbalControlManager camera;
        GimbalModeController mode(&camera);
        QSignalSpy errors(&mode, &GimbalModeController::commandFailed);
        attitude(vehicle, true, 100, 155);
        QVERIFY(mode.requestYawLock(false, mode.sessionRevision()));
        QTRY_VERIFY_WITH_TIMEOUT(!mode.commandPending(), 6000);
        QCOMPARE(errors.count(), 1);
        vehicle.ack(); // late result must not dispatch anything
        attitude(vehicle, true, 200, 155);
        QVERIFY(mode.requestYawLock(false, mode.sessionRevision()));
        vehicle.ack();
        QTest::qWait(450);
        attitude(vehicle, false, 300, 155);
        QVERIFY(!mode.commandPending());
        QVERIFY(mode.known());
        QVERIFY(!mode.yawLocked());
        QCOMPARE(camera.modeWrites, 0);
        QCOMPARE(camera.queries, 0);
    }

    void anotherCommandsDuplicateRejectionDoesNotAbortModeChange()
    {
        Vehicle vehicle;
        MultiVehicleManager::instance()->setActiveVehicle(&vehicle);
        GimbalControlManager camera;
        GimbalModeController mode(&camera);
        QSignalSpy errors(&mode, &GimbalModeController::commandFailed);
        QTRY_VERIFY(camera.queries > 0);
        emit camera.gimbalModeReceived(camera.lastRequest, 0);
        const auto oldQuery = camera.lastRequest;
        vehicle.gimbalController()->onSend = [&vehicle]() {
            ++vehicle.sentCount;
            vehicle.commandPending = true;
            // A messagesSentChanged listener can attempt a second send even
            // before the original sendRate() has returned to our caller.
            emit vehicle.mavCommandResult(vehicle.id(), 1, MAV_CMD_DO_GIMBAL_MANAGER_PITCHYAW,
                MAV_RESULT_FAILED, Vehicle::MavCmdResultFailureDuplicateCommand);
        };
        QVERIFY(mode.requestYawLock(false, mode.sessionRevision()));
        QCOMPARE(errors.count(), 0);
        // This is the native Vehicle's local rejection of a SECOND command;
        // the first command is still present in its outstanding command list.
        for (int i = 0; i < 3; ++i) {
            emit vehicle.mavCommandResult(vehicle.id(), 1, MAV_CMD_DO_GIMBAL_MANAGER_PITCHYAW,
                MAV_RESULT_FAILED, Vehicle::MavCmdResultFailureDuplicateCommand);
            QVERIFY(vehicle.commandPending);
            QVERIFY(mode.commandPending());
            QCOMPARE(camera.modeWrites, 0);
            QCOMPARE(errors.count(), 0);
        }
        vehicle.ack(); // original command, not the rejected second command
        QCOMPARE(camera.modeWrites, 1);
        QVERIFY(!camera.lastTargetLocked);
        QVERIFY(mode.commandPending()); // still requires physical feedback
        QTRY_VERIFY(camera.lastRequest != oldQuery);
        emit camera.gimbalModeReceived(camera.lastRequest, 1);
        QVERIFY(!mode.commandPending());
        QVERIFY(!mode.yawLocked());
        QCOMPARE(errors.count(), 0);
    }

    void ownSynchronousDispatchRejectionStillFailsWithoutWritingA8()
    {
        Vehicle vehicle;
        MultiVehicleManager::instance()->setActiveVehicle(&vehicle);
        GimbalControlManager camera;
        GimbalModeController mode(&camera);
        QSignalSpy errors(&mode, &GimbalModeController::commandFailed);
        vehicle.gimbalController()->onSend = [&vehicle]() {
            vehicle.commandPending = true; // another source won the race
            emit vehicle.mavCommandResult(vehicle.id(), 1, MAV_CMD_DO_GIMBAL_MANAGER_PITCHYAW,
                MAV_RESULT_FAILED, Vehicle::MavCmdResultFailureDuplicateCommand);
            // Actual dispatch failed: no increment to messagesSent.
        };
        QVERIFY(!mode.requestYawLock(false, mode.sessionRevision()));
        QVERIFY(!mode.commandPending());
        QCOMPARE(errors.count(), 1);
        QCOMPARE(camera.modeWrites, 0);
        vehicle.ack();
        QCOMPARE(camera.modeWrites, 0);
    }

    void duplicateRejectionsCannotExtendTheAckDeadline()
    {
        Vehicle vehicle;
        MultiVehicleManager::instance()->setActiveVehicle(&vehicle);
        GimbalControlManager camera;
        GimbalModeController mode(&camera);
        QSignalSpy errors(&mode, &GimbalModeController::commandFailed);
        QVERIFY(mode.requestYawLock(false, mode.sessionRevision()));
        QTimer duplicateSender;
        connect(&duplicateSender, &QTimer::timeout, &vehicle, [&vehicle]() {
            emit vehicle.mavCommandResult(vehicle.id(), 1, MAV_CMD_DO_GIMBAL_MANAGER_PITCHYAW,
                MAV_RESULT_FAILED, Vehicle::MavCmdResultFailureDuplicateCommand);
        });
        duplicateSender.start(100);
        QTRY_VERIFY_WITH_TIMEOUT(!mode.commandPending(), 6000);
        QCOMPARE(errors.count(), 1);
        QCOMPARE(camera.modeWrites, 0);
        vehicle.ack();
        QCOMPARE(camera.modeWrites, 0);
    }

    void sessionChangesRejectQueuedTargetsEvenOnTheSameObjects()
    {
        Vehicle vehicle;
        MultiVehicleManager::instance()->setActiveVehicle(&vehicle);
        GimbalControlManager camera;
        GimbalModeController mode(&camera);
        auto *gimbal = vehicle.gimbalController()->activeGimbal();
        QSignalSpy sessions(&mode, &GimbalModeController::sessionChanged);
        const auto rejectAfterReset = [&](quint32 oldSession) {
            QVERIFY(mode.sessionRevision() != oldSession);
            QCOMPARE(mode.gimbal(), static_cast<QObject *>(gimbal));
            QVERIFY(!mode.requestYawLock(false, oldSession));
            QCOMPARE(vehicle.sentCount, uint(0));
            QCOMPARE(camera.modeWrites, 0);
        };
        auto clickSession = mode.sessionRevision();
        vehicle.vehicleLinkManager()->setCommunicationLost(true);
        vehicle.vehicleLinkManager()->setCommunicationLost(false);
        rejectAfterReset(clickSession);
        clickSession = mode.sessionRevision();
        emit camera.gimbalModeEndpointChanged();
        rejectAfterReset(clickSession);
        clickSession = mode.sessionRevision();
        camera.setEnabled(false);
        camera.setEnabled(true);
        rejectAfterReset(clickSession);
        clickSession = mode.sessionRevision();
        gimbal->deviceId()->setRawValue(155);
        rejectAfterReset(clickSession);
        clickSession = mode.sessionRevision();
        gimbal->managerCompid()->setRawValue(2);
        rejectAfterReset(clickSession);
        QVERIFY(sessions.count() >= 7);
        // Only a NEW explicit click in the new session can send a command.
        QVERIFY(mode.requestYawLock(false, mode.sessionRevision()));
        QCOMPARE(vehicle.sentCount, uint(1));
    }
};
QTEST_GUILESS_MAIN(GimbalModeControllerTest)
#include "GimbalModeControllerTest.moc"
