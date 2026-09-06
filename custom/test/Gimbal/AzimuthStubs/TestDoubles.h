#pragma once

// Only QObject dependencies are doubled. The test builds the production
// provider and decodes messages encoded by the real MAVLink library.
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QVariant>

class Fact : public QObject {
    Q_OBJECT
   public:
    explicit Fact(QVariant value = 0) : _value(value) {}
    QVariant rawValue() const { return _value; }
    void setRawValue(const QVariant &value) {
        if (_value != value) {
            _value = value;
            emit rawValueChanged(value);
        }
    }

   signals:
    void rawValueChanged(const QVariant &value);

   private:
    QVariant _value;
};

class Gimbal : public QObject {
    Q_OBJECT
   public:
    explicit Gimbal(int deviceId = 154, int managerComponentId = 1)
        : _deviceId(deviceId), _managerComponentId(managerComponentId) {}
    Fact *deviceId() { return &_deviceId; }
    Fact *managerCompid() { return &_managerComponentId; }

   private:
    Fact _deviceId;
    Fact _managerComponentId;
};

class GimbalController : public QObject {
    Q_OBJECT
   public:
    Gimbal *activeGimbal() const { return _activeGimbal; }
    void setActiveGimbal(Gimbal *gimbal) {
        _activeGimbal = gimbal;
        emit activeGimbalChanged();
    }

   signals:
    void activeGimbalChanged();

   private:
    Gimbal _defaultGimbal;
    QPointer<Gimbal> _activeGimbal{&_defaultGimbal};
};

class VehicleLinkManager : public QObject {
    Q_OBJECT
   public:
    bool communicationLost() const { return _communicationLost; }
    void setCommunicationLost(bool lost) {
        if (_communicationLost != lost) {
            _communicationLost = lost;
            emit communicationLostChanged(lost);
        }
    }

   signals:
    void communicationLostChanged(bool lost);

   private:
    bool _communicationLost = false;
};

class Vehicle : public QObject {
    Q_OBJECT
   public:
    explicit Vehicle(int vehicleId = 1, int componentId = 1) : _id(vehicleId), _componentId(componentId) {}
    int id() const { return _id; }
    int compId() const { return _componentId; }
    Fact *heading() { return &_displayHeading; }
    GimbalController *gimbalController() { return &_controller; }
    VehicleLinkManager *vehicleLinkManager() { return &_linkManager; }

   private:
    int _id;
    int _componentId;
    Fact _displayHeading{45};
    GimbalController _controller;
    VehicleLinkManager _linkManager;
};

class MultiVehicleManager : public QObject {
    Q_OBJECT
   public:
    static MultiVehicleManager *instance() {
        static MultiVehicleManager manager;
        return &manager;
    }
    Vehicle *activeVehicle() const { return _vehicle; }
    void setActiveVehicle(Vehicle *vehicle) {
        _vehicle = vehicle;
        emit activeVehicleChanged(vehicle);
    }

   signals:
    void activeVehicleChanged(Vehicle *vehicle);

   private:
    QPointer<Vehicle> _vehicle;
};
