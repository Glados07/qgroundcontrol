#include "GimbalModeController.h"
#include "TestDoubles.h"
#include <QtTest/QTest>

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
            mode.noteModeCommandDispatched();
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
};
QTEST_GUILESS_MAIN(GimbalModeControllerTest)
#include "GimbalModeControllerTest.moc"
