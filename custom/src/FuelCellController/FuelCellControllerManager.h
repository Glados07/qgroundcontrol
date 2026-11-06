/****************************************************************************
 * FuelCellController telemetry and persistent FCS commands for the active vehicle.
 ****************************************************************************/
#pragma once

#include <QtCore/QElapsedTimer>
#include <QtCore/QList>
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QTimer>
#include <memory>

#include "MAVLinkLib.h"

class LinkInterface;
class Vehicle;

class FuelCellControllerManager final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool vehicleAvailable READ vehicleAvailable NOTIFY statusChanged)
    Q_PROPERTY(bool telemetryAvailable READ telemetryAvailable NOTIFY statusChanged)
    Q_PROPERTY(bool telemetryFresh READ telemetryFresh NOTIFY statusChanged)
    Q_PROPERTY(ConnectionState connectionState READ connectionState NOTIFY statusChanged)
    Q_PROPERTY(bool deviceDataValid READ deviceDataValid NOTIFY statusChanged)
    Q_PROPERTY(double requestedPower READ requestedPower NOTIFY statusChanged)
    Q_PROPERTY(double socPercent READ socPercent NOTIFY statusChanged)
    Q_PROPERTY(double dcdcOutputCurrent READ dcdcOutputCurrent NOTIFY statusChanged)
    Q_PROPERTY(int fcsState READ fcsState NOTIFY statusChanged)
    Q_PROPERTY(int acceptedCommand READ acceptedCommand NOTIFY statusChanged)
    Q_PROPERTY(bool canControl READ canControl NOTIFY statusChanged)
    Q_PROPERTY(bool canResetFault READ canResetFault NOTIFY statusChanged)
    Q_PROPERTY(bool canEmergencyStop READ canEmergencyStop NOTIFY statusChanged)
    Q_PROPERTY(bool resetCooldownActive READ resetCooldownActive NOTIFY statusChanged)
    Q_PROPERTY(RequestState requestState READ requestState NOTIFY requestChanged)
    Q_PROPERTY(bool commandPending READ commandPending NOTIFY requestChanged)
    Q_PROPERTY(int requestedCommand READ requestedCommand NOTIFY requestChanged)
    Q_PROPERTY(bool requestedFaultReset READ requestedFaultReset NOTIFY requestChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY requestChanged)
    Q_PROPERTY(quint32 sessionRevision READ sessionRevision NOTIFY sessionChanged)

public:
    // Unknown is local to QGC: it must not be confused with CAN "never connected".
    enum ConnectionState {
        Unknown = -1,
        NeverConnected = FUEL_CELL_CONNECTION_STATE_NEVER_CONNECTED,
        Connected = FUEL_CELL_CONNECTION_STATE_OK,
        Disconnected = FUEL_CELL_CONNECTION_STATE_ERROR_OCCURRED,
    };
    Q_ENUM(ConnectionState)
    enum RequestState { Idle, Pending, Accepted, Failed, Unconfirmed };
    Q_ENUM(RequestState)

    explicit FuelCellControllerManager(QObject *parent = nullptr);
    bool vehicleAvailable() const;
    bool telemetryAvailable() const { return _hasStatus; }
    bool telemetryFresh() const;
    ConnectionState connectionState() const;
    bool deviceDataValid() const { return connectionState() == Connected; }
    double requestedPower() const;
    double socPercent() const;
    double dcdcOutputCurrent() const;
    int fcsState() const;
    int acceptedCommand() const;
    bool canControl() const;
    bool canResetFault() const;
    bool canEmergencyStop() const;
    bool resetCooldownActive() const;
    RequestState requestState() const { return _requestState; }
    bool commandPending() const { return _requestState == Pending; }
    int requestedCommand() const { return _request.request_id ? _request.control_command : -1; }
    bool requestedFaultReset() const { return _request.request_id && _request.reset_fault == 1; }
    QString lastError() const { return _lastError; }
    quint32 sessionRevision() const { return _sessionRevision; }

    // true means submitted, not that the FCS has completed the requested operation.
    Q_INVOKABLE bool requestStartFcs(quint32 sessionRevision);
    Q_INVOKABLE bool requestStopFcs(quint32 sessionRevision);
    Q_INVOKABLE bool requestEmergencyStopFcs(quint32 sessionRevision);
    Q_INVOKABLE bool requestFaultReset(quint32 sessionRevision);
    void shutdown();

signals:
    void statusChanged();
    void requestChanged();
    void sessionChanged();

private:
    friend class FuelCellControllerManagerTest;
    void _bindVehicle(Vehicle *vehicle);
    void _handleMessage(const mavlink_message_t &message);
    void _poll();
    void _notifyStatus();
    void _finishRequest(RequestState state, const QString &error = {});
    bool _rejectRequest(const QString &error);
    bool _requestCommand(uint8_t command, bool resetFault, quint32 sessionRevision);
    bool _sendRequest();
    std::shared_ptr<LinkInterface> _controlLink() const;
    static QString _resultError(uint8_t result);

    QPointer<Vehicle> _vehicle;
    QMetaObject::Connection _activeVehicleConnection;
    QList<QMetaObject::Connection> _vehicleConnections;
    QElapsedTimer _clock;
    QTimer _timer;
    mavlink_fuel_cell_status_t _status{};
    bool _hasStatus = false;
    qint64 _lastStatusAt = -1;
    bool _notifiedFresh = false;
    bool _notifiedCanControl = false;
    bool _notifiedCanResetFault = false;
    bool _notifiedCanEmergencyStop = false;
    bool _notifiedResetCooldownActive = false;
    quint32 _sessionRevision = 0;

    mavlink_fuel_cell_control_t _request{};
    quint32 _nextRequestId = 0;
    uint8_t _sourceSystem = 0;
    uint8_t _sourceComponent = 0;
    qint64 _firstSendAt = 0;
    qint64 _lastSendAt = 0;
    unsigned _sendCount = 0;
    RequestState _requestState = Idle;
    QString _lastError;
    qint64 _resetCooldownUntil = -1;
};
