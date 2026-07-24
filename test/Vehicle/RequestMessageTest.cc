/****************************************************************************
 *
 * (c) 2009-2020 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "RequestMessageTest.h"
#include "MultiVehicleManager.h"
#include "MockLink.h"

#include <QtTest/QTest>

RequestMessageTest::TestCase_t RequestMessageTest::_rgTestCases[] = {
    { MockLink::FailRequestMessageNone,                      MAV_RESULT_ACCEPTED,    Vehicle::RequestMessageNoFailure,                 1,                                 MAVLINK_MSG_ID_DEBUG, false, 0 },
    { MockLink::FailRequestMessageCommandAcceptedMsgNotSent, MAV_RESULT_FAILED,      Vehicle::RequestMessageFailureMessageNotReceived, 1,                                 MAVLINK_MSG_ID_DEBUG, false, 0 },
    { MockLink::FailRequestMessageCommandUnsupported,        MAV_RESULT_UNSUPPORTED, Vehicle::RequestMessageFailureCommandError,       1,                                 MAVLINK_MSG_ID_DEBUG, false, 0 },
};

void RequestMessageTest::_requestMessageResultHandler(void* resultHandlerData, MAV_RESULT commandResult, Vehicle::RequestMessageResultHandlerFailureCode_t failureCode, const mavlink_message_t& message)
{
    TestCase_t* testCase = static_cast<TestCase_t*>(resultHandlerData);

    testCase->resultHandlerCalled = true;
    testCase->callbackCount++;
    QCOMPARE((int)testCase->expectedCommandResult, (int)commandResult);
    QCOMPARE((int)testCase->expectedFailureCode, (int)failureCode);
    if (testCase->expectedFailureCode == Vehicle::RequestMessageNoFailure) {
        QCOMPARE((int)message.msgid, testCase->expectedMessageId);
    }
}

void RequestMessageTest::_testCaseWorker(TestCase_t& testCase)
{
    _connectMockLinkNoInitialConnectSequence();

    MultiVehicleManager*    vehicleMgr  = MultiVehicleManager::instance();
    Vehicle*                vehicle     = vehicleMgr->activeVehicle();
    
    // Gimbal controller sends message requests when receiving heartbeats, trying to find a gimbal, and it messes with this test so we disable it
    vehicle->deleteGimbalController();

    // Camera manager also messes with it.
    vehicle->deleteCameraManager();

    _mockLink->clearReceivedMavCommandCounts();
    _mockLink->setRequestMessageFailureMode(testCase.failureMode);

    vehicle->requestMessage(_requestMessageResultHandler, &testCase, MAV_COMP_ID_AUTOPILOT1, MAVLINK_MSG_ID_DEBUG);
    QVERIFY(QTest::qWaitFor([&]() { return testCase.resultHandlerCalled; }, 10000));
    QCOMPARE(testCase.callbackCount, 1);
    QTRY_COMPARE_WITH_TIMEOUT(vehicle->_findMavCommandListEntryIndex(MAV_COMP_ID_AUTOPILOT1, MAV_CMD_REQUEST_MESSAGE), -1, 1000);
    QCOMPARE(_mockLink->receivedMavCommandCount(MAV_CMD_REQUEST_MESSAGE), testCase.expectedSendCount);

    // We should be able to do it twice in a row without any duplicate command problems
    testCase.resultHandlerCalled = false;
    testCase.callbackCount = 0;
    _mockLink->clearReceivedMavCommandCounts();
    vehicle->requestMessage(_requestMessageResultHandler, &testCase, MAV_COMP_ID_AUTOPILOT1, MAVLINK_MSG_ID_DEBUG);
    QVERIFY(QTest::qWaitFor([&]() { return testCase.resultHandlerCalled; }, 10000));
    QCOMPARE(testCase.callbackCount, 1);
    QTRY_COMPARE_WITH_TIMEOUT(vehicle->_findMavCommandListEntryIndex(MAV_COMP_ID_AUTOPILOT1, MAV_CMD_REQUEST_MESSAGE), -1, 1000);
    QCOMPARE(_mockLink->receivedMavCommandCount(MAV_CMD_REQUEST_MESSAGE), testCase.expectedSendCount);

    _disconnectMockLink();
}

void RequestMessageTest::_performTestCases(void)
{
    int index = 0;
    for (TestCase_t& testCase: _rgTestCases) {
        qDebug() << "Testing case" << index++;
        _testCaseWorker(testCase);
    }
}

void RequestMessageTest::_duplicateCommand(void)
{
    _connectMockLinkNoInitialConnectSequence();

    TestCase_t firstRequest = {
        MockLink::FailRequestMessageCommandAcceptedMsgNotSent,
        MAV_RESULT_FAILED,
        Vehicle::RequestMessageFailureMessageNotReceived,
        1,
        MAVLINK_MSG_ID_DEBUG,
        false,
        0
    };
    TestCase_t duplicateRequest = {
        MockLink::FailRequestMessageCommandAcceptedMsgNotSent,
        MAV_RESULT_FAILED,
        Vehicle::RequestMessageFailureDuplicateCommand,
        1,
        MAVLINK_MSG_ID_DEBUG,
        false,
        0
    };

    MultiVehicleManager*    vehicleMgr  = MultiVehicleManager::instance();
    Vehicle*                vehicle     = vehicleMgr->activeVehicle();

    vehicle->deleteGimbalController();
    vehicle->deleteCameraManager();

    _mockLink->clearReceivedMavCommandCounts();
    _mockLink->setRequestMessageFailureMode(firstRequest.failureMode);

    QTRY_VERIFY_WITH_TIMEOUT(!vehicle->isMavCommandPending(MAV_COMP_ID_AUTOPILOT1, MAV_CMD_REQUEST_MESSAGE), 1000);

    vehicle->requestMessage(_requestMessageResultHandler, &firstRequest, MAV_COMP_ID_AUTOPILOT1, MAVLINK_MSG_ID_DEBUG);
    QVERIFY(QTest::qWaitFor([&]() { return _mockLink->receivedMavCommandCount(MAV_CMD_REQUEST_MESSAGE) == 1; }, 100));
    QCOMPARE(firstRequest.resultHandlerCalled, false);

    vehicle->requestMessage(_requestMessageResultHandler, &duplicateRequest, MAV_COMP_ID_AUTOPILOT1, MAVLINK_MSG_ID_DEBUG);

    QCOMPARE(duplicateRequest.resultHandlerCalled, true);
    QCOMPARE(duplicateRequest.callbackCount, 1);
    QCOMPARE(_mockLink->receivedMavCommandCount(MAV_CMD_REQUEST_MESSAGE), duplicateRequest.expectedSendCount);

    QVERIFY(QTest::qWaitFor([&]() { return firstRequest.resultHandlerCalled; }, 3000));
    QCOMPARE(firstRequest.callbackCount, 1);
    QTRY_VERIFY_WITH_TIMEOUT(!vehicle->isMavCommandPending(MAV_COMP_ID_AUTOPILOT1, MAV_CMD_REQUEST_MESSAGE), 1000);

    _disconnectMockLink();
}

void RequestMessageTest::_compIdAllRequestMessageResultHandler(void* resultHandlerData, MAV_RESULT commandResult, Vehicle::RequestMessageResultHandlerFailureCode_t failureCode, const mavlink_message_t& /*message*/)
{
    TestCase_t* testCase = static_cast<TestCase_t*>(resultHandlerData);

    testCase->resultHandlerCalled = true;
    QCOMPARE((int)testCase->expectedCommandResult, (int)commandResult);
    QCOMPARE((int)testCase->expectedFailureCode, (int)failureCode);
}

void RequestMessageTest::_compIdAllFailure(void)
{
    _connectMockLinkNoInitialConnectSequence();

    RequestMessageTest::TestCase_t testCase = {
        MockLink::FailRequestMessageCommandNoResponse, MAV_RESULT_FAILED, Vehicle::RequestMessageFailureCommandError, 0, MAVLINK_MSG_ID_DEBUG, false, 0
    };

    MultiVehicleManager*    vehicleMgr  = MultiVehicleManager::instance();
    Vehicle*                vehicle     = vehicleMgr->activeVehicle();

    _mockLink->clearReceivedMavCommandCounts();
    _mockLink->setRequestMessageFailureMode(testCase.failureMode);

    vehicle->requestMessage(_requestMessageResultHandler, &testCase, MAV_COMP_ID_ALL, MAVLINK_MSG_ID_DEBUG);
    QCOMPARE(testCase.resultHandlerCalled, true);
    QCOMPARE(vehicle->_findMavCommandListEntryIndex(MAV_COMP_ID_ALL, MAV_CMD_REQUEST_MESSAGE), -1);
    QCOMPARE(_mockLink->receivedMavCommandCount(MAV_CMD_REQUEST_MESSAGE), 0);

    _disconnectMockLink();
}

void RequestMessageTest::_differentMessageQueued(void)
{
    _connectMockLinkNoInitialConnectSequence();

    MultiVehicleManager* vehicleMgr = MultiVehicleManager::instance();
    Vehicle* vehicle = vehicleMgr->activeVehicle();

    vehicle->deleteGimbalController();
    vehicle->deleteCameraManager();

    TestCase_t firstRequest = {
        MockLink::FailRequestMessageCommandAcceptedMsgNotSent,
        MAV_RESULT_FAILED,
        Vehicle::RequestMessageFailureMessageNotReceived,
        1,
        MAVLINK_MSG_ID_DEBUG,
        false,
        0
    };
    TestCase_t queuedRequest = {
        MockLink::FailRequestMessageNone,
        MAV_RESULT_ACCEPTED,
        Vehicle::RequestMessageNoFailure,
        2,
        MAVLINK_MSG_ID_AUTOPILOT_VERSION,
        false,
        0
    };

    _mockLink->clearReceivedMavCommandCounts();
    _mockLink->setRequestMessageFailureMode(firstRequest.failureMode);

    vehicle->requestMessage(_requestMessageResultHandler, &firstRequest, MAV_COMP_ID_AUTOPILOT1, firstRequest.expectedMessageId);
    QVERIFY(QTest::qWaitFor([&]() { return _mockLink->receivedMavCommandCount(MAV_CMD_REQUEST_MESSAGE) == 1; }, 100));

    vehicle->requestMessage(_requestMessageResultHandler, &queuedRequest, MAV_COMP_ID_AUTOPILOT1, queuedRequest.expectedMessageId);
    QCOMPARE(queuedRequest.resultHandlerCalled, false);
    QCOMPARE(_mockLink->receivedMavCommandCount(MAV_CMD_REQUEST_MESSAGE), 1);

    QVERIFY(QTest::qWaitFor([&]() { return firstRequest.resultHandlerCalled; }, 3000));
    QCOMPARE(firstRequest.callbackCount, 1);

    QVERIFY(QTest::qWaitFor([&]() { return queuedRequest.resultHandlerCalled; }, 1000));
    QCOMPARE(queuedRequest.callbackCount, 1);
    QCOMPARE(_mockLink->receivedMavCommandCount(MAV_CMD_REQUEST_MESSAGE), queuedRequest.expectedSendCount);
    QVERIFY(!vehicle->isMavCommandPending(MAV_COMP_ID_AUTOPILOT1, MAV_CMD_REQUEST_MESSAGE));

    _disconnectMockLink();
}
