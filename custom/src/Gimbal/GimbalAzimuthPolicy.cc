/****************************************************************************
 *
 * Gimbal azimuth frame conversion policy.
 *
 ****************************************************************************/

#include "GimbalAzimuthPolicy.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr double kRadiansToDegrees = 180.0 / 3.141592653589793238462643383279502884;
constexpr double kDegreesToRadians = 1.0 / kRadiansToDegrees;
constexpr double kMinimumQuaternionComponent = 1e-12;

using Quaternion = std::array<double, 4>;

bool normalizeQuaternion(const Quaternion& input, Quaternion& output) {
    for (const double component : input) {
        if (!std::isfinite(component)) {
            return false;
        }
    }

    const double scale = std::max({std::abs(input[0]), std::abs(input[1]), std::abs(input[2]), std::abs(input[3])});
    if (scale < kMinimumQuaternionComponent) {
        return false;
    }

    const Quaternion scaled{
        input[0] / scale,
        input[1] / scale,
        input[2] / scale,
        input[3] / scale,
    };
    const double scaledNorm = std::sqrt((scaled[0] * scaled[0]) + (scaled[1] * scaled[1]) + (scaled[2] * scaled[2]) +
                                        (scaled[3] * scaled[3]));
    if (!std::isfinite(scaledNorm) || scaledNorm <= 0.0) {
        return false;
    }

    output = {
        scaled[0] / scaledNorm,
        scaled[1] / scaledNorm,
        scaled[2] / scaledNorm,
        scaled[3] / scaledNorm,
    };
    return true;
}

Quaternion multiply(const Quaternion& lhs, const Quaternion& rhs) {
    return {
        (lhs[0] * rhs[0]) - (lhs[1] * rhs[1]) - (lhs[2] * rhs[2]) - (lhs[3] * rhs[3]),
        (lhs[0] * rhs[1]) + (lhs[1] * rhs[0]) + (lhs[2] * rhs[3]) - (lhs[3] * rhs[2]),
        (lhs[0] * rhs[2]) - (lhs[1] * rhs[3]) + (lhs[2] * rhs[0]) + (lhs[3] * rhs[1]),
        (lhs[0] * rhs[3]) + (lhs[1] * rhs[2]) - (lhs[2] * rhs[1]) + (lhs[3] * rhs[0]),
    };
}

Quaternion yawQuaternion(double yawRadians) {
    const double normalizedYaw = std::remainder(yawRadians, 2.0 * 3.141592653589793238462643383279502884);
    const double halfYaw = normalizedYaw * 0.5;
    return {std::cos(halfYaw), 0.0, 0.0, std::sin(halfYaw)};
}

double quaternionYawDegrees(const Quaternion& quaternion) {
    const double numerator = 2.0 * ((quaternion[0] * quaternion[3]) + (quaternion[1] * quaternion[2]));
    const double denominator = 1.0 - (2.0 * ((quaternion[2] * quaternion[2]) + (quaternion[3] * quaternion[3])));
    return std::atan2(numerator, denominator) * kRadiansToDegrees;
}

Quaternion earthFromVehicle(const Quaternion& vehicleQuaternion, double earthOffsetRadians) {
    return multiply(yawQuaternion(earthOffsetRadians), vehicleQuaternion);
}

bool deltaYawUsable(const GimbalAzimuthPolicy::Input& input) {
    return input.deltaYawSupported && input.deltaYawAvailable && std::isfinite(input.deltaYawRadians);
}

bool headingUsable(const GimbalAzimuthPolicy::Input& input) {
    return input.vehicleHeadingAvailable && std::isfinite(input.vehicleHeadingDegrees);
}

}  // namespace

GimbalAzimuthPolicy::Result GimbalAzimuthPolicy::calculate(const Input& input) {
    Result result;

    // The MAVLink specification declares the simultaneous use of both frame
    // bits invalid. Do not guess from yaw-lock in this case.
    if (input.yawInVehicleFrame && input.yawInEarthFrame) {
        result.error = Error::ConflictingFrameFlags;
        return result;
    }

    Quaternion reportedQuaternion;
    if (!normalizeQuaternion(input.quaternion, reportedQuaternion)) {
        result.error = Error::InvalidQuaternion;
        return result;
    }

    const bool useDeltaYaw = deltaYawUsable(input);
    const bool useVehicleHeading = headingUsable(input);

    if (input.yawInEarthFrame) {
        result.valid = true;
        result.absoluteYawDegrees = wrap180(quaternionYawDegrees(reportedQuaternion));
        result.source = Source::ReportedEarthFrame;
        return result;
    }

    const bool vehicleFrame = input.yawInVehicleFrame || !input.yawLock;
    if (vehicleFrame) {
        // Explicit frame flags opt in to delta_yaw. With no explicit frame,
        // MAVLink requires the legacy yaw-lock interpretation and delta_yaw
        // must be ignored even if its extension field happens to be non-zero.
        if (input.yawInVehicleFrame && useDeltaYaw) {
            const Quaternion earthQuaternion = earthFromVehicle(reportedQuaternion, input.deltaYawRadians);
            result.absoluteYawDegrees = wrap180(quaternionYawDegrees(earthQuaternion));
            result.valid = true;
            result.source = Source::DeltaYaw;
            return result;
        }

        if (useVehicleHeading) {
            const Quaternion earthQuaternion =
                earthFromVehicle(reportedQuaternion, input.vehicleHeadingDegrees * kDegreesToRadians);
            result.absoluteYawDegrees = wrap180(quaternionYawDegrees(earthQuaternion));
            result.valid = true;
            result.source = input.yawInVehicleFrame ? Source::VehicleHeadingFallback : Source::LegacyVehicleHeading;
            return result;
        }

        result.error = Error::MissingEarthReference;
        return result;
    }

    // No explicit frame flags and yaw-lock set is the MAVLink legacy earth
    // frame. delta_yaw is intentionally ignored in this branch.
    result.valid = true;
    result.absoluteYawDegrees = wrap180(quaternionYawDegrees(reportedQuaternion));
    result.source = Source::LegacyEarthFrame;
    return result;
}

bool GimbalAzimuthPolicy::isValidQuaternion(const std::array<double, 4>& quaternion) {
    Quaternion normalized;
    return normalizeQuaternion(quaternion, normalized);
}

double GimbalAzimuthPolicy::wrap180(double degrees) {
    if (!std::isfinite(degrees)) {
        return degrees;
    }

    double wrapped = std::remainder(degrees, 360.0);
    if (wrapped >= 180.0) {
        wrapped -= 360.0;
    } else if (wrapped < -180.0) {
        wrapped += 360.0;
    }
    return wrapped;
}
