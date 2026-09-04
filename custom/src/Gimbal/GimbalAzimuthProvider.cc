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
constexpr qint64 kTransitionReferenceMaxAgeMs = 1000;
constexpr qint64 kLegacyLockTransitionGuardMs = 1000;
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

QString lockSelectionName(GimbalYawLockResolver::Selection selection) {
    switch (selection) {
        case GimbalYawLockResolver::Selection::Undecided:
            return QStringLiteral("Undecided");
        case GimbalYawLockResolver::Selection::Standard:
            return QStringLiteral("Standard");
        case GimbalYawLockResolver::Selection::ReportedYaw:
            return QStringLiteral("ReportedYaw");
        case GimbalYawLockResolver::Selection::VehicleHeading:
            return QStringLiteral("VehicleHeading");
    }

    return QStringLiteral("Unknown");
}

QString lockCompatibilityModeName(GimbalYawLockResolver::CompatibilityMode mode) {
    switch (mode) {
        case GimbalYawLockResolver::CompatibilityMode::None:
            return QStringLiteral("None");
        case GimbalYawLockResolver::CompatibilityMode::ExplicitVehicleFrame:
            return QStringLiteral("ExplicitVehicleFrame");
        case GimbalYawLockResolver::CompatibilityMode::LegacyNoFrame:
            return QStringLiteral("LegacyNoFrame");
    }

    return QStringLiteral("Unknown");
}

}  // namespace

GimbalAzimuthProvider::GimbalAzimuthProvider(QObject *parent) : QObject(parent) {
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
    if (!vehicle) {
        return;
    }

    _trackVehicle(vehicle);
    const qint64 receivedAtMs = _monotonicClock.elapsed();

    // The plugin observes messages before Vehicle updates its Facts. Remember
    // that at least one real heading-bearing packet has arrived so the Fact's
    // default value (0) is never mistaken for measured North.
    const bool fromFlightController = (message.sysid == vehicle->id()) && (message.compid == vehicle->compId());
    if (fromFlightController &&
        ((message.msgid == MAVLINK_MSG_ID_ATTITUDE) || (message.msgid == MAVLINK_MSG_ID_ATTITUDE_QUATERNION) ||
         (message.msgid == MAVLINK_MSG_ID_HIGH_LATENCY) || (message.msgid == MAVLINK_MSG_ID_HIGH_LATENCY2))) {
        _vehicleHeadingTelemetryAtMs.insert(vehicle, receivedAtMs);
    }

    if (message.msgid != MAVLINK_MSG_ID_GIMBAL_DEVICE_ATTITUDE_STATUS) {
        return;
    }

    mavlink_gimbal_device_attitude_status_t status{};
    mavlink_msg_gimbal_device_attitude_status_decode(&message, &status);
    if (status.gimbal_device_id > 6) {
        qCDebug(GimbalAzimuthProviderLog) << "Ignoring attitude for invalid gimbal device id" << status.gimbal_device_id
                                          << "from component" << message.compid;
        return;
    }

    const bool yawInVehicleFrame = (status.flags & GIMBAL_DEVICE_FLAGS_YAW_IN_VEHICLE_FRAME) != 0;
    const bool yawInEarthFrame = (status.flags & GIMBAL_DEVICE_FLAGS_YAW_IN_EARTH_FRAME) != 0;
    const bool hasExplicitYawFrame = yawInVehicleFrame != yawInEarthFrame;

    VehicleSamples &vehicleSamples = _samples[vehicle];
    const quint16 key = _sampleKey(message.compid, status.gimbal_device_id);
    const auto previousIt = vehicleSamples.constFind(key);
    const bool previousFresh = previousIt != vehicleSamples.cend() && previousIt->receivedAtMs >= 0 &&
                               (receivedAtMs - previousIt->receivedAtMs) <= kSampleTimeoutMs;
    // A current value of zero is valid on the first packet after a restart.
    // Only the previous zero is unsuitable as a rollback baseline.
    const bool senderRestarted = previousIt != vehicleSamples.cend() &&
                                 GimbalYawLockResolver::senderRestarted(previousIt->timeBootMs, status.time_boot_ms);
    const bool previouslySupported =
        !senderRestarted && previousIt != vehicleSamples.cend() && previousIt->deltaYawSupported;
    // A later non-zero extension (notably gimbal_device_id) can force an
    // untouched delta_yaw=0 onto the wire, so payload length alone cannot
    // distinguish a legitimate first zero from an unsupported default. A
    // semantic non-default delta field proves support; support then remains
    // sticky because MAVLink 2 may trim later legitimate zero values.
    const bool deltaYawFieldPresent = message.len >= kDeltaYawPayloadLength;
    const bool deltaYawVelocityFieldPresent = message.len >= kDeltaYawVelocityPayloadLength;
    const bool deltaYawSupportEvidence =
        hasExplicitYawFrame && deltaYawFieldPresent &&
        (hasSemanticExtensionValue(status.delta_yaw) ||
         (deltaYawVelocityFieldPresent && hasSemanticExtensionValue(status.delta_yaw_velocity)));
    const bool deltaYawSupported = previouslySupported || deltaYawSupportEvidence;

    bool headingOk = false;
    const double vehicleHeading = vehicle->heading()->rawValue().toDouble(&headingOk);
    const auto headingTelemetryIt = _vehicleHeadingTelemetryAtMs.constFind(vehicle);
    const bool measuredHeadingAvailable =
        headingTelemetryIt != _vehicleHeadingTelemetryAtMs.cend() && headingOk && std::isfinite(vehicleHeading);
    const bool recentHeadingTelemetry = measuredHeadingAvailable && receivedAtMs >= headingTelemetryIt.value() &&
                                        (receivedAtMs - headingTelemetryIt.value()) <= kSampleTimeoutMs;

    GimbalAzimuthPolicy::Input input;
    input.quaternion = {
        static_cast<double>(status.q[0]),
        static_cast<double>(status.q[1]),
        static_cast<double>(status.q[2]),
        static_cast<double>(status.q[3]),
    };
    input.yawInVehicleFrame = yawInVehicleFrame;
    input.yawInEarthFrame = yawInEarthFrame;
    input.yawLock = (status.flags & GIMBAL_DEVICE_FLAGS_YAW_LOCK) != 0;
    input.deltaYawSupported = deltaYawSupported;
    input.deltaYawAvailable = deltaYawSupported && std::isfinite(status.delta_yaw);
    input.deltaYawRadians = static_cast<double>(status.delta_yaw);
    input.vehicleHeadingAvailable = recentHeadingTelemetry;
    input.vehicleHeadingDegrees = vehicleHeading;

    const GimbalYawLockResolver::CompatibilityMode lockCompatibilityMode =
        GimbalYawLockResolver::modeForStatus(input.yawLock, input.yawInVehicleFrame, input.yawInEarthFrame);

    CachedSample sample;
    if (!senderRestarted && previousFresh) {
        sample.yawLockState = previousIt->yawLockState;
        if (lockCompatibilityMode == GimbalYawLockResolver::CompatibilityMode::LegacyNoFrame) {
            if ((previousIt->lockCompatibilityMode == GimbalYawLockResolver::CompatibilityMode::LegacyNoFrame) &&
                previousIt->transitionReferenceValid) {
                sample.transitionReferenceValid = true;
                sample.transitionReferenceYawDegrees = previousIt->transitionReferenceYawDegrees;
                sample.transitionReportedYawReferenceValid = previousIt->transitionReportedYawReferenceValid;
                sample.transitionReportedYawReferenceDegrees = previousIt->transitionReportedYawReferenceDegrees;
                sample.transitionReferenceAtMs = previousIt->transitionReferenceAtMs;
                sample.transitionStartedAtMs = previousIt->transitionStartedAtMs;
            } else if (!previousIt->yawLock && previousIt->result.valid &&
                       std::isfinite(previousIt->result.absoluteYawDegrees) &&
                       (receivedAtMs >= previousIt->receivedAtMs) &&
                       ((receivedAtMs - previousIt->receivedAtMs) <= kTransitionReferenceMaxAgeMs)) {
                // This is the same Vehicle/component/device route and the
                // preceding Follow sample is close to the mode edge, so it is
                // a bounded world-yaw continuity reference for the ambiguous
                // legacy lock. Raw q continuity is checked independently.
                sample.transitionReferenceValid = true;
                sample.transitionReferenceYawDegrees = previousIt->result.absoluteYawDegrees;
                sample.transitionReportedYawReferenceValid = previousIt->reportedYawValid;
                sample.transitionReportedYawReferenceDegrees = previousIt->reportedYawDegrees;
                sample.transitionReferenceAtMs = previousIt->receivedAtMs;
                sample.transitionStartedAtMs = receivedAtMs;
            }
        }
    }

    const GimbalAzimuthPolicy::Result standardResult = GimbalAzimuthPolicy::calculate(input);
    GimbalAzimuthPolicy::Input reportedYawInput = input;
    reportedYawInput.yawInVehicleFrame = false;
    reportedYawInput.yawInEarthFrame = true;
    const GimbalAzimuthPolicy::Result reportedYawResult = GimbalAzimuthPolicy::calculate(reportedYawInput);
    GimbalAzimuthPolicy::Result vehicleHeadingResult;

    const bool needsVehicleHeadingCandidate =
        (lockCompatibilityMode == GimbalYawLockResolver::CompatibilityMode::LegacyNoFrame) ||
        ((lockCompatibilityMode == GimbalYawLockResolver::CompatibilityMode::ExplicitVehicleFrame) &&
         (standardResult.source == GimbalAzimuthPolicy::Source::DeltaYaw));
    if (recentHeadingTelemetry && needsVehicleHeadingCandidate) {
        GimbalAzimuthPolicy::Input vehicleHeadingInput = input;
        // LegacyNoFrame + YAW_LOCK would normally make Policy interpret q as
        // earth-frame yaw. Force the independent body-frame hypothesis here
        // so this candidate is genuinely yaw(q) + Vehicle.heading.
        vehicleHeadingInput.yawInVehicleFrame = true;
        vehicleHeadingInput.yawInEarthFrame = false;
        vehicleHeadingInput.deltaYawSupported = false;
        vehicleHeadingInput.deltaYawAvailable = false;
        vehicleHeadingResult = GimbalAzimuthPolicy::calculate(vehicleHeadingInput);
    }

    GimbalYawLockResolver::Input resolverInput;
    resolverInput.mode = lockCompatibilityMode;
    resolverInput.standardResult = standardResult;
    resolverInput.reportedYawResult = reportedYawResult;
    resolverInput.vehicleHeadingResult = vehicleHeadingResult;
    resolverInput.transitionReferenceAvailable =
        (lockCompatibilityMode == GimbalYawLockResolver::CompatibilityMode::LegacyNoFrame) &&
        sample.transitionReferenceValid && (sample.transitionReferenceAtMs >= 0) &&
        (sample.transitionStartedAtMs >= sample.transitionReferenceAtMs) &&
        ((sample.transitionStartedAtMs - sample.transitionReferenceAtMs) <= kTransitionReferenceMaxAgeMs) &&
        (receivedAtMs >= sample.transitionStartedAtMs) &&
        ((receivedAtMs - sample.transitionStartedAtMs) <= kLegacyLockTransitionGuardMs);
    resolverInput.transitionReferenceYawDegrees = sample.transitionReferenceYawDegrees;
    resolverInput.transitionReportedYawReferenceAvailable =
        resolverInput.transitionReferenceAvailable && sample.transitionReportedYawReferenceValid;
    resolverInput.transitionReportedYawReferenceDegrees = sample.transitionReportedYawReferenceDegrees;
    const GimbalYawLockResolver::Result resolved = GimbalYawLockResolver::update(sample.yawLockState, resolverInput);
    sample.result = resolved.azimuth;
    sample.lockCompatibilityMode = lockCompatibilityMode;
    sample.yawLock = input.yawLock;
    sample.deltaYawSupported = deltaYawSupported;
    sample.reportedYawValid = reportedYawResult.valid && std::isfinite(reportedYawResult.absoluteYawDegrees);
    sample.reportedYawDegrees = reportedYawResult.absoluteYawDegrees;
    sample.timeBootMs = status.time_boot_ms;
    sample.receivedAtMs = receivedAtMs;

    const bool sourceChanged =
        senderRestarted || previousIt == vehicleSamples.cend() || previousIt->result.source != sample.result.source;
    const bool lockSessionChanged = previousIt == vehicleSamples.cend() || senderRestarted || !previousFresh ||
                                    previousIt->lockCompatibilityMode != lockCompatibilityMode ||
                                    previousIt->yawLock != input.yawLock;
    if (sourceChanged || resolved.selectionChanged || lockSessionChanged) {
        qCInfo(GimbalAzimuthProviderLog) << "Gimbal azimuth reference changed"
                                         << "vehicle" << vehicle->id() << "source component" << message.compid
                                         << "device id" << status.gimbal_device_id << "payload length" << message.len
                                         << "flags" << status.flags << "yaw lock" << input.yawLock << "vehicle frame"
                                         << input.yawInVehicleFrame << "earth frame" << input.yawInEarthFrame
                                         << "compatibility mode" << lockCompatibilityModeName(lockCompatibilityMode)
                                         << "delta supported" << input.deltaYawSupported << "delta yaw"
                                         << input.deltaYawRadians << "heading available" << measuredHeadingAvailable
                                         << "recent heading" << recentHeadingTelemetry << "vehicle heading"
                                         << input.vehicleHeadingDegrees << "protocol reference"
                                         << _sourceName(standardResult.source) << "protocol azimuth"
                                         << standardResult.absoluteYawDegrees << "reported q yaw valid"
                                         << reportedYawResult.valid << "reported q yaw"
                                         << reportedYawResult.absoluteYawDegrees << "heading candidate valid"
                                         << vehicleHeadingResult.valid << "heading candidate yaw"
                                         << vehicleHeadingResult.absoluteYawDegrees << "transition reference valid"
                                         << resolverInput.transitionReferenceAvailable << "transition reference yaw"
                                         << resolverInput.transitionReferenceYawDegrees << "transition raw q valid"
                                         << resolverInput.transitionReportedYawReferenceAvailable << "transition raw q"
                                         << resolverInput.transitionReportedYawReferenceDegrees << "lock resolver"
                                         << lockSelectionName(resolved.selection) << "result valid"
                                         << sample.result.valid << "reference" << _sourceName(sample.result.source)
                                         << "azimuth" << sample.result.absoluteYawDegrees;
    }

    const bool errorChanged = sample.result.error != GimbalAzimuthPolicy::Error::None &&
                              (previousIt == vehicleSamples.cend() || previousIt->result.error != sample.result.error);
    if (errorChanged) {
        qCWarning(GimbalAzimuthProviderLog)
            << "Rejected/incomplete gimbal azimuth sample"
            << "vehicle" << vehicle->id() << "source component" << message.compid << "device id"
            << status.gimbal_device_id << "flags" << status.flags << "error" << errorName(sample.result.error);
    }

    vehicleSamples.insert(key, sample);
    if (vehicle == _activeVehicle) {
        _publishActiveSample();
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
                    _vehicleHeadingTelemetryAtMs.remove(trackedVehicle);
                    if (trackedVehicle == _activeVehicle) {
                        _publishActiveSample();
                    }
                });
    }
    connect(vehicle, &QObject::destroyed, this, [this, trackedVehicle]() {
        _samples.remove(trackedVehicle);
        _trackedVehicles.remove(trackedVehicle);
        _vehicleHeadingTelemetryAtMs.remove(trackedVehicle);
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

    const auto vehicleIt = _samples.constFind(_activeVehicle.data());
    if (vehicleIt == _samples.cend()) {
        _publishResult(nullptr);
        return;
    }

    const quint8 deviceId = static_cast<quint8>(_activeGimbal->deviceId()->rawValue().toUInt());
    const quint8 managerComponentId = static_cast<quint8>(_activeGimbal->managerCompid()->rawValue().toUInt());
    const VehicleSamples &vehicleSamples = vehicleIt.value();
    const CachedSample *selected = nullptr;

    // Manager/device-in-one components report ids 1..6 in the payload.
    // Prefer the exact active manager/device route. A standalone fallback is
    // only considered when that route has never produced a sample, otherwise
    // another MAVLink component could replace the active gimbal by recency.
    if ((deviceId >= 1) && (deviceId <= 6)) {
        const auto exactIt = vehicleSamples.constFind(_sampleKey(managerComponentId, deviceId));
        if (exactIt != vehicleSamples.cend()) {
            selected = &exactIt.value();
        }
    }
    // Standalone gimbal devices report payload id 0 and use the MAVLink
    // source component itself as their device id.
    if (!selected) {
        const auto standaloneIt = vehicleSamples.constFind(_sampleKey(deviceId, 0));
        if (standaloneIt != vehicleSamples.cend()) {
            selected = &standaloneIt.value();
        }
    }

    const bool fresh = selected && (selected->receivedAtMs >= 0) &&
                       ((_monotonicClock.elapsed() - selected->receivedAtMs) <= kSampleTimeoutMs);
    _publishResult(fresh ? &selected->result : nullptr);
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
        case GimbalAzimuthPolicy::Source::YawLockReportedYawCompatibility:
            return QStringLiteral("YawLockReportedYawCompatibility");
        case GimbalAzimuthPolicy::Source::YawLockVehicleHeadingCompatibility:
            return QStringLiteral("YawLockVehicleHeadingCompatibility");
        case GimbalAzimuthPolicy::Source::VehicleHeadingFallback:
            return QStringLiteral("VehicleHeadingFallback");
        case GimbalAzimuthPolicy::Source::LegacyEarthFrame:
            return QStringLiteral("LegacyEarthFrame");
        case GimbalAzimuthPolicy::Source::LegacyVehicleHeading:
            return QStringLiteral("LegacyVehicleHeading");
    }

    return QStringLiteral("Invalid");
}
