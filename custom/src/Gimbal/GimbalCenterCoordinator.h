/****************************************************************************
 *
 * Shared MAVLink gimbal posture sequencing and UniRC CH10 action state.
 *
 ****************************************************************************/

#pragma once

#include "Ch10GimbalActionState.h"

#include <QtCore/QMetaObject>
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QList>
#include <QtCore/QSet>
#include <QtCore/QString>
#include <QtCore/QTimer>

class Gimbal;
class GimbalController;
class Vehicle;

class GimbalCenterCoordinator : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool dispatchInProgress READ dispatchInProgress NOTIFY dispatchInProgressChanged)

public:
    explicit GimbalCenterCoordinator(QObject *parent = nullptr);
    ~GimbalCenterCoordinator() override;

    bool busy() const { return _busy; }
    bool dispatchInProgress() const { return _dispatchInProgress; }

    Q_INVOKABLE bool requestCenter();
    Q_INVOKABLE bool requestNextCh10Action();
    Q_INVOKABLE void noteRecenterCommandDispatched();
    Q_INVOKABLE void notePitch90CommandDispatched();
    Q_INVOKABLE void noteYawLockCommandDispatched();
    void noteManualAttitudeInput();
    Q_INVOKABLE void cancel();

signals:
    void busyChanged();
    void dispatchInProgressChanged();
    void centerRequestStarted();
    void gimbalActionRequestStarted();

private:
    enum class Phase {
        Idle,
        WaitingForOwnership,
        WaitingForPrimerAck,
        SettlingPrimer,
        WaitingForFinalAck,
    };

    void _activeVehicleChanged(Vehicle *vehicle);
    void _activeGimbalChanged();
    void _ownershipChanged();
    void _mavCommandResult(int vehicleId, int targetComponent, int command, int ackResult, int failureCode);
    void _reviewRequest();
    bool _beginRequest(Ch10GimbalActionState::Action action);
    void _sendPrimer();
    void _sendFinalCenter();
    void _sendPitch90();
    void _finishRequest();
    void _logNextCh10ActionChange(bool changed, const char *reason);
    void _resetNextCh10Action(const char *reason);
    void _setBusy(bool busy);
    void _setDispatchInProgress(bool dispatchInProgress);
    void _bindObservedGimbal(Gimbal *gimbal);
    void _clearObservedConnections();
    void _clearRequestConnections();
    bool _requestContextIsCurrent() const;

    static bool _hasConfirmedOwnership(const Gimbal *gimbal);
    static QString _primerKey(const Vehicle *vehicle, Gimbal *gimbal);

    QPointer<Vehicle> _observedVehicle;
    QPointer<GimbalController> _observedController;
    QPointer<Gimbal> _observedGimbal;
    QList<QMetaObject::Connection> _observedConnections;
    QList<QMetaObject::Connection> _observedGimbalConnections;

    QPointer<Vehicle> _requestVehicle;
    QPointer<GimbalController> _requestController;
    QPointer<Gimbal> _requestGimbal;
    QList<QMetaObject::Connection> _requestConnections;

    QSet<QString> _primerRequiredKeys;
    QString _requestPrimerKey;
    Ch10GimbalActionState _ch10ActionState;
    Ch10GimbalActionState::Action _requestAction =
        Ch10GimbalActionState::Action::Recenter;
    Phase _phase = Phase::Idle;
    bool _busy = false;
    bool _dispatchInProgress = false;
    bool _acquireSent = false;
    quint64 _requestGeneration = 0;
    int _requestVehicleId = -1;
    int _requestManagerCompid = -1;

    QTimer _requestTimeout;
    QTimer _primerSettleTimer;
    QTimer _finalAckTimeout;

    static constexpr int kRequestTimeoutMs = 10000;
    static constexpr int kPrimerSettleMs = 400;
    static constexpr int kFinalAckTimeoutMs = 4000;
    static constexpr int kGimbalManagerPitchYawCommand = 1000;
    static constexpr int kMavResultAccepted = 0;
    static constexpr int kCommandResultOnlyFailureCode = 0;
    static constexpr float kPrimerPitchMin = -90.0f;
    static constexpr float kPrimerPitchMax = 0.0f;
    static constexpr float kPrimerPitchStep = 1.0f;
    static constexpr float kPitch90Degrees = -90.0f;
};
