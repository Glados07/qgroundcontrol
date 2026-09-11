/****************************************************************************
 * Confirmed gimbal mode, independent of the azimuth/reference-frame pipeline.
 ****************************************************************************/
#pragma once

#include <QtCore/QElapsedTimer>
#include <QtCore/QList>
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QTimer>

#include "MAVLinkLib.h"

class Gimbal;
class GimbalController;
class GimbalControlManager;
class Vehicle;

class GimbalModeController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool known READ known NOTIFY modeChanged)
    Q_PROPERTY(bool yawLocked READ yawLocked NOTIFY modeChanged)
    Q_PROPERTY(int mode READ mode NOTIFY modeChanged)
    Q_PROPERTY(QObject *gimbal READ gimbal NOTIFY modeChanged)

public:
    enum Mode { Unknown, Follow, Locked, Fpv };
    explicit GimbalModeController(GimbalControlManager *camera, QObject *parent = nullptr);
    ~GimbalModeController() override;
    bool known() const { return _mode != Unknown; }
    bool yawLocked() const { return _mode == Locked; }
    int mode() const { return _mode; }
    QObject *gimbal() const;
    Q_INVOKABLE void noteModeCommandDispatched();

signals:
    void modeChanged();

private:
    void _bindVehicle(Vehicle *vehicle);
    void _bindGimbal();
    void _reset();
    void _poll();
    void _handleMessage(const mavlink_message_t &message);
    void _handleSdkMode(quint64 requestId, quint8 mode);
    void _publish(Mode mode, const char *source);
    void _applyToNative();
    bool _connected() const;
    bool _isProductA8Route() const;
    bool _canQueryA8() const;
    static void _disconnect(QList<QMetaObject::Connection> &connections);

    QPointer<GimbalControlManager> _camera;
    QPointer<Vehicle> _vehicle;
    QPointer<GimbalController> _controller;
    QPointer<Gimbal> _gimbal;
    QList<QMetaObject::Connection> _vehicleConnections;
    QList<QMetaObject::Connection> _gimbalConnections;
    QElapsedTimer _clock;
    QTimer _timer;
    Mode _mode = Unknown;
    Mode _sampleMode = Unknown;
    qint64 _sampleAt = -1;
    qint64 _lastQueryAt = -10000;
    qint64 _queryNotBefore = 0;
    qint64 _lastConflictLogAt = -10000;
    quint32 _deviceBootMs = 0;
    quint64 _requestId = 0;
    bool _requestPending = false;
    bool _lastCanQueryA8 = false;
};
