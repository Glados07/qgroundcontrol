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
constexpr double kTransitionEntryCloseDegrees = 8.0;
constexpr double kTransitionGuardCloseDegrees = 3.0;
constexpr double kTransitionReportedYawContinuityDegrees = 3.0;

double angularDistance(double lhs, double rhs) { return std::abs(GimbalAzimuthPolicy::wrap180(lhs - rhs)); }

bool validResult(const GimbalAzimuthPolicy::Result& result) {
    return result.valid && std::isfinite(result.absoluteYawDegrees);
}

bool reportedYawAlternativeAvailable(const GimbalYawLockResolver::Input& input) {
    return (input.mode == GimbalYawLockResolver::CompatibilityMode::ExplicitVehicleFrame) &&
           validResult(input.reportedYawResult);
}

bool vehicleHeadingAlternativeAvailable(const GimbalYawLockResolver::Input& input) {
    const bool independentFromStandard = (input.mode == GimbalYawLockResolver::CompatibilityMode::LegacyNoFrame) ||
                                         (input.standardResult.source == GimbalAzimuthPolicy::Source::DeltaYaw);
    return independentFromStandard && validResult(input.vehicleHeadingResult);
}

GimbalYawLockResolver::Selection transitionReferencePreference(const GimbalYawLockResolver::Input& input,
                                                               bool vehicleHeadingAvailable,
                                                               double maximumCloseDistance) {
    if ((input.mode != GimbalYawLockResolver::CompatibilityMode::LegacyNoFrame) ||
        !input.transitionReferenceAvailable || !std::isfinite(input.transitionReferenceYawDegrees) ||
        !vehicleHeadingAvailable) {
        return GimbalYawLockResolver::Selection::Undecided;
    }

    const double standardDistance =
        angularDistance(input.standardResult.absoluteYawDegrees, input.transitionReferenceYawDegrees);
    const double vehicleHeadingDistance =
        angularDistance(input.vehicleHeadingResult.absoluteYawDegrees, input.transitionReferenceYawDegrees);
    const bool reportedYawStayedContinuous =
        input.transitionReportedYawReferenceAvailable && validResult(input.reportedYawResult) &&
        std::isfinite(input.transitionReportedYawReferenceDegrees) &&
        (angularDistance(input.reportedYawResult.absoluteYawDegrees, input.transitionReportedYawReferenceDegrees) <=
         kTransitionReportedYawContinuityDegrees);
    if ((standardDistance <= maximumCloseDistance) &&
        ((standardDistance + kMotionAdvantageDegrees) <= vehicleHeadingDistance)) {
        return GimbalYawLockResolver::Selection::Standard;
    }
    if (reportedYawStayedContinuous && (vehicleHeadingDistance <= maximumCloseDistance) &&
        ((vehicleHeadingDistance + kMotionAdvantageDegrees) <= standardDistance)) {
        return GimbalYawLockResolver::Selection::VehicleHeading;
    }

    return GimbalYawLockResolver::Selection::Undecided;
}

void setAnchor(GimbalYawLockResolver::State& state, const GimbalYawLockResolver::Input& input) {
    state.anchorValid = true;
    state.anchorStandardYawDegrees = input.standardResult.absoluteYawDegrees;
    state.anchorReportedYawValid = reportedYawAlternativeAvailable(input);
    if (state.anchorReportedYawValid) {
        state.anchorReportedYawDegrees = input.reportedYawResult.absoluteYawDegrees;
    }
    state.anchorVehicleHeadingValid = vehicleHeadingAlternativeAvailable(input);
    if (state.anchorVehicleHeadingValid) {
        state.anchorVehicleHeadingYawDegrees = input.vehicleHeadingResult.absoluteYawDegrees;
    }
    state.mode = input.mode;
    state.standardSource = input.standardResult.source;
}

void clearLockObservation(GimbalYawLockResolver::State& state) { state = GimbalYawLockResolver::State{}; }

}  // namespace

GimbalYawLockResolver::Result GimbalYawLockResolver::update(State& state, const Input& input) {
    Result result;
    result.azimuth = input.standardResult;

    const Selection previousSelection = state.selection;
    if (input.mode == CompatibilityMode::None) {
        clearLockObservation(state);
        result.selection = state.selection;
        result.selectionChanged = previousSelection != state.selection;
        return result;
    }

    const bool standardValid = validResult(input.standardResult);
    const bool reportedYawAvailable = reportedYawAlternativeAvailable(input);
    const bool vehicleHeadingAvailable = vehicleHeadingAlternativeAvailable(input);
    if (!standardValid || ((input.mode == CompatibilityMode::ExplicitVehicleFrame) && !reportedYawAvailable)) {
        // A malformed or temporarily incomplete sample must not discard a
        // selection learned from prior valid motion. The provider independently
        // drops this state after a receive timeout, sender restart or link loss.
        result.selection = state.selection;
        result.selectionChanged = false;
        return result;
    }

    bool observationReset = false;
    if ((state.mode != CompatibilityMode::None) && (state.mode != input.mode)) {
        // Never transfer a learned interpretation between the explicit-frame
        // and ambiguous legacy protocols.
        state.selection = Selection::Standard;
        setAnchor(state, input);
        observationReset = true;
    } else if ((state.standardSource != GimbalAzimuthPolicy::Source::Invalid) &&
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
    } else if (state.anchorValid && ((state.anchorReportedYawValid != reportedYawAvailable) ||
                                     (state.anchorVehicleHeadingValid != vehicleHeadingAvailable))) {
        // A newly available or expired alternative has no motion history in
        // the current window. Reanchor all candidates before comparing it.
        setAnchor(state, input);
        observationReset = true;
    }

    const bool startingObservation = !state.anchorValid;
    bool transitionAdjusted = false;
    if (startingObservation) {
        // Explicit-frame senders retain the protocol result until motion gives
        // evidence otherwise. A no-frame legacy sender is ambiguous by design,
        // so a fresh Follow result can resolve its first Lock packet without a
        // visible heading-sized jump.
        const Selection preferred =
            transitionReferencePreference(input, vehicleHeadingAvailable, kTransitionEntryCloseDegrees);
        if (preferred != Selection::Undecided) {
            state.selection = preferred;
            transitionAdjusted = true;
        }
        setAnchor(state, input);
    } else if ((input.mode == CompatibilityMode::LegacyNoFrame) && input.transitionReferenceAvailable &&
               vehicleHeadingAvailable) {
        // Some bridges change the flag and q's actual frame in adjacent
        // packets. While the provider's short transition guard is active,
        // replace the current interpretation only when it has made a full
        // discontinuity and the other candidate remains very close to the
        // pre-lock world azimuth.
        const Selection preferred =
            transitionReferencePreference(input, vehicleHeadingAvailable, kTransitionGuardCloseDegrees);
        const Selection effectiveSelection =
            state.selection == Selection::Undecided ? Selection::Standard : state.selection;
        if ((effectiveSelection == Selection::VehicleHeading) && (preferred == Selection::Standard)) {
            const double selectedDistance =
                angularDistance(input.vehicleHeadingResult.absoluteYawDegrees, input.transitionReferenceYawDegrees);
            if (selectedDistance >= kReferenceMotionDegrees) {
                state.selection = preferred;
                setAnchor(state, input);
                transitionAdjusted = true;
            }
        } else if ((state.selection == Selection::Undecided) && (preferred == Selection::Standard)) {
            state.selection = Selection::Standard;
            setAnchor(state, input);
            transitionAdjusted = true;
        }
    }

    // Without transition evidence (for example when QGC starts already
    // locked), compare candidate motion. Keep validating every learned choice
    // so later sender behaviour can replace it.
    if (!startingObservation && !observationReset && !transitionAdjusted) {
        const double standardMotion =
            angularDistance(input.standardResult.absoluteYawDegrees, state.anchorStandardYawDegrees);
        const double reportedMotion = reportedYawAvailable ? angularDistance(input.reportedYawResult.absoluteYawDegrees,
                                                                             state.anchorReportedYawDegrees)
                                                           : 0.0;
        const double vehicleHeadingMotion =
            vehicleHeadingAvailable
                ? angularDistance(input.vehicleHeadingResult.absoluteYawDegrees, state.anchorVehicleHeadingYawDegrees)
                : 0.0;

        const auto compatibilityCandidateDominates = [&](Selection candidate) {
            const bool candidateAvailable = ((candidate == Selection::ReportedYaw) && reportedYawAvailable) ||
                                            ((candidate == Selection::VehicleHeading) && vehicleHeadingAvailable);
            if (!candidateAvailable) {
                return false;
            }

            const double candidateMotion = candidate == Selection::ReportedYaw ? reportedMotion : vehicleHeadingMotion;
            if ((standardMotion < kReferenceMotionDegrees) ||
                ((candidateMotion + kMotionAdvantageDegrees) > standardMotion)) {
                return false;
            }
            if (reportedYawAvailable && (candidate != Selection::ReportedYaw) &&
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
        } else if ((state.selection == Selection::ReportedYaw) || (state.selection == Selection::VehicleHeading)) {
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
            const bool standardBeatsReported = reportedYawAvailable && (reportedMotion >= kReferenceMotionDegrees) &&
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
                (standardMotion >= kReanchorMotionDegrees) &&
                (!reportedYawAvailable || (reportedMotion >= kReanchorMotionDegrees)) &&
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

GimbalYawLockResolver::CompatibilityMode GimbalYawLockResolver::modeForStatus(bool yawLock, bool yawInVehicleFrame,
                                                                              bool yawInEarthFrame) {
    if (!yawLock || yawInEarthFrame) {
        return CompatibilityMode::None;
    }
    return yawInVehicleFrame ? CompatibilityMode::ExplicitVehicleFrame : CompatibilityMode::LegacyNoFrame;
}

bool GimbalYawLockResolver::senderRestarted(std::uint32_t previousTimeBootMs, std::uint32_t currentTimeBootMs,
                                            std::uint32_t minimumRollbackMs) {
    return (previousTimeBootMs != 0U) && (currentTimeBootMs < previousTimeBootMs) &&
           ((previousTimeBootMs - currentTimeBootMs) > minimumRollbackMs);
}

void GimbalYawLockResolver::reset(State& state) { state = State{}; }
