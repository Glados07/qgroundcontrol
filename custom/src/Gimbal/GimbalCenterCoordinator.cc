/****************************************************************************
 *
 * Reliable MAVLink gimbal-center sequencing used by custom input surfaces.
 *
 ****************************************************************************/

#include "GimbalCenterCoordinator.h"

#include "Fact.h"
#include "Gimbal.h"
#include "GimbalController.h"
#include "MultiVehicleManager.h"
#include "Vehicle.h"

#include <QtCore/QLoggingCategory>
#include <QtCore/QtGlobal>

#include <utility>

Q_LOGGING_CATEGORY(GimbalCenterCoordinatorLog, "qgc.custom.gimbal.center")

GimbalCenterCoordinator::GimbalCenterCoordinator(QObject *parent)
    : QObject(parent)
{
    _requestTimeout.setSingleShot(true);
    _requestTimeout.setInterval(kRequestTimeoutMs);
    connect(&_requestTimeout, &QTimer::timeout, this, &GimbalCenterCoordinator::cancel);

    _primerSettleTimer.setSingleShot(true);
    _primerSettleTimer.setInterval(kPrimerSettleMs);
    connect(&_primerSettleTimer, &QTimer::timeout, this, [this]() {
        if ((_phase != Phase::SettlingPrimer) || !_requestContextIsCurrent()
            || !_hasConfirmedOwnership(_requestGimbal)) {
            cancel();
            return;
        }
        _sendFinalCenter();
    });

    _finalAckTimeout.setSingleShot(true);
    _finalAckTimeout.setInterval(kFinalAckTimeoutMs);
    connect(&_finalAckTimeout, &QTimer::timeout, this, &GimbalCenterCoordinator::cancel);

    MultiVehicleManager *const vehicleManager = MultiVehicleManager::instance();
    connect(vehicleManager, &MultiVehicleManager::activeVehicleChanged,
            this, &GimbalCenterCoordinator::_activeVehicleChanged);
    _activeVehicleChanged(vehicleManager->activeVehicle());
}

GimbalCenterCoordinator::~GimbalCenterCoordinator()
{
    cancel();
    _clearObservedConnections();
}

bool GimbalCenterCoordinator::requestCenter()
{
    if (_busy) {
        return _requestContextIsCurrent();
    }

    Vehicle *const vehicle = MultiVehicleManager::instance()->activeVehicle();
    GimbalController *const controller = vehicle ? vehicle->gimbalController() : nullptr;
    Gimbal *const gimbal = controller ? controller->activeGimbal() : nullptr;
    if (!vehicle || !controller || !gimbal) {
        return false;
    }

    _requestVehicle = vehicle;
    _requestController = controller;
    _requestGimbal = gimbal;
    _requestVehicleId = vehicle->id();
    _requestManagerCompid = gimbal->managerCompid()->rawValue().toInt();
    _requestPrimerKey = _primerKey(vehicle, gimbal);
    _acquireSent = false;
    ++_requestGeneration;

    _requestConnections.append(connect(vehicle, &Vehicle::mavCommandResult,
                                       this, &GimbalCenterCoordinator::_mavCommandResult));
    _requestConnections.append(connect(vehicle, &QObject::destroyed, this, &GimbalCenterCoordinator::cancel));
    _requestConnections.append(connect(controller, &QObject::destroyed, this, &GimbalCenterCoordinator::cancel));
    _requestConnections.append(connect(gimbal, &QObject::destroyed, this, &GimbalCenterCoordinator::cancel));

    _setBusy(true);
    emit centerRequestStarted();

    const bool hasOwnership = _hasConfirmedOwnership(gimbal);
    if (!hasOwnership) {
        if (!_requestPrimerKey.isEmpty()) {
            _primerRequiredKeys.insert(_requestPrimerKey);
        }
        _phase = Phase::WaitingForOwnership;
        _requestTimeout.start();
        _acquireSent = true;
        controller->acquireGimbalControl();
        QTimer::singleShot(0, this, &GimbalCenterCoordinator::_reviewRequest);
        return true;
    }

    if (_primerRequiredKeys.contains(_requestPrimerKey)) {
        _phase = Phase::WaitingForPrimerAck;
        _requestTimeout.start();
        _sendPrimer();
        return true;
    }

    _sendFinalCenter();
    return true;
}

void GimbalCenterCoordinator::cancel()
{
    if (!_busy) {
        return;
    }
    _finishRequest();
}

void GimbalCenterCoordinator::_activeVehicleChanged(Vehicle *vehicle)
{
    if (_busy && (vehicle != _requestVehicle)) {
        cancel();
    }

    _clearObservedConnections();
    _observedVehicle = vehicle;
    _observedController = vehicle ? vehicle->gimbalController() : nullptr;
    _observedGimbal = _observedController ? _observedController->activeGimbal() : nullptr;

    if (_observedVehicle) {
        _observedConnections.append(connect(_observedVehicle, &QObject::destroyed, this, [this]() {
            if (_busy) {
                cancel();
            }
            _activeVehicleChanged(nullptr);
        }));
    }
    if (_observedController) {
        _observedConnections.append(connect(_observedController, &GimbalController::activeGimbalChanged,
                                            this, &GimbalCenterCoordinator::_activeGimbalChanged));
        _observedConnections.append(connect(_observedController, &GimbalController::showAcquireGimbalControlPopup,
                                            this, [this]() {
            if (_busy && _dispatchInProgress) {
                const quint64 generation = _requestGeneration;
                QTimer::singleShot(0, this, [this, generation]() {
                    if (_busy && (_requestGeneration == generation)) {
                        cancel();
                    }
                });
            }
        }));
        _observedConnections.append(connect(_observedController, &QObject::destroyed, this, [this]() {
            if (_busy) {
                cancel();
            }
            _observedController.clear();
            _bindObservedGimbal(nullptr);
        }));
    }
    _bindObservedGimbal(_observedGimbal);
}

void GimbalCenterCoordinator::_activeGimbalChanged()
{
    Gimbal *const gimbal = _observedController ? _observedController->activeGimbal() : nullptr;
    if (_busy && (gimbal != _requestGimbal)) {
        cancel();
    }
    _bindObservedGimbal(gimbal);
}

void GimbalCenterCoordinator::_ownershipChanged()
{
    if (!_hasConfirmedOwnership(_observedGimbal)) {
        const QString key = _primerKey(_observedVehicle, _observedGimbal);
        if (!key.isEmpty()) {
            _primerRequiredKeys.insert(key);
        }
    }

    if (!_busy || !_requestContextIsCurrent()) {
        return;
    }

    if (_phase == Phase::WaitingForOwnership) {
        QTimer::singleShot(0, this, &GimbalCenterCoordinator::_reviewRequest);
    } else if (!_hasConfirmedOwnership(_requestGimbal)) {
        cancel();
    }
}

void GimbalCenterCoordinator::_mavCommandResult(int vehicleId, int targetComponent, int command,
                                                int ackResult, int failureCode)
{
    if (!_busy
        || (vehicleId != _requestVehicleId)
        || (targetComponent != _requestManagerCompid)
        || (command != kGimbalManagerPitchYawCommand)) {
        return;
    }

    const bool accepted = (ackResult == kMavResultAccepted)
                          && (failureCode == kCommandResultOnlyFailureCode);
    if (_phase == Phase::WaitingForPrimerAck) {
        if (!accepted) {
            cancel();
            return;
        }
        _phase = Phase::SettlingPrimer;
        _primerSettleTimer.start();
        return;
    }

    if (_phase == Phase::WaitingForFinalAck) {
        if (accepted && !_requestPrimerKey.isEmpty()) {
            _primerRequiredKeys.remove(_requestPrimerKey);
        }
        _finishRequest();
    }
}

void GimbalCenterCoordinator::_reviewRequest()
{
    if (!_busy || (_phase != Phase::WaitingForOwnership)) {
        return;
    }
    if (!_requestContextIsCurrent()) {
        cancel();
        return;
    }
    if (!_hasConfirmedOwnership(_requestGimbal)) {
        if (!_acquireSent) {
            _acquireSent = true;
            _requestController->acquireGimbalControl();
        }
        return;
    }

    _phase = Phase::WaitingForPrimerAck;
    _sendPrimer();
}

void GimbalCenterCoordinator::_sendPrimer()
{
    if (!_busy || !_requestContextIsCurrent() || !_hasConfirmedOwnership(_requestGimbal)) {
        cancel();
        return;
    }

    float currentPitch = _requestGimbal->absolutePitch()->rawValue().toFloat();
    if (!qIsFinite(currentPitch)) {
        currentPitch = kPrimerPitchMax;
    }
    const float boundedPitch = qBound(kPrimerPitchMin, currentPitch, kPrimerPitchMax);
    const float primerPitch = (boundedPitch <= (kPrimerPitchMax - (2.0f * kPrimerPitchStep)))
                                 ? boundedPitch + kPrimerPitchStep
                                 : boundedPitch - kPrimerPitchStep;

    _phase = Phase::WaitingForPrimerAck;
    _setDispatchInProgress(true);
    _requestController->sendPitchBodyYaw(primerPitch, 0.0f, false);
    _setDispatchInProgress(false);
}

void GimbalCenterCoordinator::_sendFinalCenter()
{
    if (!_busy || !_requestContextIsCurrent() || !_hasConfirmedOwnership(_requestGimbal)) {
        cancel();
        return;
    }

    _requestTimeout.stop();
    _primerSettleTimer.stop();
    _phase = Phase::WaitingForFinalAck;
    _setDispatchInProgress(true);
    _requestController->centerGimbal();
    _setDispatchInProgress(false);

    // A matching command result may be emitted synchronously (for example a
    // duplicate-command rejection), so only arm the timer if the request is
    // still in its final phase after dispatch returns.
    if (_busy && (_phase == Phase::WaitingForFinalAck)) {
        _finalAckTimeout.start();
    }
}

void GimbalCenterCoordinator::_finishRequest()
{
    _requestTimeout.stop();
    _primerSettleTimer.stop();
    _finalAckTimeout.stop();
    _clearRequestConnections();
    _requestVehicle.clear();
    _requestController.clear();
    _requestGimbal.clear();
    _requestPrimerKey.clear();
    _requestVehicleId = -1;
    _requestManagerCompid = -1;
    _acquireSent = false;
    _phase = Phase::Idle;
    _setDispatchInProgress(false);
    _setBusy(false);
}

void GimbalCenterCoordinator::_setBusy(bool busy)
{
    if (_busy == busy) {
        return;
    }
    _busy = busy;
    emit busyChanged();
}

void GimbalCenterCoordinator::_setDispatchInProgress(bool dispatchInProgress)
{
    if (_dispatchInProgress == dispatchInProgress) {
        return;
    }
    _dispatchInProgress = dispatchInProgress;
    emit dispatchInProgressChanged();
}

void GimbalCenterCoordinator::_bindObservedGimbal(Gimbal *gimbal)
{
    for (const QMetaObject::Connection &connection : std::as_const(_observedGimbalConnections)) {
        QObject::disconnect(connection);
    }
    _observedGimbalConnections.clear();

    _observedGimbal = gimbal;
    if (!_observedGimbal) {
        return;
    }
    _observedGimbalConnections.append(connect(_observedGimbal, &Gimbal::gimbalHaveControlChanged,
                                              this, &GimbalCenterCoordinator::_ownershipChanged));
    _observedGimbalConnections.append(connect(_observedGimbal, &Gimbal::gimbalOthersHaveControlChanged,
                                              this, &GimbalCenterCoordinator::_ownershipChanged));
    _observedGimbalConnections.append(connect(_observedGimbal, &QObject::destroyed, this, [this]() {
        if (_busy) {
            cancel();
        }
        _observedGimbal.clear();
    }));

    if (!_hasConfirmedOwnership(_observedGimbal)) {
        const QString key = _primerKey(_observedVehicle, _observedGimbal);
        if (!key.isEmpty()) {
            _primerRequiredKeys.insert(key);
        }
    }
}

void GimbalCenterCoordinator::_clearObservedConnections()
{
    for (const QMetaObject::Connection &connection : std::as_const(_observedGimbalConnections)) {
        QObject::disconnect(connection);
    }
    _observedGimbalConnections.clear();
    for (const QMetaObject::Connection &connection : std::as_const(_observedConnections)) {
        QObject::disconnect(connection);
    }
    _observedConnections.clear();
    _observedVehicle.clear();
    _observedController.clear();
    _observedGimbal.clear();
}

void GimbalCenterCoordinator::_clearRequestConnections()
{
    for (const QMetaObject::Connection &connection : std::as_const(_requestConnections)) {
        QObject::disconnect(connection);
    }
    _requestConnections.clear();
}

bool GimbalCenterCoordinator::_requestContextIsCurrent() const
{
    Vehicle *const activeVehicle = MultiVehicleManager::instance()->activeVehicle();
    GimbalController *const controller = activeVehicle ? activeVehicle->gimbalController() : nullptr;
    return activeVehicle
           && (activeVehicle == _requestVehicle)
           && (controller == _requestController)
           && (controller->activeGimbal() == _requestGimbal);
}

bool GimbalCenterCoordinator::_hasConfirmedOwnership(const Gimbal *gimbal)
{
    return gimbal && gimbal->gimbalHaveControl() && !gimbal->gimbalOthersHaveControl();
}

QString GimbalCenterCoordinator::_primerKey(const Vehicle *vehicle, Gimbal *gimbal)
{
    if (!vehicle || !gimbal) {
        return {};
    }
    return QStringLiteral("%1:%2:%3")
        .arg(vehicle->id())
        .arg(gimbal->managerCompid()->rawValue().toUInt())
        .arg(gimbal->deviceId()->rawValue().toUInt());
}
