/****************************************************************************
 *
 * Gimbal azimuth frame conversion policy.
 *
 ****************************************************************************/

#pragma once

#include <array>

class GimbalAzimuthPolicy {
   public:
    enum class Source {
        Invalid,
        ReportedEarthFrame,
        DeltaYaw,
        YawLockReportedYawCompatibility,
        YawLockVehicleHeadingCompatibility,
        VehicleHeadingFallback,
        LegacyEarthFrame,
        LegacyVehicleHeading,
    };

    enum class Error {
        None,
        ConflictingFrameFlags,
        InvalidQuaternion,
        MissingEarthReference,
    };

    struct Input {
        // MAVLink quaternion order is [w, x, y, z].
        std::array<double, 4> quaternion{1.0, 0.0, 0.0, 0.0};
        bool yawInVehicleFrame = false;
        bool yawInEarthFrame = false;
        bool yawLock = false;

        // Availability is deliberately separate from support. MAVLink 2
        // extension fields may decode to zero when they were not transmitted.
        bool deltaYawSupported = false;
        bool deltaYawAvailable = false;
        double deltaYawRadians = 0.0;

        // Used only when an earth reference cannot be obtained from the
        // reported frame or a usable delta_yaw.
        double vehicleHeadingDegrees = 0.0;
        bool vehicleHeadingAvailable = false;
    };

    struct Result {
        bool valid = false;
        double absoluteYawDegrees = 0.0;
        Source source = Source::Invalid;
        Error error = Error::None;
    };

    [[nodiscard]] static Result calculate(const Input& input);
    [[nodiscard]] static bool isValidQuaternion(const std::array<double, 4>& quaternion);

    // Normalizes finite angles to [-180, 180). Non-finite input is returned
    // unchanged so callers can retain its invalid state.
    [[nodiscard]] static double wrap180(double degrees);
};
