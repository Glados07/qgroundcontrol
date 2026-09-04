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
    enum class CompatibilityMode {
        None,
        ExplicitVehicleFrame,
        LegacyNoFrame,
    };

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
        bool anchorReportedYawValid = false;
        double anchorVehicleHeadingYawDegrees = 0.0;
        bool anchorVehicleHeadingValid = false;
        CompatibilityMode mode = CompatibilityMode::None;
        GimbalAzimuthPolicy::Source standardSource = GimbalAzimuthPolicy::Source::Invalid;
    };

    struct Input {
        CompatibilityMode mode = CompatibilityMode::None;
        GimbalAzimuthPolicy::Result standardResult;
        // Only ExplicitVehicleFrame uses direct yaw(q) as an independent
        // alternative. In LegacyNoFrame it is identical to standardResult.
        GimbalAzimuthPolicy::Result reportedYawResult;
        // An independent yaw(q) + recent Vehicle.heading candidate. It is
        // considered for a delta_yaw protocol result and for LegacyNoFrame.
        GimbalAzimuthPolicy::Result vehicleHeadingResult;

        // During the short Legacy Follow -> Lock boundary, the provider can
        // supply the last fresh world azimuth from the same gimbal route. It
        // is evidence for choosing between direct yaw(q) and heading+yaw(q),
        // never a value to freeze after a real yaw command.
        bool transitionReferenceAvailable = false;
        double transitionReferenceYawDegrees = 0.0;
        // A vendor heading+yaw(q) override additionally requires raw yaw(q)
        // itself to remain continuous across the same mode boundary. This
        // prevents an ordinary yaw command from being hidden merely because
        // one transformed candidate happens to resemble the previous output.
        bool transitionReportedYawReferenceAvailable = false;
        double transitionReportedYawReferenceDegrees = 0.0;
    };

    struct Result {
        GimbalAzimuthPolicy::Result azimuth;
        Selection selection = Selection::Undecided;
        bool selectionChanged = false;
    };

    [[nodiscard]] static Result update(State& state, const Input& input);
    [[nodiscard]] static CompatibilityMode modeForStatus(bool yawLock, bool yawInVehicleFrame, bool yawInEarthFrame);
    [[nodiscard]] static bool senderRestarted(std::uint32_t previousTimeBootMs, std::uint32_t currentTimeBootMs,
                                              std::uint32_t minimumRollbackMs = 1000);
    static void reset(State& state);
};
