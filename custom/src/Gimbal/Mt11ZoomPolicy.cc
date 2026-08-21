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
constexpr int kMinimumHeldStepCadenceMs = 350;
constexpr int kMaximumHeldStepCadenceMs = 2000;
constexpr int kHeldStepCadencePerZoomMs = 600;

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

bool Mt11ZoomPolicy::holdDirectionAvailable(double measuredZoom,
                                            double displayZoom,
                                            double zoomStep,
                                            double deviceMaximumZoom,
                                            int direction,
                                            bool absoluteCommandPending)
{
    double targetZoom = displayZoom;
    return heldGestureStepTarget(measuredZoom,
                                 displayZoom,
                                 zoomStep,
                                 deviceMaximumZoom,
                                 direction,
                                 absoluteCommandPending,
                                 &targetZoom);
}

bool Mt11ZoomPolicy::heldStepTarget(double displayZoom,
                                    double zoomStep,
                                    double deviceMaximumZoom,
                                    int direction,
                                    double* targetZoom)
{
    if (!targetZoom
        || (direction != -1 && direction != 1)
        || !validDomain(zoomStep, deviceMaximumZoom)) {
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

    return ZoomStepPolicy::stepTarget(displayZoom,
                                      zoomStep,
                                      MinimumZoom,
                                      maximumDisplayZoom,
                                      direction,
                                      targetZoom);
}

bool Mt11ZoomPolicy::heldGestureStepTarget(double measuredZoom,
                                           double displayZoom,
                                           double zoomStep,
                                           double deviceMaximumZoom,
                                           int direction,
                                           bool absoluteCommandPending,
                                           double* targetZoom)
{
    if (!targetZoom
        || (direction != -1 && direction != 1)
        || !validMeasuredZoom(measuredZoom, deviceMaximumZoom)
        || !isDisplayTarget(displayZoom,
                            zoomStep,
                            deviceMaximumZoom)) {
        return false;
    }

    const double pendingDelta = displayZoom - measuredZoom;
    if (absoluteCommandPending
        && pendingDelta * direction > kComparisonTolerance) {
        *targetZoom = qRound(displayZoom * 10.0) / 10.0;
        return true;
    }

    return heldStepTarget(displayZoom,
                          zoomStep,
                          deviceMaximumZoom,
                          direction,
                          targetZoom);
}

int Mt11ZoomPolicy::heldStepCadenceMs(double zoomStep)
{
    if (!qIsFinite(zoomStep) || zoomStep <= 0.0) {
        return 0;
    }
    return qBound(kMinimumHeldStepCadenceMs,
                  qRound(zoomStep * kHeldStepCadencePerZoomMs),
                  kMaximumHeldStepCadenceMs);
}

bool Mt11ZoomPolicy::heldStepTargetReached(double measuredZoom,
                                           double targetZoom,
                                           int direction)
{
    if (!qIsFinite(measuredZoom)
        || !qIsFinite(targetZoom)
        || (direction != -1 && direction != 1)) {
        return false;
    }
    return direction > 0
        ? measuredZoom >= targetZoom - kComparisonTolerance
        : measuredZoom <= targetZoom + kComparisonTolerance;
}
