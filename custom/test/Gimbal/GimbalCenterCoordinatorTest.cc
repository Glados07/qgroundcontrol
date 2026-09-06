#include "GimbalCenterCoordinator.h"
#include "UniRcChannelPolicy.h"
#include "TestDoubles.h"

#include <QtTest/QTest>

class GimbalCenterCoordinatorTest : public QObject
{
    Q_OBJECT
private slots:
    void cleanup()
    {
        MultiVehicleManager::instance()->setActiveVehicle(nullptr);
    }

    void reacquiresDespiteStaleOwnedCache_data()
    {
        QTest::addColumn<int>("channel7");
        QTest::addColumn<int>("channel8");
        QTest::newRow("lower-bound") << 1400 << 1400;
        QTest::newRow("small-jitter") << 1490 << 1510;
        QTest::newRow("upper-bound") << 1600 << 1600;
    }

    void reacquiresDespiteStaleOwnedCache()
    {
        QFETCH(int, channel7);
        QFETCH(int, channel8);
        Vehicle vehicle;
        MultiVehicleManager::instance()->setActiveVehicle(&vehicle);
        GimbalCenterCoordinator coordinator;
        auto &controller = vehicle.controller;
        bool managerActuallyOwns = true;
        int deniedCount = 0;
        controller.onAcquire = [&]() {
            QTimer::singleShot(0, &vehicle, [&]() {
                managerActuallyOwns = true;
                vehicle.ack(1001);
            });
        };
        controller.onSend = [&]() {
            ++vehicle.sentCount;
            const int result = managerActuallyOwns ? 0 : 2;
            deniedCount += result == 2;
            QTimer::singleShot(0, &vehicle, [&, result]() { vehicle.ack(1000, result); });
        };

        UniRcChannelPolicy policy;
        const auto frame = [&](int ch7, int ch8, int ch10) {
            const auto result = policy.update(ch7, ch8, 1500, ch10, false);
            if (result.manualAttitudeInputDetected) {
                coordinator.noteManualAttitudeInput();
            }
            if (result.ch10Pressed) {
                coordinator.requestNextCh10Action();
            }
        };
        frame(1500, 1500, 1000);
        frame(1500, 1500, 2000);
        QTRY_COMPARE(controller.pitches.size(), 1);
        QTRY_VERIFY(!coordinator.busy());
        QCOMPARE(controller.pitches.last(), 0.0f);

        // External RC takes over; QGC has not received the new manager status.
        // This independent FC state is the fault injection, not a claim that
        // the local CH7/CH8 policy sends or suppresses RC traffic.
        managerActuallyOwns = false;
        frame(channel7, channel8, 1000);
        frame(channel7, channel8, 2000);
        QTRY_COMPARE(controller.pitches.size(), 2);
        QTRY_VERIFY(!coordinator.busy());
        QCOMPARE(controller.pitches.last(), -90.0f);
        QCOMPARE(deniedCount, 0);
        QCOMPARE(controller.acquireCount, 2);
        frame(channel7, channel8, 2000); // Held button cannot repeat.
        QCoreApplication::processEvents();
        QCOMPARE(controller.pitches.size(), 2);
    }

    void ownershipRequiresMatchingConfigureAck()
    {
        Vehicle vehicle;
        MultiVehicleManager::instance()->setActiveVehicle(&vehicle);
        GimbalCenterCoordinator coordinator;
        QVERIFY(coordinator.requestNextCh10Action());
        QCoreApplication::processEvents();
        QCOMPARE(vehicle.controller.acquireCount, 1);
        QVERIFY(vehicle.controller.pitches.isEmpty());
        vehicle.ack(1001, 0, 0, 155);
        emit vehicle.mavCommandResult(2, 154, 1001, 0, 0);
        vehicle.ack(1000);
        QCoreApplication::processEvents();
        QVERIFY(vehicle.controller.pitches.isEmpty());
        vehicle.ack(1001);
        QTRY_COMPARE(vehicle.controller.pitches.size(), 1);
        QVERIFY(coordinator.busy());
        vehicle.ack(1000);
        QVERIFY(!coordinator.busy());
    }

    void pitch90WaitsForAckAndFailureDoesNotAdvance_data()
    {
        QTest::addColumn<int>("result");
        QTest::addColumn<int>("failure");
        QTest::newRow("denied") << 2 << 0;
        QTest::newRow("failed") << 4 << 0;
        QTest::newRow("no-response") << 4 << 1;
        QTest::newRow("local-duplicate") << 4 << 2;
    }

    void pitch90WaitsForAckAndFailureDoesNotAdvance()
    {
        QFETCH(int, result);
        QFETCH(int, failure);
        Vehicle vehicle;
        MultiVehicleManager::instance()->setActiveVehicle(&vehicle);
        GimbalCenterCoordinator coordinator;
        coordinator.noteRecenterCommandDispatched();
        QVERIFY(coordinator.requestNextCh10Action());
        vehicle.ack(1001);
        QTRY_COMPARE(vehicle.controller.pitches.size(), 1);
        QCOMPARE(vehicle.controller.pitches.last(), -90.0f);
        QVERIFY(coordinator.busy());
        coordinator.requestNextCh10Action();
        QCOMPARE(vehicle.controller.pitches.size(), 1);
        vehicle.ack(1000, result, failure);
        QVERIFY(!coordinator.busy());

        QVERIFY(coordinator.requestNextCh10Action());
        vehicle.ack(1001);
        QTRY_COMPARE(vehicle.controller.pitches.size(), 2);
        QCOMPARE(vehicle.controller.pitches.last(), -90.0f);
        vehicle.ack(1000);
        QVERIFY(!coordinator.busy());
        QVERIFY(coordinator.requestNextCh10Action());
        vehicle.ack(1001);
        QTRY_COMPARE(vehicle.controller.pitches.size(), 3);
        QCOMPARE(vehicle.controller.pitches.last(), 0.0f);
        vehicle.ack(1000);
    }

    void acquisitionFailureDoesNotDispatchOrAdvance()
    {
        Vehicle vehicle;
        MultiVehicleManager::instance()->setActiveVehicle(&vehicle);
        GimbalCenterCoordinator coordinator;
        coordinator.noteRecenterCommandDispatched();
        QVERIFY(coordinator.requestNextCh10Action());
        vehicle.ack(1001, 2);
        QVERIFY(!coordinator.busy());
        QVERIFY(vehicle.controller.pitches.isEmpty());
        QVERIFY(coordinator.requestNextCh10Action());
        vehicle.ack(1001);
        QTRY_COMPARE(vehicle.controller.pitches.size(), 1);
        QCOMPARE(vehicle.controller.pitches.last(), -90.0f);
        vehicle.ack(1000);
    }

    void waitsForConsistentOwnershipAndPreservesPrimer_data()
    {
        QTest::addColumn<bool>("ackFirst");
        QTest::newRow("ack-before-status") << true;
        QTest::newRow("status-before-ack") << false;
    }

    void waitsForConsistentOwnershipAndPreservesPrimer()
    {
        QFETCH(bool, ackFirst);
        Vehicle vehicle;
        auto &controller = vehicle.controller;
        controller.activeGimbal()->setOwnership(false, true);
        MultiVehicleManager::instance()->setActiveVehicle(&vehicle);
        GimbalCenterCoordinator coordinator;
        QVERIFY(coordinator.requestCenter());
        if (ackFirst) {
            vehicle.ack(1001);
        } else {
            controller.activeGimbal()->setOwnership(true, false);
        }
        QCoreApplication::processEvents();
        QVERIFY(controller.pitches.isEmpty());
        if (ackFirst) {
            controller.activeGimbal()->setOwnership(true, false);
        } else {
            vehicle.ack(1001);
        }
        QTRY_COMPARE(controller.pitches.size(), 1);
        QCOMPARE(controller.pitches.last(), -1.0f);
        QVERIFY(!controller.errorFlags.last());
        vehicle.ack(1000);
        QTRY_COMPARE(controller.pitches.size(), 2);
        QCOMPARE(controller.pitches.last(), 0.0f);
        QVERIFY(controller.errorFlags.last());
        vehicle.ack(1000);
        QVERIFY(!coordinator.busy());
    }

    void laterManualInputWinsOverCenterAck()
    {
        Vehicle vehicle;
        MultiVehicleManager::instance()->setActiveVehicle(&vehicle);
        GimbalCenterCoordinator coordinator;
        QVERIFY(coordinator.requestNextCh10Action());
        vehicle.ack(1001);
        QTRY_COMPARE(vehicle.controller.pitches.size(), 1);
        // Must invalidate a pending commit even when nextAction is already
        // Recenter: comparing just the enum misses this intervening event.
        coordinator.noteManualAttitudeInput();
        vehicle.ack(1000);
        QVERIFY(coordinator.requestNextCh10Action());
        vehicle.ack(1001);
        QTRY_COMPARE(vehicle.controller.pitches.size(), 2);
        QCOMPARE(vehicle.controller.pitches.last(), 0.0f);
        vehicle.ack(1000);
    }

    void failedCenterDoesNotAdvance()
    {
        Vehicle vehicle;
        MultiVehicleManager::instance()->setActiveVehicle(&vehicle);
        GimbalCenterCoordinator coordinator;
        QVERIFY(coordinator.requestCenter());
        vehicle.ack(1001);
        QTRY_COMPARE(vehicle.controller.pitches.size(), 1);
        vehicle.ack(1000, 2);
        QVERIFY(coordinator.requestNextCh10Action());
        vehicle.ack(1001);
        QTRY_COMPARE(vehicle.controller.pitches.size(), 2);
        QCOMPARE(vehicle.controller.pitches.last(), 0.0f);
        vehicle.ack(1000);
    }

    void cancelledRequestCannotDispatchOnLateAck()
    {
        Vehicle vehicle;
        MultiVehicleManager::instance()->setActiveVehicle(&vehicle);
        GimbalCenterCoordinator coordinator;
        QVERIFY(coordinator.requestNextCh10Action());
        coordinator.cancel();
        vehicle.ack(1001);
        QCoreApplication::processEvents();
        QVERIFY(vehicle.controller.pitches.isEmpty());
        QVERIFY(!coordinator.busy());
    }

    void synchronousDuplicateIsSafe_data()
    {
        QTest::addColumn<bool>("pitch90");
        QTest::addColumn<bool>("duringAcquire");
        QTest::newRow("center-configure") << false << true;
        QTest::newRow("center-final") << false << false;
        QTest::newRow("pitch90-configure") << true << true;
        QTest::newRow("pitch90-final") << true << false;
    }

    void synchronousDuplicateIsSafe()
    {
        QFETCH(bool, pitch90);
        QFETCH(bool, duringAcquire);
        Vehicle vehicle;
        MultiVehicleManager::instance()->setActiveVehicle(&vehicle);
        GimbalCenterCoordinator coordinator;
        if (pitch90) {
            coordinator.noteRecenterCommandDispatched();
        }
        // Native Vehicle emits duplicate-command failure from inside the
        // send call, without incrementing messagesSent.
        if (duringAcquire) {
            vehicle.controller.onAcquire = [&]() { vehicle.ack(1001, 4, 2); };
        } else {
            vehicle.controller.onSend = [&]() { vehicle.ack(1000, 4, 2); };
        }
        QVERIFY(coordinator.requestNextCh10Action());
        if (!duringAcquire) {
            vehicle.ack(1001);
        }
        QTRY_VERIFY(!coordinator.busy());
        QVERIFY(!coordinator.dispatchInProgress());
        QCOMPARE(vehicle.messagesSent(), 0u);
        vehicle.controller.onAcquire = nullptr;
        vehicle.controller.onSend = [&]() { ++vehicle.sentCount; };
        QVERIFY(coordinator.requestNextCh10Action());
        vehicle.ack(1001);
        QTRY_COMPARE(vehicle.messagesSent(), 1u);
        QCOMPARE(vehicle.controller.pitches.last(), pitch90 ? -90.0f : 0.0f);
        vehicle.ack(1000);
    }

    void noLinkDoesNotAdvance()
    {
        Vehicle vehicle;
        MultiVehicleManager::instance()->setActiveVehicle(&vehicle);
        GimbalCenterCoordinator coordinator;
        coordinator.noteRecenterCommandDispatched();
        vehicle.controller.onSend = nullptr; // No link, no counter increment.
        QVERIFY(coordinator.requestNextCh10Action());
        vehicle.ack(1001);
        QTRY_VERIFY(!coordinator.busy());
        vehicle.controller.onSend = [&]() { ++vehicle.sentCount; };
        QVERIFY(coordinator.requestNextCh10Action());
        vehicle.ack(1001);
        QTRY_COMPARE(vehicle.messagesSent(), 1u);
        QCOMPARE(vehicle.controller.pitches.last(), -90.0f);
        vehicle.ack(1000);
    }

    void vehicleSwitchCancelsAndResetsAction()
    {
        Vehicle first;
        Vehicle second(2);
        MultiVehicleManager::instance()->setActiveVehicle(&first);
        GimbalCenterCoordinator coordinator;
        coordinator.noteRecenterCommandDispatched();
        QVERIFY(coordinator.requestNextCh10Action());
        MultiVehicleManager::instance()->setActiveVehicle(&second);
        first.ack(1001);
        QCoreApplication::processEvents();
        QVERIFY(first.controller.pitches.isEmpty());
        QVERIFY(!coordinator.busy());
        QVERIFY(coordinator.requestNextCh10Action());
        second.ack(1001);
        QTRY_COMPARE(second.controller.pitches.size(), 1);
        QCOMPARE(second.controller.pitches.last(), 0.0f);
        second.ack(1000);
    }

    void finalAckTimeoutRetainsAction()
    {
        Vehicle vehicle;
        MultiVehicleManager::instance()->setActiveVehicle(&vehicle);
        GimbalCenterCoordinator coordinator;
        coordinator.noteRecenterCommandDispatched();
        QVERIFY(coordinator.requestNextCh10Action());
        vehicle.ack(1001);
        QTRY_COMPARE(vehicle.controller.pitches.size(), 1);
        QVERIFY(coordinator.busy());
        QTRY_VERIFY_WITH_TIMEOUT(!coordinator.busy(), 5500);
        vehicle.ack(1000); // Late result cannot change the action after timeout.
        QVERIFY(coordinator.requestNextCh10Action());
        vehicle.ack(1001);
        QTRY_COMPARE(vehicle.controller.pitches.size(), 2);
        QCOMPARE(vehicle.controller.pitches.last(), -90.0f);
        vehicle.ack(1000);
    }

    void configureTimeoutNeverDispatchesPosture()
    {
        Vehicle vehicle;
        MultiVehicleManager::instance()->setActiveVehicle(&vehicle);
        GimbalCenterCoordinator coordinator;
        QVERIFY(coordinator.requestNextCh10Action());
        QTRY_VERIFY_WITH_TIMEOUT(!coordinator.busy(), 11500);
        QCOMPARE(vehicle.controller.acquireCount, 1);
        QVERIFY(vehicle.controller.pitches.isEmpty());
        vehicle.ack(1001);
        QCoreApplication::processEvents();
        QVERIFY(vehicle.controller.pitches.isEmpty());
    }
};

QTEST_GUILESS_MAIN(GimbalCenterCoordinatorTest)
#include "GimbalCenterCoordinatorTest.moc"
