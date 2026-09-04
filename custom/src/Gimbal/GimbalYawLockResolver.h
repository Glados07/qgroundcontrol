/****************************************************************************
 *
 * Per-gimbal yaw-lock frame compatibility resolver.
 *
 ****************************************************************************/

#pragma once

#include <cstdint>

#include "GimbalAzimuthPolicy.h"

class GimbalYawLockResolver {
   public:
    enum class Selection {
        Undecided,
        Standard,
        ReportedYaw,
        VehicleHeading,
    };

    struct State {
        Selection selection = Selection::Undecided;
        bool anchorValid = false;
        double anchorStandardYawDegrees = 0.0;
        double anchorReportedYawDegrees = 0.0;
        double anchorVehicleHeadingYawDegrees = 0.0;
        bool anchorVehicleHeadingValid = false;
        GimbalAzimuthPolicy::Source standardSource = GimbalAzimuthPolicy::Source::Invalid;
    };

    struct Input {
        // Compatibility detection is limited to an explicitly declared
        // vehicle-frame quaternion while YAW_LOCK is active.
        bool eligible = false;
        GimbalAzimuthPolicy::Result standardResult;
        GimbalAzimuthPolicy::Result reportedYawResult;
        // An independent yaw(q) + recent Vehicle.heading candidate. It is
        // considered only while the protocol candidate is using delta_yaw.
        GimbalAzimuthPolicy::Result vehicleHeadingResult;
    };

    struct Result {
        GimbalAzimuthPolicy::Result azimuth;
        Selection selection = Selection::Undecided;
        bool selectionChanged = false;
    };

    [[nodiscard]] static Result update(State& state, const Input& input);
    [[nodiscard]] static bool senderRestarted(std::uint32_t previousTimeBootMs, std::uint32_t currentTimeBootMs,
                                              std::uint32_t minimumRollbackMs = 1000);
    static void reset(State& state);
};
