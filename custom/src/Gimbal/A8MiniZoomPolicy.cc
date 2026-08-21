/****************************************************************************
 *
 * 思翼 A8 Mini 视频分辨率与缩放策略。
 *
 ****************************************************************************/

#include "A8MiniZoomPolicy.h"
#include "ZoomStepPolicy.h"

#include <QtCore/QtMath>

namespace {

static constexpr double kComparisonTolerance = 0.051;
// One validated solicited 0x18 sample which exactly equals the legal target is
// sufficient to finish actual-position verification. Target display is owned
// separately by the Manager and does not wait for this tracker.
static constexpr int kTargetConfirmationCount = 1;

} // namespace

void A8MiniZoomPolicy::TargetTracker::reset(double targetZoom)
{
    clear();
    if (!qIsFinite(targetZoom)) {
        return;
    }

    _targetZoom = qRound(targetZoom * 10.0) / 10.0;
    _active = true;
}

void A8MiniZoomPolicy::TargetTracker::clear()
{
    _targetZoom = 1.0;
    _targetMatchCount = 0;
    _active = false;
}

A8MiniZoomPolicy::TargetObservation
A8MiniZoomPolicy::TargetTracker::observe(double actualZoom)
{
    if (!_active || !qIsFinite(actualZoom)) {
        return TargetObservation::Waiting;
    }

    const double normalizedActual = qRound(actualZoom * 10.0) / 10.0;
    if (qAbs(normalizedActual - _targetZoom) <= kComparisonTolerance) {
        ++_targetMatchCount;
        return _targetMatchCount >= kTargetConfirmationCount
            ? TargetObservation::TargetReached
            : TargetObservation::Waiting;
    }

    _targetMatchCount = 0;
    // No amount of repeated intermediate feedback can complete or cancel an
    // active 0x0f target. The Manager owns one bounded operation deadline and
    // accepts only exact solicited 0x18 target feedback before it expires.
    return TargetObservation::Waiting;
}

bool A8MiniZoomPolicy::isSupportedPulledVideoResolution(quint16 width,
                                                        quint16 height)
{
    return (width == 1920 && height == 1080)
        || (width == 1280 && height == 720);
}

bool A8MiniZoomPolicy::maximumZoomForRecordingResolution(
    quint16 width,
    quint16 height,
    double* maximumZoom)
{
    if (!maximumZoom) {
        return false;
    }

    if ((width == 3840 || width == 4096) && height == 2160) {
        *maximumZoom = 1.0;
        return true;
    }
    if (width == 2560 && height == 1440) {
        *maximumZoom = 3.5;
        return true;
    }
    if (width == 1920 && height == 1080) {
        *maximumZoom = 5.5;
        return true;
    }
    if (width == 1280 && height == 720) {
        *maximumZoom = 6.0;
        return true;
    }
    return false;
}

bool A8MiniZoomPolicy::alignedMaximumZoom(double capabilityMaximumZoom,
                                          double zoomStep,
                                          double minimumZoom,
                                          double* maximumZoom)
{
    if (!maximumZoom
        || !qIsFinite(capabilityMaximumZoom)
        || !qIsFinite(zoomStep)
        || !qIsFinite(minimumZoom)
        || zoomStep <= 0.0
        || capabilityMaximumZoom < minimumZoom) {
        return false;
    }

    const int capabilityTenths = qRound(capabilityMaximumZoom * 10.0);
    const int stepTenths = qRound(zoomStep * 10.0);
    const int minimumTenths = qRound(minimumZoom * 10.0);
    if (stepTenths < 1 || capabilityTenths < minimumTenths) {
        return false;
    }

    // The recording-mode ceiling is itself a legal terminal stop even when the
    // final interval is shorter than zoomStep (for example 5.0 -> 5.5 at
    // 1080P with a 1.0x step). Keep the historical function name because it is
    // part of the custom policy API, but do not discard that terminal stop.
    *maximumZoom = capabilityTenths / 10.0;
    return true;
}

bool A8MiniZoomPolicy::isAlignedZoom(double zoomLevel,
                                     double zoomStep,
                                     double minimumZoom,
                                     double maximumZoom)
{
    return ZoomStepPolicy::isAlignedZoom(
        zoomLevel, zoomStep, minimumZoom, maximumZoom);
}

bool A8MiniZoomPolicy::feedbackReachedStop(double actualZoom,
                                           double targetZoom,
                                           int direction)
{
    if (!qIsFinite(actualZoom)
        || !qIsFinite(targetZoom)
        || (direction != -1 && direction != 1)) {
        return false;
    }

    // Legacy progress users may advance only when the real 0x18 value reaches
    // or passes the next legal stop in the active direction. In particular,
    // this helper never renames 1.6x to 2.0x.
    return direction > 0
        ? actualZoom >= targetZoom - kComparisonTolerance
        : actualZoom <= targetZoom + kComparisonTolerance;
}

bool A8MiniZoomPolicy::exactDirectionalProgressStop(double currentZoom,
                                                    double targetZoom,
                                                    double actualZoom,
                                                    double zoomStep,
                                                    double minimumZoom,
                                                    double maximumZoom,
                                                    double* progressZoom)
{
    if (!progressZoom
        || !qIsFinite(currentZoom)
        || !qIsFinite(targetZoom)
        || !qIsFinite(actualZoom)
        || !isAlignedZoom(currentZoom,
                          zoomStep,
                          minimumZoom,
                          maximumZoom)
        || !isAlignedZoom(targetZoom,
                          zoomStep,
                          minimumZoom,
                          maximumZoom)
        || !isAlignedZoom(actualZoom,
                          zoomStep,
                          minimumZoom,
                          maximumZoom)) {
        return false;
    }

    const double normalizedActual = qRound(actualZoom * 10.0) / 10.0;
    if (qAbs(normalizedActual - targetZoom) <= kComparisonTolerance) {
        *progressZoom = qRound(targetZoom * 10.0) / 10.0;
        return true;
    }

    const int direction =
        targetZoom > currentZoom + kComparisonTolerance
            ? 1
            : (targetZoom < currentZoom - kComparisonTolerance ? -1 : 0);
    if (direction == 0
        || qAbs(normalizedActual - currentZoom)
            <= kComparisonTolerance) {
        return false;
    }

    // Follow only the single configured path for this gesture. Off-grid
    // samples have already been rejected above, so no midpoint or rounding can
    // manufacture a value.
    double pathStop = qRound(currentZoom * 10.0) / 10.0;
    for (int examinedStops = 0; examinedStops < 64; ++examinedStops) {
        double nextStop = 0.0;
        if (!stepTarget(pathStop,
                        zoomStep,
                        minimumZoom,
                        maximumZoom,
                        direction,
                        &nextStop)) {
            return false;
        }
        if ((direction > 0
             && nextStop > targetZoom + kComparisonTolerance)
            || (direction < 0
                && nextStop < targetZoom - kComparisonTolerance)) {
            return false;
        }

        if (qAbs(normalizedActual - nextStop)
            <= kComparisonTolerance) {
            *progressZoom = nextStop;
            return true;
        }

        const bool feedbackPassedStop = direction > 0
            ? normalizedActual > nextStop + kComparisonTolerance
            : normalizedActual < nextStop - kComparisonTolerance;
        if (!feedbackPassedStop) {
            return false;
        }
        pathStop = nextStop;
    }
    return false;
}

bool A8MiniZoomPolicy::terminalHandoffStop(double zoomStep,
                                           double minimumZoom,
                                           double maximumZoom,
                                           int direction,
                                           double* handoffZoom)
{
    if (!handoffZoom
        || !qIsFinite(zoomStep)
        || !qIsFinite(minimumZoom)
        || !qIsFinite(maximumZoom)
        || zoomStep <= 0.0
        || minimumZoom >= maximumZoom
        || (direction != -1 && direction != 1)) {
        return false;
    }

    const int stepTenths = qRound(zoomStep * 10.0);
    const int minimumTenths = qRound(minimumZoom * 10.0);
    const int maximumTenths = qRound(maximumZoom * 10.0);
    if (stepTenths < 1 || minimumTenths >= maximumTenths) {
        return false;
    }

    int handoffTenths = minimumTenths;
    if (direction > 0) {
        // Last canonical stop strictly below the exact upper endpoint.
        handoffTenths =
            minimumTenths
            + ((maximumTenths - minimumTenths - 1) / stepTenths)
                * stepTenths;
    } else {
        // First canonical stop strictly above the lower endpoint. If the
        // configured step spans the entire range, the upper endpoint is it.
        handoffTenths =
            qMin(maximumTenths, minimumTenths + stepTenths);
    }
    if (handoffTenths < minimumTenths
        || handoffTenths > maximumTenths) {
        return false;
    }
    *handoffZoom = handoffTenths / 10.0;
    return true;
}

bool A8MiniZoomPolicy::alignmentTarget(double currentZoom,
                                       double zoomStep,
                                       double minimumZoom,
                                       double maximumZoom,
                                       int direction,
                                       double* targetZoom)
{
    return ZoomStepPolicy::alignmentTarget(currentZoom,
                                           zoomStep,
                                           minimumZoom,
                                           maximumZoom,
                                           direction,
                                           targetZoom);
}

bool A8MiniZoomPolicy::stepTarget(double currentZoom,
                                  double zoomStep,
                                  double minimumZoom,
                                  double maximumZoom,
                                  int direction,
                                  double* targetZoom)
{
    return ZoomStepPolicy::stepTarget(currentZoom,
                                      zoomStep,
                                      minimumZoom,
                                      maximumZoom,
                                      direction,
                                      targetZoom);
}

bool A8MiniZoomPolicy::heldTarget(double startZoom,
                                  int direction,
                                  qint64 elapsedMs,
                                  double zoomStep,
                                  double minimumZoom,
                                  double maximumZoom,
                                  double* targetZoom)
{
    return heldTarget(startZoom,
                      direction,
                      elapsedMs,
                      zoomStep,
                      minimumZoom,
                      maximumZoom,
                      kDefaultHeldZoomStepPeriodMs,
                      targetZoom);
}

bool A8MiniZoomPolicy::heldTarget(double startZoom,
                                  int direction,
                                  qint64 elapsedMs,
                                  double zoomStep,
                                  double minimumZoom,
                                  double maximumZoom,
                                  qint64 stepPeriodMs,
                                  double* targetZoom)
{
    if (!targetZoom
        || elapsedMs < kMinimumHeldZoomElapsedMs
        || stepPeriodMs <= 0
        || (direction != -1 && direction != 1)
        || !qIsFinite(startZoom)
        || !qIsFinite(zoomStep)
        || !qIsFinite(minimumZoom)
        || !qIsFinite(maximumZoom)) {
        return false;
    }

    // Positive-duration qRound semantics without floating-point overflow:
    // exact half periods round up to the next configured stop.
    const qint64 completePeriods = elapsedMs / stepPeriodMs;
    const qint64 remainderMs = elapsedMs % stepPeriodMs;
    const qint64 halfPeriodMs =
        stepPeriodMs / 2 + stepPeriodMs % 2;
    const qint64 roundedPeriods =
        completePeriods + (remainderMs >= halfPeriodMs ? 1 : 0);
    const qint64 requestedAdvances = qMax<qint64>(1, roundedPeriods);

    const int stepTenths = qRound(zoomStep * 10.0);
    const int minimumTenths = qRound(minimumZoom * 10.0);
    const int maximumTenths = qRound(maximumZoom * 10.0);
    if (stepTenths < 1
        || minimumTenths > maximumTenths) {
        return false;
    }

    // At most this many distinct stops exist, including the exact terminal
    // endpoint. Capping the loop also makes arbitrarily large elapsed values
    // saturate immediately and safely.
    const qint64 maximumAdvances =
        (maximumTenths - minimumTenths) / stepTenths + 2;
    const qint64 advancesToApply =
        qMin(requestedAdvances, maximumAdvances);

    double resolvedTarget = startZoom;
    bool moved = false;
    for (qint64 advance = 0; advance < advancesToApply; ++advance) {
        double nextTarget = resolvedTarget;
        if (!stepTarget(resolvedTarget,
                        zoomStep,
                        minimumZoom,
                        maximumZoom,
                        direction,
                        &nextTarget)) {
            break;
        }
        resolvedTarget = nextTarget;
        moved = true;
    }
    if (!moved) {
        return false;
    }

    *targetZoom = resolvedTarget;
    return true;
}
