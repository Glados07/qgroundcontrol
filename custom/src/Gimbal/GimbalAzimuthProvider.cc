/****************************************************************************
 *
 * Active MAVLink gimbal azimuth provider for custom UI surfaces.
 *
 ****************************************************************************/

#include "GimbalAzimuthProvider.h"

#include <QtCore/QLoggingCategory>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

#include "Fact.h"
#include "Gimbal.h"
#include "GimbalController.h"
#include "MultiVehicleManager.h"
#include "Vehicle.h"
#include "VehicleLinkManager.h"

Q_LOGGING_CATEGORY(GimbalAzimuthProviderLog, "qgc.custom.gimbal.azimuth")

namespace {

constexpr quint8 kDeltaYawPayloadLength =
    static_cast<quint8>(MAVLINK_MSG_ID_GIMBAL_DEVICE_ATTITUDE_STATUS_MIN_LEN + sizeof(float));
constexpr quint8 kDeltaYawVelocityPayloadLength =
    static_cast<quint8>(MAVLINK_MSG_ID_GIMBAL_DEVICE_ATTITUDE_STATUS_MIN_LEN + (2 * sizeof(float)));
constexpr qint64 kSampleTimeoutMs = 2000;
constexpr qint64 kDiagnosticIntervalMs = 200;
constexpr double kRadiansToDegrees = 180.0 / 3.14159265358979323846;
constexpr int kStaleSampleCheckIntervalMs = 250;

bool hasSemanticExtensionValue(float value) {
    return std::isnan(value) || (std::isfinite(value) && ((value != 0.0F) || std::signbit(value)));
}

QString errorName(GimbalAzimuthPolicy::Error error) {
    switch (error) {
        case GimbalAzimuthPolicy::Error::None:
            return QStringLiteral("None");
        case GimbalAzimuthPolicy::Error::ConflictingFrameFlags:
            return QStringLiteral("ConflictingFrameFlags");
        case GimbalAzimuthPolicy::Error::InvalidQuaternion:
            return QStringLiteral("InvalidQuaternion");
        case GimbalAzimuthPolicy::Error::MissingEarthReference:
            return QStringLiteral("MissingEarthReference");
    }

    return QStringLiteral("Unknown");
}

QString headingSourceName(GimbalHeadingTelemetry::Source source) {
    switch (source) {
        case GimbalHeadingTelemetry::Source::None:
            return QStringLiteral("None");
        case GimbalHeadingTelemetry::Source::Quaternion:
            return QStringLiteral("ATTITUDE_QUATERNION");
        case GimbalHeadingTelemetry::Source::Attitude:
            return QStringLiteral("ATTITUDE");
        case GimbalHeadingTelemetry::Source::HighLatency:
            return QStringLiteral("HIGH_LATENCY");
    }
    return QStringLiteral("Unknown");
}

bool senderRestarted(quint32 previous, quint32 current) {
    // A normal uint32 wrap is forward progress, not a new sender boot.
    const quint32 rollback = previous - current;
    return previous != 0U && current < previous && rollback > 1000U && rollback < 0x80000000U;
}

}  // namespace

GimbalAzimuthProvider::GimbalAzimuthProvider(Fact *legacyYawReference, QObject *parent) : QObject(parent) {
    if (legacyYawReference) {
        _setLegacyYawReference(legacyYawReference->rawValue().toInt());
        connect(legacyYawReference, &Fact::rawValueChanged, this,
                [this](const QVariant &value) { _setLegacyYawReference(value.toInt()); });
    }
    _monotonicClock.start();
    _staleSampleTimer.setInterval(kStaleSampleCheckIntervalMs);
    _staleSampleTimer.setTimerType(Qt::CoarseTimer);
    connect(&_staleSampleTimer, &QTimer::timeout, this, &GimbalAzimuthProvider::_publishActiveSample);
    _staleSampleTimer.start();

    MultiVehicleManager *const vehicleManager = MultiVehicleManager::instance();
    connect(vehicleManager, &MultiVehicleManager::activeVehicleChanged, this,
            &GimbalAzimuthProvider::_activeVehicleChanged);
    _activeVehicleChanged(vehicleManager->activeVehicle());
}

GimbalAzimuthProvider::~GimbalAzimuthProvider() {
    _clearActiveGimbalBindings();
    _clearActiveBindings();
}

void GimbalAzimuthProvider::handleMavlinkMessage(Vehicle *vehicle, const mavlink_message_t &message) {
    if (!vehicle || message.sysid != vehicle->id()) {
        return;
    }

    _trackVehicle(vehicle);
    const qint64 receivedAtMs = _monotonicClock.elapsed();
    if (message.compid == vehicle->compId() && _handleHeadingTelemetry(vehicle, message, receivedAtMs)) {
        // The heading value and its timestamp come from the same decoded
        // packet. Do not read the integer, display-adjusted Vehicle Fact here.
        _refreshVehicleSamples(vehicle, receivedAtMs);
        if (vehicle == _activeVehicle) {
            _publishActiveSample();
        }
    }

    if (message.msgid != MAVLINK_MSG_ID_GIMBAL_DEVICE_ATTITUDE_STATUS) {
        return;
    }

    mavlink_gimbal_device_attitude_status_t status{};
    mavlink_msg_gimbal_device_attitude_status_decode(&message, &status);
    if (status.gimbal_device_id > 6) {
        return;
    }

    VehicleSamples &vehicleSamples = _samples[vehicle];
    const quint16 key = _sampleKey(message.compid, status.gimbal_device_id);
    const auto previousIt = vehicleSamples.find(key);
    const bool previousFresh = previousIt != vehicleSamples.end() && receivedAtMs >= previousIt->receivedAtMs &&
                               receivedAtMs - previousIt->receivedAtMs <= kSampleTimeoutMs;
    const bool restarted =
        previousIt != vehicleSamples.end() && senderRestarted(previousIt->timeBootMs, status.time_boot_ms);
    if (previousIt != vehicleSamples.end() && previousIt->timeBootMs != 0U && status.time_boot_ms != 0U && !restarted) {
        const quint32 forward = status.time_boot_ms - previousIt->timeBootMs;
        if (forward == 0U || forward >= 0x80000000U) {
            // Replayed / slightly reordered attitudes must not renew q's
            // lifetime or combine an older q with the newest FC heading.
            // Some legacy senders use a constant zero: leave that unsupported
            // timestamp to the receive-time freshness check instead.
            return;
        }
    }
    CachedSample sample = previousFresh && !restarted ? previousIt.value() : CachedSample{};
    const bool forceLog = !previousFresh || restarted || sample.flags != status.flags;
    const bool yawInVehicleFrame = (status.flags & GIMBAL_DEVICE_FLAGS_YAW_IN_VEHICLE_FRAME) != 0;
    const bool yawInEarthFrame = (status.flags & GIMBAL_DEVICE_FLAGS_YAW_IN_EARTH_FRAME) != 0;
    const bool hasExplicitYawFrame = yawInVehicleFrame != yawInEarthFrame;

    // A non-zero device id can force default extension zeros onto the wire.
    // Length alone is not proof of delta support; keep semantic evidence
    // within this sender route and its current fresh session.
    const bool deltaSupportEvidence =
        hasExplicitYawFrame && message.len >= kDeltaYawPayloadLength &&
        (hasSemanticExtensionValue(status.delta_yaw) ||
         (message.len >= kDeltaYawVelocityPayloadLength && hasSemanticExtensionValue(status.delta_yaw_velocity)));
    sample.input.deltaYawSupported = sample.input.deltaYawSupported || deltaSupportEvidence;
    sample.input.deltaYawAvailable = sample.input.deltaYawSupported && std::isfinite(status.delta_yaw);
    sample.input.deltaYawRadians = status.delta_yaw;
    sample.input.quaternion = {status.q[0], status.q[1], status.q[2], status.q[3]};
    sample.input.yawInVehicleFrame = yawInVehicleFrame;
    sample.input.yawInEarthFrame = yawInEarthFrame;
    sample.input.yawLock = (status.flags & GIMBAL_DEVICE_FLAGS_YAW_LOCK) != 0;
    sample.sourceComponentId = message.compid;
    sample.deviceId = status.gimbal_device_id;
    sample.payloadLength = message.len;
    sample.flags = status.flags;
    sample.timeBootMs = status.time_boot_ms;
    sample.receivedAtMs = receivedAtMs;

    _recalculateSample(vehicle, sample, receivedAtMs, forceLog);
    vehicleSamples.insert(key, sample);
    if (vehicle == _activeVehicle) {
        _publishActiveSample();
    }
}

bool GimbalAzimuthProvider::_handleHeadingTelemetry(Vehicle *vehicle, const mavlink_message_t &message,
                                                    qint64 receivedAtMs) {
    using Source = GimbalHeadingTelemetry::Source;
    switch (message.msgid) {
        case MAVLINK_MSG_ID_ATTITUDE: {
            mavlink_attitude_t attitude{};
            mavlink_msg_attitude_decode(&message, &attitude);
            return _vehicleHeadingTelemetry[vehicle].update(Source::Attitude,
                                                            static_cast<double>(attitude.yaw) * kRadiansToDegrees,
                                                            receivedAtMs, attitude.time_boot_ms);
        }
        case MAVLINK_MSG_ID_ATTITUDE_QUATERNION: {
            mavlink_attitude_quaternion_t attitude{};
            mavlink_msg_attitude_quaternion_decode(&message, &attitude);
            GimbalAzimuthPolicy::Input input;
            input.quaternion = {attitude.q1, attitude.q2, attitude.q3, attitude.q4};
            input.yawInEarthFrame = true;
            // repr_offset_q is explicitly a user-display rotation. The
            // gimbal's vehicle-heading frame needs the physical heading.
            const auto result = GimbalAzimuthPolicy::calculate(input);
            return result.valid &&
                   _vehicleHeadingTelemetry[vehicle].update(Source::Quaternion, result.absoluteYawDegrees, receivedAtMs,
                                                            attitude.time_boot_ms);
        }
        case MAVLINK_MSG_ID_HIGH_LATENCY: {
            mavlink_high_latency_t status{};
            mavlink_msg_high_latency_decode(&message, &status);
            return status.heading <= 36000U &&
                   _vehicleHeadingTelemetry[vehicle].update(Source::HighLatency,
                                                            static_cast<double>(status.heading) / 100.0, receivedAtMs);
        }
        case MAVLINK_MSG_ID_HIGH_LATENCY2: {
            mavlink_high_latency2_t status{};
            mavlink_msg_high_latency2_decode(&message, &status);
            return status.heading <= 180U &&
                   _vehicleHeadingTelemetry[vehicle].update(Source::HighLatency,
                                                            static_cast<double>(status.heading) * 2.0, receivedAtMs);
        }
        default:
            return false;
    }
}

void GimbalAzimuthProvider::_setLegacyYawReference(int reference) {
    using Reference = GimbalAzimuthPolicy::LegacyYawReference;
    const Reference next = reference == static_cast<int>(Reference::VehicleHeading) ? Reference::VehicleHeading
                           : reference == static_cast<int>(Reference::EarthNorth)   ? Reference::EarthNorth
                                                                                    : Reference::Protocol;
    if (_legacyYawReference == next) {
        return;
    }
    _legacyYawReference = next;
    if (!_monotonicClock.isValid()) {
        return;
    }
    const qint64 nowMs = _monotonicClock.elapsed();
    for (auto vehicleIt = _samples.begin(); vehicleIt != _samples.end(); ++vehicleIt) {
        for (auto sampleIt = vehicleIt->begin(); sampleIt != vehicleIt->end(); ++sampleIt) {
            _recalculateSample(vehicleIt.key(), sampleIt.value(), nowMs, true);
        }
    }
    _publishActiveSample();
}

void GimbalAzimuthProvider::_refreshVehicleSamples(Vehicle *vehicle, qint64 nowMs) {
    const auto vehicleIt = _samples.find(vehicle);
    if (vehicleIt == _samples.end()) {
        return;
    }
    for (auto sampleIt = vehicleIt->begin(); sampleIt != vehicleIt->end(); ++sampleIt) {
        _recalculateSample(vehicle, sampleIt.value(), nowMs);
    }
}

void GimbalAzimuthProvider::_recalculateSample(Vehicle *vehicle, CachedSample &sample, qint64 nowMs, bool forceLog) {
    const auto headingIt = _vehicleHeadingTelemetry.constFind(vehicle);
    const GimbalHeadingTelemetry::Sample heading =
        headingIt != _vehicleHeadingTelemetry.cend() ? headingIt->heading(nowMs) : GimbalHeadingTelemetry::Sample{};
    sample.input.vehicleHeadingAvailable = heading.valid;
    sample.input.vehicleHeadingDegrees = heading.yawDegrees;
    sample.input.legacyYawReference = _legacyYawReference;
    const bool fresh = nowMs >= sample.receivedAtMs && nowMs - sample.receivedAtMs <= kSampleTimeoutMs;
    const auto next = fresh ? GimbalAzimuthPolicy::calculate(sample.input) : GimbalAzimuthPolicy::Result{};
    const bool changed = forceLog || sample.result.source != next.source || sample.result.valid != next.valid ||
                         sample.result.error != next.error;
    sample.result = next;
    if (changed) {
        qCInfo(GimbalAzimuthProviderLog) << "Gimbal azimuth reference changed"
                                         << "vehicle" << vehicle->id() << "source component" << sample.sourceComponentId
                                         << "device id" << sample.deviceId << "flags" << sample.flags
                                         << "legacy yaw reference" << static_cast<int>(_legacyYawReference)
                                         << "yaw lock" << sample.input.yawLock << "reference"
                                         << _sourceName(next.source) << "result valid" << next.valid << "error"
                                         << errorName(next.error) << "azimuth" << next.absoluteYawDegrees;
    }
    if (changed || sample.diagnosticAtMs < 0 || nowMs - sample.diagnosticAtMs >= kDiagnosticIntervalMs) {
        sample.diagnosticAtMs = nowMs;
        auto directInput = sample.input;
        directInput.yawInVehicleFrame = false;
        directInput.yawInEarthFrame = true;
        const auto direct = GimbalAzimuthPolicy::calculate(directInput);
        // Continuous diagnostics, independent of a reference change, make
        // heading/q arrival skew and the exact chosen formula observable.
        qCDebug(GimbalAzimuthProviderLog)
            << "Gimbal azimuth sample"
            << "vehicle" << vehicle->id() << "source component" << sample.sourceComponentId << "device id"
            << sample.deviceId << "payload length" << sample.payloadLength << "flags" << sample.flags << "yaw lock"
            << sample.input.yawLock << "legacy yaw reference" << static_cast<int>(_legacyYawReference)
            << "gimbal boot ms" << sample.timeBootMs << "gimbal received ms" << sample.receivedAtMs << "gimbal age ms"
            << nowMs - sample.receivedAtMs << "raw q" << sample.input.quaternion[0] << sample.input.quaternion[1]
            << sample.input.quaternion[2] << sample.input.quaternion[3] << "reported q yaw" << direct.absoluteYawDegrees
            << "heading valid" << heading.valid << "heading source" << headingSourceName(heading.source)
            << "raw heading" << heading.yawDegrees << "heading received ms" << heading.receivedAtMs << "heading age ms"
            << heading.ageMs << "display heading" << vehicle->heading()->rawValue() << "delta supported"
            << sample.input.deltaYawSupported << "delta yaw" << sample.input.deltaYawRadians << "reference"
            << _sourceName(next.source) << "result valid" << next.valid << "azimuth" << next.absoluteYawDegrees;
    }
}

void GimbalAzimuthProvider::_activeVehicleChanged(Vehicle *vehicle) {
    _clearActiveGimbalBindings();
    _clearActiveBindings();

    _activeVehicle = vehicle;
    _activeController = vehicle ? vehicle->gimbalController() : nullptr;

    if (_activeVehicle) {
        _activeBindings.append(
            connect(_activeVehicle, &QObject::destroyed, this, [this]() { _activeVehicleChanged(nullptr); }));
    }
    if (_activeController) {
        _activeBindings.append(connect(_activeController, &GimbalController::activeGimbalChanged, this,
                                       &GimbalAzimuthProvider::_activeGimbalChanged));
        _activeBindings.append(connect(_activeController, &QObject::destroyed, this, [this]() {
            _activeController.clear();
            _bindActiveGimbal(nullptr);
        }));
    }

    _bindActiveGimbal(_activeController ? _activeController->activeGimbal() : nullptr);
}

void GimbalAzimuthProvider::_activeGimbalChanged() {
    _bindActiveGimbal(_activeController ? _activeController->activeGimbal() : nullptr);
}

void GimbalAzimuthProvider::_bindActiveGimbal(Gimbal *gimbal) {
    _clearActiveGimbalBindings();
    _activeGimbal = gimbal;
    if (_activeGimbal) {
        _activeGimbalBindings.append(connect(_activeGimbal, &QObject::destroyed, this, [this]() {
            _activeGimbal.clear();
            _publishActiveSample();
        }));
    }
    _publishActiveSample();
}

void GimbalAzimuthProvider::_clearActiveBindings() {
    for (const QMetaObject::Connection &connection : std::as_const(_activeBindings)) {
        disconnect(connection);
    }
    _activeBindings.clear();
    _activeController.clear();
    _activeVehicle.clear();
}

void GimbalAzimuthProvider::_clearActiveGimbalBindings() {
    for (const QMetaObject::Connection &connection : std::as_const(_activeGimbalBindings)) {
        disconnect(connection);
    }
    _activeGimbalBindings.clear();
    _activeGimbal.clear();
}

void GimbalAzimuthProvider::_trackVehicle(Vehicle *vehicle) {
    if (_trackedVehicles.contains(vehicle)) {
        return;
    }

    _trackedVehicles.insert(vehicle);
    Vehicle *const trackedVehicle = vehicle;
    VehicleLinkManager *const linkManager = vehicle->vehicleLinkManager();
    if (linkManager) {
        connect(linkManager, &VehicleLinkManager::communicationLostChanged, this,
                [this, trackedVehicle](bool communicationLost) {
                    if (!communicationLost) {
                        return;
                    }

                    _samples.remove(trackedVehicle);
                    _vehicleHeadingTelemetry.remove(trackedVehicle);
                    if (trackedVehicle == _activeVehicle) {
                        _publishActiveSample();
                    }
                });
    }
    connect(vehicle, &QObject::destroyed, this, [this, trackedVehicle]() {
        _samples.remove(trackedVehicle);
        _trackedVehicles.remove(trackedVehicle);
        _vehicleHeadingTelemetry.remove(trackedVehicle);
    });
}

void GimbalAzimuthProvider::_publishActiveSample() {
    if (!_activeVehicle || !_activeGimbal) {
        _publishResult(nullptr);
        return;
    }

    VehicleLinkManager *const linkManager = _activeVehicle->vehicleLinkManager();
    if (!linkManager || linkManager->communicationLost()) {
        _publishResult(nullptr);
        return;
    }

    const auto vehicleIt = _samples.find(_activeVehicle.data());
    if (vehicleIt == _samples.end()) {
        _publishResult(nullptr);
        return;
    }

    const quint8 deviceId = static_cast<quint8>(_activeGimbal->deviceId()->rawValue().toUInt());
    const quint8 managerComponentId = static_cast<quint8>(_activeGimbal->managerCompid()->rawValue().toUInt());
    VehicleSamples &vehicleSamples = vehicleIt.value();
    CachedSample *selected = nullptr;

    // Manager/device-in-one components report ids 1..6 in the payload.
    // Prefer the exact active manager/device route. A standalone fallback is
    // only considered when that route has never produced a sample, otherwise
    // another MAVLink component could replace the active gimbal by recency.
    if ((deviceId >= 1) && (deviceId <= 6)) {
        const auto exactIt = vehicleSamples.find(_sampleKey(managerComponentId, deviceId));
        if (exactIt != vehicleSamples.end()) {
            selected = &exactIt.value();
        }
    }
    // Standalone gimbal devices report payload id 0 and use the MAVLink
    // source component itself as their device id.
    if (!selected) {
        const auto standaloneIt = vehicleSamples.find(_sampleKey(deviceId, 0));
        if (standaloneIt != vehicleSamples.end()) {
            selected = &standaloneIt.value();
        }
    }

    if (selected) {
        _recalculateSample(_activeVehicle.data(), *selected, _monotonicClock.elapsed());
    }
    _publishResult(selected ? &selected->result : nullptr);
}

void GimbalAzimuthProvider::_publishResult(const GimbalAzimuthPolicy::Result *result) {
    const bool newValid = result && result->valid;
    const double newAbsoluteYaw = newValid ? result->absoluteYawDegrees : std::numeric_limits<double>::quiet_NaN();
    const bool newUsingDeltaYaw = result && result->source == GimbalAzimuthPolicy::Source::DeltaYaw;
    const QString newReferenceSource = result ? _sourceName(result->source) : QStringLiteral("Invalid");

    const bool changed = (_valid != newValid) || (newValid && (!_valid || _absoluteYaw != newAbsoluteYaw)) ||
                         (_usingDeltaYaw != newUsingDeltaYaw) || (_referenceSource != newReferenceSource);

    _valid = newValid;
    _absoluteYaw = newAbsoluteYaw;
    _usingDeltaYaw = newUsingDeltaYaw;
    _referenceSource = newReferenceSource;

    if (changed) {
        emit attitudeChanged();
    }
}

quint16 GimbalAzimuthProvider::_sampleKey(quint8 sourceComponentId, quint8 reportedDeviceId) {
    return static_cast<quint16>((static_cast<quint16>(sourceComponentId) << 8) | reportedDeviceId);
}

QString GimbalAzimuthProvider::_sourceName(GimbalAzimuthPolicy::Source source) {
    switch (source) {
        case GimbalAzimuthPolicy::Source::Invalid:
            return QStringLiteral("Invalid");
        case GimbalAzimuthPolicy::Source::ReportedEarthFrame:
            return QStringLiteral("ReportedEarthFrame");
        case GimbalAzimuthPolicy::Source::DeltaYaw:
            return QStringLiteral("DeltaYaw");
        case GimbalAzimuthPolicy::Source::ConfiguredLegacyVehicleHeading:
            return QStringLiteral("ConfiguredLegacyVehicleHeading");
        case GimbalAzimuthPolicy::Source::ConfiguredLegacyEarthFrame:
            return QStringLiteral("ConfiguredLegacyEarthFrame");
        case GimbalAzimuthPolicy::Source::VehicleHeadingFallback:
            return QStringLiteral("VehicleHeadingFallback");
        case GimbalAzimuthPolicy::Source::LegacyEarthFrame:
            return QStringLiteral("LegacyEarthFrame");
        case GimbalAzimuthPolicy::Source::LegacyVehicleHeading:
            return QStringLiteral("LegacyVehicleHeading");
    }

    return QStringLiteral("Invalid");
}
