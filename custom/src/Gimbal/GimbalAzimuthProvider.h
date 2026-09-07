/****************************************************************************
 *
 * Active MAVLink gimbal azimuth provider for custom UI surfaces.
 *
 ****************************************************************************/

#pragma once

#include <QtCore/QElapsedTimer>
#include <QtCore/QHash>
#include <QtCore/QList>
#include <QtCore/QMetaObject>
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QSet>
#include <QtCore/QString>
#include <QtCore/QTimer>

#include "GimbalAzimuthPolicy.h"
#include "GimbalHeadingTelemetry.h"
#include "MAVLinkLib.h"

class Gimbal;
class GimbalController;
class Vehicle;

class GimbalAzimuthProvider final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool valid READ valid NOTIFY attitudeChanged)
    Q_PROPERTY(double absoluteYaw READ absoluteYaw NOTIFY attitudeChanged)
    Q_PROPERTY(bool usingDeltaYaw READ usingDeltaYaw NOTIFY attitudeChanged)
    Q_PROPERTY(QString referenceSource READ referenceSource NOTIFY attitudeChanged)

   public:
    explicit GimbalAzimuthProvider(QObject *parent = nullptr);
    ~GimbalAzimuthProvider() override;

    bool valid() const { return _valid; }
    double absoluteYaw() const { return _absoluteYaw; }
    bool usingDeltaYaw() const { return _usingDeltaYaw; }
    QString referenceSource() const { return _referenceSource; }

    void handleMavlinkMessage(Vehicle *vehicle, const mavlink_message_t &message);

   signals:
    void attitudeChanged();

   private:
    struct CachedSample {
        GimbalAzimuthPolicy::Input input;
        GimbalAzimuthPolicy::Result result;
        quint8 sourceComponentId = 0;
        quint8 deviceId = 0;
        quint8 payloadLength = 0;
        quint16 flags = 0;
        quint32 timeBootMs = 0;
        qint64 receivedAtMs = 0;
        qint64 diagnosticAtMs = -1;
    };

    using VehicleSamples = QHash<quint16, CachedSample>;

    void _activeVehicleChanged(Vehicle *vehicle);
    void _activeGimbalChanged();
    void _bindActiveGimbal(Gimbal *gimbal);
    void _clearActiveBindings();
    void _clearActiveGimbalBindings();
    void _trackVehicle(Vehicle *vehicle);
    bool _handleHeadingTelemetry(Vehicle *vehicle, const mavlink_message_t &message, qint64 receivedAtMs);
    void _refreshVehicleSamples(Vehicle *vehicle, qint64 nowMs);
    void _recalculateSample(Vehicle *vehicle, CachedSample &sample, qint64 nowMs, bool forceLog = false);
    void _publishActiveSample();
    void _publishResult(const GimbalAzimuthPolicy::Result *result);

    static quint16 _sampleKey(quint8 sourceComponentId, quint8 reportedDeviceId);
    static QString _sourceName(GimbalAzimuthPolicy::Source source);

    QPointer<Vehicle> _activeVehicle;
    QPointer<GimbalController> _activeController;
    QPointer<Gimbal> _activeGimbal;
    QList<QMetaObject::Connection> _activeBindings;
    QList<QMetaObject::Connection> _activeGimbalBindings;

    QHash<Vehicle *, VehicleSamples> _samples;
    QSet<Vehicle *> _trackedVehicles;
    QHash<Vehicle *, GimbalHeadingTelemetry> _vehicleHeadingTelemetry;
    QElapsedTimer _monotonicClock;
    QTimer _staleSampleTimer;

    bool _valid = false;
    double _absoluteYaw = 0.0;
    bool _usingDeltaYaw = false;
    QString _referenceSource = QStringLiteral("Invalid");
};
