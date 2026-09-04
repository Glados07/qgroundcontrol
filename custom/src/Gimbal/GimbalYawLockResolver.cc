/****************************************************************************
 *
 * Per-gimbal yaw-lock frame compatibility resolver.
 *
 ****************************************************************************/

#include "GimbalYawLockResolver.h"

#include <cmath>

namespace {

// When QGC starts while already locked, wait for enough base motion to compare
// the protocol transform, direct reported yaw and (when independent) q plus a
// recent vehicle heading. A compatibility candidate must be uniquely more
// stable, allowing realistic lock drift without training on attitude noise.
// Similar movement in every available candidate is treated as a camera command
// and starts a new observation window.
constexpr double kReferenceMotionDegrees = 8.0;
constexpr double kMotionAdvantageDegrees = 5.0;
constexpr double kReanchorMotionDegrees = 3.0;

double angularDistance(double lhs, double rhs) { return std::abs(GimbalAzimuthPolicy::wrap180(lhs - rhs)); }

bool validResult(const GimbalAzimuthPolicy::Result& result) {
    return result.valid && std::isfinite(result.absoluteYawDegrees);
}

bool vehicleHeadingAlternativeAvailable(const GimbalYawLockResolver::Input& input) {
    return (input.standardResult.source == GimbalAzimuthPolicy::Source::DeltaYaw) &&
           validResult(input.vehicleHeadingResult);
}

void setAnchor(GimbalYawLockResolver::State& state, const GimbalYawLockResolver::Input& input) {
    state.anchorValid = true;
    state.anchorStandardYawDegrees = input.standardResult.absoluteYawDegrees;
    state.anchorReportedYawDegrees = input.reportedYawResult.absoluteYawDegrees;
    state.anchorVehicleHeadingValid = vehicleHeadingAlternativeAvailable(input);
    if (state.anchorVehicleHeadingValid) {
        state.anchorVehicleHeadingYawDegrees = input.vehicleHeadingResult.absoluteYawDegrees;
    }
    state.standardSource = input.standardResult.source;
}

void clearLockObservation(GimbalYawLockResolver::State& state) {
    state.selection = GimbalYawLockResolver::Selection::Undecided;
    state.anchorValid = false;
    state.anchorVehicleHeadingValid = false;
    state.standardSource = GimbalAzimuthPolicy::Source::Invalid;
}

}  // namespace

GimbalYawLockResolver::Result GimbalYawLockResolver::update(State& state, const Input& input) {
    Result result;
    result.azimuth = input.standardResult;

    const Selection previousSelection = state.selection;
    if (!input.eligible) {
        clearLockObservation(state);
        result.selection = state.selection;
        result.selectionChanged = previousSelection != state.selection;
        return result;
    }

    const bool candidatesValid = validResult(input.standardResult) && validResult(input.reportedYawResult);
    if (!candidatesValid) {
        // A malformed or temporarily incomplete sample must not discard a
        // selection learned from prior valid motion. The provider independently
        // drops this state after a receive timeout, sender restart or link loss.
        result.selection = state.selection;
        result.selectionChanged = false;
        return result;
    }

    const bool vehicleHeadingAvailable = vehicleHeadingAlternativeAvailable(input);
    bool observationReset = false;
    if ((state.standardSource != GimbalAzimuthPolicy::Source::Invalid) &&
        (state.standardSource != input.standardResult.source)) {
        // A changed protocol source has no motion history in the old window.
        // Prefer it immediately, then let subsequent evidence disprove it if
        // this sender's frame relationship is inconsistent.
        state.selection = Selection::Standard;
        setAnchor(state, input);
        observationReset = true;
    } else if ((state.selection == Selection::VehicleHeading) && !vehicleHeadingAvailable) {
        // Never continue a compatibility result after its recent heading
        // reference disappears. Fall back to the protocol result immediately.
        state.selection = Selection::Standard;
        setAnchor(state, input);
        observationReset = true;
    } else if (state.anchorValid && (state.anchorVehicleHeadingValid != vehicleHeadingAvailable)) {
        // A newly available or expired heading candidate has no motion history
        // in the current window. Reanchor all candidates before comparing it.
        setAnchor(state, input);
        observationReset = true;
    }

    // A single lock-transition packet cannot distinguish a sender that changed
    // q's frame from a compliant body-frame sender receiving a simultaneous
    // yaw command. Start with the protocol result and require relative motion
    // evidence before overriding it. Keep validating after every selection so
    // later samples can correct a transient or changed sender behavior.
    if (!state.anchorValid || (state.standardSource != input.standardResult.source)) {
        setAnchor(state, input);
    } else if (!observationReset) {
        const double standardMotion =
            angularDistance(input.standardResult.absoluteYawDegrees, state.anchorStandardYawDegrees);
        const double reportedMotion =
            angularDistance(input.reportedYawResult.absoluteYawDegrees, state.anchorReportedYawDegrees);
        const double vehicleHeadingMotion =
            vehicleHeadingAvailable
                ? angularDistance(input.vehicleHeadingResult.absoluteYawDegrees, state.anchorVehicleHeadingYawDegrees)
                : 0.0;

        const auto compatibilityCandidateDominates = [&](Selection candidate) {
            const bool candidateAvailable =
                (candidate == Selection::ReportedYaw) ||
                ((candidate == Selection::VehicleHeading) && vehicleHeadingAvailable);
            if (!candidateAvailable) {
                return false;
            }

            const double candidateMotion =
                candidate == Selection::ReportedYaw ? reportedMotion : vehicleHeadingMotion;
            if ((standardMotion < kReferenceMotionDegrees) ||
                ((candidateMotion + kMotionAdvantageDegrees) > standardMotion)) {
                return false;
            }
            if ((candidate != Selection::ReportedYaw) &&
                ((reportedMotion < kReferenceMotionDegrees) ||
                 ((candidateMotion + kMotionAdvantageDegrees) > reportedMotion))) {
                return false;
            }
            if (vehicleHeadingAvailable && (candidate != Selection::VehicleHeading) &&
                ((vehicleHeadingMotion < kReferenceMotionDegrees) ||
                 ((candidateMotion + kMotionAdvantageDegrees) > vehicleHeadingMotion))) {
                return false;
            }
            return true;
        };

        const bool reportedYawDominates = compatibilityCandidateDominates(Selection::ReportedYaw);
        const bool vehicleHeadingDominates = compatibilityCandidateDominates(Selection::VehicleHeading);

        Selection nextSelection = state.selection;
        if (reportedYawDominates) {
            nextSelection = Selection::ReportedYaw;
        } else if (vehicleHeadingDominates) {
            nextSelection = Selection::VehicleHeading;
        } else if ((state.selection == Selection::ReportedYaw) ||
                   (state.selection == Selection::VehicleHeading)) {
            const double selectedMotion =
                state.selection == Selection::ReportedYaw ? reportedMotion : vehicleHeadingMotion;
            if ((selectedMotion >= kReferenceMotionDegrees) &&
                ((standardMotion + kMotionAdvantageDegrees) <= selectedMotion)) {
                // Standard is the protocol-safe tie breaker whenever the
                // current compatibility choice is disproved but no other
                // compatibility candidate is uniquely better.
                nextSelection = Selection::Standard;
            }
        } else if (state.selection == Selection::Undecided) {
            const bool standardBeatsReported =
                (reportedMotion >= kReferenceMotionDegrees) &&
                ((standardMotion + kMotionAdvantageDegrees) <= reportedMotion);
            const bool standardBeatsVehicleHeading =
                vehicleHeadingAvailable && (vehicleHeadingMotion >= kReferenceMotionDegrees) &&
                ((standardMotion + kMotionAdvantageDegrees) <= vehicleHeadingMotion);
            if (standardBeatsReported || standardBeatsVehicleHeading) {
                nextSelection = Selection::Standard;
            }
        }

        if ((nextSelection != state.selection) || reportedYawDominates || vehicleHeadingDominates) {
            state.selection = nextSelection;
            // A compatibility result that wins another full motion window is
            // fresh evidence, even when the selected source does not change.
            // Move every anchor forward so later frame-behaviour changes are
            // compared with recent motion instead of the original lock sample.
            setAnchor(state, input);
        } else {
            const bool allCandidatesMoved =
                (standardMotion >= kReanchorMotionDegrees) && (reportedMotion >= kReanchorMotionDegrees) &&
                (!vehicleHeadingAvailable || (vehicleHeadingMotion >= kReanchorMotionDegrees));
            if (allCandidatesMoved) {
                setAnchor(state, input);
            }
        }
    }

    if (state.selection == Selection::ReportedYaw) {
        result.azimuth = input.reportedYawResult;
        result.azimuth.source = GimbalAzimuthPolicy::Source::YawLockReportedYawCompatibility;
    } else if (state.selection == Selection::VehicleHeading) {
        result.azimuth = input.vehicleHeadingResult;
        result.azimuth.source = GimbalAzimuthPolicy::Source::YawLockVehicleHeadingCompatibility;
    }

    result.selection = state.selection;
    result.selectionChanged = previousSelection != state.selection;
    return result;
}

bool GimbalYawLockResolver::senderRestarted(std::uint32_t previousTimeBootMs, std::uint32_t currentTimeBootMs,
                                            std::uint32_t minimumRollbackMs) {
    return (previousTimeBootMs != 0U) && (currentTimeBootMs < previousTimeBootMs) &&
           ((previousTimeBootMs - currentTimeBootMs) > minimumRollbackMs);
}

void GimbalYawLockResolver::reset(State& state) { state = State{}; }
