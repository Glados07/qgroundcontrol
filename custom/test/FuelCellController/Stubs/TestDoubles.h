#pragma once

#include "MAVLinkLib.h"
#include <QtCore/QList>
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <functional>
#include <memory>

class LinkInterface {
public:
    bool isConnected() const { return connected; }
    bool isLogReplay() const { return replay; }
    bool mavlinkChannelIsSet() const { return channelSet; }
    uint8_t mavlinkChannel() const { return channel; }
    bool connected = true;
    bool replay = false;
    bool channelSet = true;
    uint8_t channel = 0;
};

class Vehicle;
class VehicleLinkManager : public QObject {
    Q_OBJECT
public:
    bool communicationLost() const { return _lost; }
    std::weak_ptr<LinkInterface> primaryLink() const { return _link; }
    void setPrimaryLink(const std::shared_ptr<LinkInterface> &link) { _link = link; emit primaryLinkChanged(); }
    void setCommunicationLost(bool lost) {
        if (_lost != lost) { _lost = lost; emit communicationLostChanged(lost); }
    }
signals:
    void primaryLinkChanged();
    void communicationLostChanged(bool lost);
    void allLinksRemoved(Vehicle *vehicle);
private:
    bool _lost = false;
    std::weak_ptr<LinkInterface> _link;
};

class Vehicle : public QObject {
    Q_OBJECT
public:
    explicit Vehicle(int system = 1, int component = 1) : _id(system), _component(component) {}
    int id() const { return _id; }
    int compId() const { return _component; }
    bool isOfflineEditingVehicle() const { return offline; }
    VehicleLinkManager *vehicleLinkManager() { return &_links; }
    bool sendMessageOnLinkThreadSafe(LinkInterface *link, mavlink_message_t message) {
        if (!sendSucceeds || !link->isConnected()) { return false; }
        sent.append(message);
        if (onSend) { onSend(message); }
        return true;
    }
    bool offline = false;
    bool sendSucceeds = true;
    QList<mavlink_message_t> sent;
    std::function<void(const mavlink_message_t &)> onSend;
signals:
    void mavlinkMessageReceived(const mavlink_message_t &message);
private:
    int _id;
    int _component;
    VehicleLinkManager _links;
};

class MultiVehicleManager : public QObject {
    Q_OBJECT
public:
    static MultiVehicleManager *instance() { static MultiVehicleManager manager; return &manager; }
    Vehicle *activeVehicle() const { return _active; }
    void setActiveVehicle(Vehicle *vehicle) { _active = vehicle; emit activeVehicleChanged(vehicle); }
signals:
    void activeVehicleChanged(Vehicle *vehicle);
private:
    QPointer<Vehicle> _active;
};

class MAVLinkProtocol {
public:
    static MAVLinkProtocol *instance() { static MAVLinkProtocol protocol; return &protocol; }
    int getSystemId() const { return systemId; }
    static int getComponentId() { return MAV_COMP_ID_MISSIONPLANNER; }
    int systemId = 255;
};
