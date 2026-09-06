#pragma once

// Test-only transport/telemetry doubles. The tests compile the production
// coordinator, not a second implementation of its state machine.
#include <QtCore/QObject>
#include <QtCore/QVariant>
#include <QtCore/QVector>
#include <functional>

class Fact
{
public:
    explicit Fact(int value = 0) : _value(value) {}
    QVariant rawValue() const { return _value; }
private:
    QVariant _value;
};

class Gimbal : public QObject
{
    Q_OBJECT
public:
    Fact *managerCompid() { return &_managerCompid; }
    Fact *deviceId() { return &_deviceId; }
    Fact *absolutePitch() { return &_pitch; }
    bool gimbalHaveControl() const { return _haveControl; }
    bool gimbalOthersHaveControl() const { return _othersHaveControl; }
    void setOwnership(bool haveControl, bool othersHaveControl)
    {
        // Match the native controller's sequential property notifications.
        if (_haveControl != haveControl) {
            _haveControl = haveControl;
            emit gimbalHaveControlChanged();
        }
        if (_othersHaveControl != othersHaveControl) {
            _othersHaveControl = othersHaveControl;
            emit gimbalOthersHaveControlChanged();
        }
    }
signals:
    void gimbalHaveControlChanged();
    void gimbalOthersHaveControlChanged();
private:
    Fact _managerCompid{154};
    Fact _deviceId{1};
    Fact _pitch{0};
    bool _haveControl = true;
    bool _othersHaveControl = false;
};

class GimbalController : public QObject
{
    Q_OBJECT
public:
    Gimbal *activeGimbal() { return &_gimbal; }
    void acquireGimbalControl()
    {
        ++acquireCount;
        if (onAcquire) {
            onAcquire();
        }
    }
    void centerGimbal() { sendPitchBodyYaw(0.0f, 0.0f); }
    void sendPitchBodyYaw(float pitch, float yaw, bool showError = true)
    {
        if (_gimbal.gimbalOthersHaveControl()) {
            emit showAcquireGimbalControlPopup();
            return;
        }
        if (!_gimbal.gimbalHaveControl()) {
            acquireGimbalControl();
        }
        pitches.append(pitch);
        yaws.append(yaw);
        errorFlags.append(showError);
        if (onSend) {
            onSend();
        }
    }
    int acquireCount = 0;
    QVector<float> pitches;
    QVector<float> yaws;
    QVector<bool> errorFlags;
    std::function<void()> onAcquire;
    std::function<void()> onSend;
signals:
    void activeGimbalChanged();
    void showAcquireGimbalControlPopup();
private:
    Gimbal _gimbal;
};

class Vehicle : public QObject
{
    Q_OBJECT
public:
    explicit Vehicle(int vehicleId = 1) : _id(vehicleId)
    {
        controller.onSend = [this]() { ++sentCount; };
    }
    int id() const { return _id; }
    GimbalController *gimbalController() { return &controller; }
    uint messagesSent() const { return sentCount; }
    void ack(int command, int result = 0, int failure = 0,
             int component = 154)
    {
        emit mavCommandResult(_id, component, command, result, failure);
    }
    uint sentCount = 0;
    GimbalController controller;
signals:
    void mavCommandResult(int vehicleId, int targetComponent, int command,
                          int ackResult, int failureCode);
private:
    int _id;
};

class MultiVehicleManager : public QObject
{
    Q_OBJECT
public:
    static MultiVehicleManager *instance()
    {
        static MultiVehicleManager manager;
        return &manager;
    }
    Vehicle *activeVehicle() const { return _vehicle; }
    void setActiveVehicle(Vehicle *vehicle)
    {
        _vehicle = vehicle;
        emit activeVehicleChanged(vehicle);
    }
signals:
    void activeVehicleChanged(Vehicle *vehicle);
private:
    Vehicle *_vehicle = nullptr;
};
