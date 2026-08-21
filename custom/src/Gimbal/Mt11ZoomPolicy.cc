/****************************************************************************
 *
 * UniPod MT11 display-target and gesture zoom policy.
 *
 ****************************************************************************/

#include "Mt11ZoomPolicy.h"

#include "ZoomStepPolicy.h"

#include <QtCore/QtMath>

namespace {

constexpr double kComparisonTolerance = 0.051;
// MT11 feedback has one decimal place. This covers the complete 1.0x-255.9x
// wire domain even with the minimum supported 0.1x display step.
constexpr int kMaximumExaminedStops = 4096;

bool validDomain(double zoomStep, double deviceMaximumZoom)
{
    return qIsFinite(zoomStep)
        && qIsFinite(deviceMaximumZoom)
        && zoomStep > 0.0
        && deviceMaximumZoom >= Mt11ZoomPolicy::MinimumZoom;
}

bool validMeasuredZoom(double measuredZoom, double deviceMaximumZoom)
{
    return qIsFinite(measuredZoom)
        && measuredZoom >= Mt11ZoomPolicy::MinimumZoom - kComparisonTolerance
        && measuredZoom <= deviceMaximumZoom + kComparisonTolerance;
}

bool displayGridMaximum(double zoomStep,
                        double deviceMaximumZoom,
                        double* maximumZoom)
{
    if (!maximumZoom || !validDomain(zoomStep, deviceMaximumZoom)) {
        return false;
    }

    const int stepTenths = qRound(zoomStep * 10.0);
    const int minimumTenths = qRound(Mt11ZoomPolicy::MinimumZoom * 10.0);
    const int deviceMaximumTenths = qRound(deviceMaximumZoom * 10.0);
    if (stepTenths < 1 || deviceMaximumTenths < minimumTenths) {
        return false;
    }

    const int fullStepCount =
        (deviceMaximumTenths - minimumTenths) / stepTenths;
    *maximumZoom =
        (minimumTenths + fullStepCount * stepTenths) / 10.0;
    return true;
}

} // namespace

bool Mt11ZoomPolicy::isDisplayTarget(double zoomLevel,
                                     double zoomStep,
                                     double deviceMaximumZoom)
{
    double maximumDisplayZoom = MinimumZoom;
    return displayGridMaximum(
               zoomStep, deviceMaximumZoom, &maximumDisplayZoom)
        && ZoomStepPolicy::isAlignedZoom(zoomLevel,
                                         zoomStep,
                                         MinimumZoom,
                                         maximumDisplayZoom);
}

bool Mt11ZoomPolicy::tapTarget(double measuredZoom,
                               double displayZoom,
                               double zoomStep,
                               double deviceMaximumZoom,
                               int direction,
                               double* targetZoom)
{
    if (!targetZoom
        || (direction != -1 && direction != 1)
        || !validDomain(zoomStep, deviceMaximumZoom)
        || !validMeasuredZoom(measuredZoom, deviceMaximumZoom)
        || measuredZoom > AbsoluteCommandMaximumZoom + kComparisonTolerance) {
        return false;
    }

    const double tapPhysicalMaximum = qMin(
        deviceMaximumZoom, AbsoluteCommandMaximumZoom);
    double tapMaximum = MinimumZoom;
    if (!displayGridMaximum(zoomStep,
                            tapPhysicalMaximum,
                            &tapMaximum)) {
        return false;
    }
    if (!ZoomStepPolicy::isAlignedZoom(displayZoom,
                                       zoomStep,
                                       MinimumZoom,
                                       tapMaximum)) {
        return false;
    }

    return ZoomStepPolicy::stepTarget(displayZoom,
                                      zoomStep,
                                      MinimumZoom,
                                      tapMaximum,
                                      direction,
                                      targetZoom);
}

bool Mt11ZoomPolicy::alignedDisplayTarget(double measuredZoom,
                                          double zoomStep,
                                          double deviceMaximumZoom,
                                          int preferredDirection,
                                          double* displayTarget)
{
    if (!displayTarget
        || preferredDirection < -1
        || preferredDirection > 1
        || !validDomain(zoomStep, deviceMaximumZoom)
        || !validMeasuredZoom(measuredZoom, deviceMaximumZoom)) {
        return false;
    }

    double maximumDisplayZoom = MinimumZoom;
    if (!displayGridMaximum(zoomStep,
                            deviceMaximumZoom,
                            &maximumDisplayZoom)) {
        return false;
    }
    const double boundedMeasured = qBound(
        MinimumZoom, measuredZoom, maximumDisplayZoom);
    return ZoomStepPolicy::alignmentTarget(boundedMeasured,
                                           zoomStep,
                                           MinimumZoom,
                                           maximumDisplayZoom,
                                           preferredDirection,
                                           displayTarget);
}

bool Mt11ZoomPolicy::holdStartDisplayTarget(double measuredZoom,
                                            double displayZoom,
                                            double zoomStep,
                                            double deviceMaximumZoom,
                                            int direction,
                                            bool absoluteCommandPending,
                                            double* displayTarget)
{
    if (!displayTarget || (direction != -1 && direction != 1)) {
        return false;
    }

    if (absoluteCommandPending) {
        if (!validMeasuredZoom(measuredZoom, deviceMaximumZoom)
            || !isDisplayTarget(displayZoom,
                                zoomStep,
                                deviceMaximumZoom)) {
            return false;
        }
        *displayTarget = qRound(displayZoom * 10.0) / 10.0;
        return true;
    }

    return alignedDisplayTarget(measuredZoom,
                                zoomStep,
                                deviceMaximumZoom,
                                direction,
                                displayTarget);
}

bool Mt11ZoomPolicy::holdDirectionAvailable(double measuredZoom,
                                            double displayZoom,
                                            double zoomStep,
                                            double deviceMaximumZoom,
                                            int direction,
                                            bool absoluteCommandPending)
{
    if ((direction != -1 && direction != 1)
        || !validMeasuredZoom(measuredZoom, deviceMaximumZoom)) {
        return false;
    }

    const auto canMoveFrom = [direction, deviceMaximumZoom](double zoom) {
        return direction > 0
            ? zoom < deviceMaximumZoom - kComparisonTolerance
            : zoom > MinimumZoom + kComparisonTolerance;
    };
    if (canMoveFrom(measuredZoom)) {
        return true;
    }

    // The device may already be moving away from the last sample under 0x0f.
    // Treat the newest legal target as an additional feasibility bound, not
    // as proof of physical position. This allows an opposite held gesture to
    // explicitly preempt the pending absolute generation at either endpoint.
    return absoluteCommandPending
        && isDisplayTarget(displayZoom, zoomStep, deviceMaximumZoom)
        && canMoveFrom(displayZoom);
}

bool Mt11ZoomPolicy::heldProgressTarget(double displayZoom,
                                        double measuredZoom,
                                        double zoomStep,
                                        double deviceMaximumZoom,
                                        int direction,
                                        double* displayTarget)
{
    if (!displayTarget
        || (direction != -1 && direction != 1)
        || !validDomain(zoomStep, deviceMaximumZoom)
        || !validMeasuredZoom(measuredZoom, deviceMaximumZoom)) {
        return false;
    }

    double maximumDisplayZoom = MinimumZoom;
    if (!displayGridMaximum(zoomStep,
                            deviceMaximumZoom,
                            &maximumDisplayZoom)
        || !ZoomStepPolicy::isAlignedZoom(displayZoom,
                                          zoomStep,
                                          MinimumZoom,
                                          maximumDisplayZoom)) {
        return false;
    }

    double resolvedTarget = qRound(displayZoom * 10.0) / 10.0;
    for (int examinedStops = 0;
         examinedStops < kMaximumExaminedStops;
         ++examinedStops) {
        double nextTarget = resolvedTarget;
        if (!ZoomStepPolicy::stepTarget(resolvedTarget,
                                        zoomStep,
                                        MinimumZoom,
                                        maximumDisplayZoom,
                                        direction,
                                        &nextTarget)) {
            break;
        }

        const bool reached = direction > 0
            ? measuredZoom >= nextTarget - kComparisonTolerance
            : measuredZoom <= nextTarget + kComparisonTolerance;
        if (!reached) {
            break;
        }
        resolvedTarget = nextTarget;
    }

    *displayTarget = resolvedTarget;
    return true;
}
