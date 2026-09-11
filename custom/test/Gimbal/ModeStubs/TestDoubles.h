#pragma once
#include "MAVLinkLib.h"
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QVariant>

class Fact : public QObject {
    Q_OBJECT
public:
    explicit Fact(int value = 0) : _value(value) {}
    QVariant rawValue() const { return _value; }
    void setRawValue(QVariant value) { _value = value; emit rawValueChanged(value); }
signals:
    void rawValueChanged(const QVariant &value);
private:
    QVariant _value;
};
class QmlObjectListModel : public QObject {
    Q_OBJECT
public:
    int count() const { return _count; }
    void setCount(int count) { _count = count; emit countChanged(count); }
signals:
    void countChanged(int count);
private:
    int _count = 1;
};
class Gimbal : public QObject {
    Q_OBJECT
public:
    Fact *deviceId() { return &_device; }
    Fact *managerCompid() { return &_manager; }
    bool yawLock() const { return _locked; }
    void setYawLock(bool locked) { if (locked != _locked) { _locked = locked; emit yawLockChanged(); } }
signals:
    void yawLockChanged();
private:
    Fact _device{154}, _manager{1};
    bool _locked = false;
};
class GimbalController : public QObject {
    Q_OBJECT
public:
    Gimbal *activeGimbal() const { return _active; }
    QmlObjectListModel *gimbals() { return &_gimbals; }
    void setActiveGimbal(Gimbal *gimbal) { _active = gimbal; emit activeGimbalChanged(); }
signals:
    void activeGimbalChanged();
private:
    Gimbal _default;
    QPointer<Gimbal> _active{&_default};
    QmlObjectListModel _gimbals;
};
class Vehicle;
class VehicleLinkManager : public QObject {
    Q_OBJECT
public:
    bool communicationLost() const { return _lost; }
    void setCommunicationLost(bool lost) { _lost = lost; emit communicationLostChanged(lost); }
signals:
    void communicationLostChanged(bool lost);
    void allLinksRemoved(Vehicle *vehicle);
private:
    bool _lost = false;
};
class Vehicle : public QObject {
    Q_OBJECT
public:
    explicit Vehicle(int id = 1) : _id(id) {}
    int id() const { return _id; }
    int compId() const { return 1; }
    GimbalController *gimbalController() { return &_controller; }
    VehicleLinkManager *vehicleLinkManager() { return &_links; }
    void sendMavCommand(int, MAV_CMD command, bool, float param1) {
        ++queryCount;
        lastCommand = command;
        lastParam = param1;
    }
    int queryCount = 0;
    MAV_CMD lastCommand = MAV_CMD_NAV_WAYPOINT;
    float lastParam = 0;
signals:
    void mavlinkMessageReceived(const mavlink_message_t &message);
private:
    int _id;
    GimbalController _controller;
    VehicleLinkManager _links;
};
class MultiVehicleManager : public QObject {
    Q_OBJECT
public:
    static MultiVehicleManager *instance() { static MultiVehicleManager manager; return &manager; }
    Vehicle *activeVehicle() const { return _active; }
    QmlObjectListModel *vehicles() { return &_vehicles; }
    void setActiveVehicle(Vehicle *vehicle) { _active = vehicle; emit activeVehicleChanged(vehicle); }
signals:
    void activeVehicleChanged(Vehicle *vehicle);
private:
    QPointer<Vehicle> _active;
    QmlObjectListModel _vehicles;
};
class GimbalControlManager : public QObject {
    Q_OBJECT
public:
    bool enabled() const { return _enabled; }
    void setEnabled(bool enabled) { _enabled = enabled; emit enabledChanged(); }
    bool requestGimbalMode(quint64 requestId) { lastRequest = requestId; ++queries; return true; }
    void cancelGimbalModeRequest() { ++cancels; }
    quint64 lastRequest = 0;
    int queries = 0, cancels = 0;
signals:
    void enabledChanged();
    void gimbalModeEndpointChanged();
    void gimbalModeReceived(quint64 requestId, quint8 mode);
private:
    bool _enabled = true;
};
