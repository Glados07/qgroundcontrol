/****************************************************************************
 *
 * UniPod MT11 display-target and tap zoom policy.
 *
 ****************************************************************************/

#include "Mt11ZoomPolicy.h"

#include "ZoomStepPolicy.h"

#include <QtCore/QtMath>

namespace {

constexpr double kComparisonTolerance = 0.051;

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

bool absoluteBoundaryAvailable(double deviceMaximumZoom)
{
    return deviceMaximumZoom
        >= Mt11ZoomPolicy::AbsoluteCommandMaximumZoom
            - kComparisonTolerance;
}

bool isAbsoluteBoundaryTarget(double zoomLevel,
                              double deviceMaximumZoom)
{
    return absoluteBoundaryAvailable(deviceMaximumZoom)
        && qAbs(zoomLevel
                - Mt11ZoomPolicy::AbsoluteCommandMaximumZoom)
            <= kComparisonTolerance;
}

bool preferNearestTarget(double referenceZoom,
                         double candidateZoom,
                         double selectedZoom,
                         int preferredDirection)
{
    const int referenceTenths = qRound(referenceZoom * 10.0);
    const int candidateTenths = qRound(candidateZoom * 10.0);
    const int selectedTenths = qRound(selectedZoom * 10.0);
    const int candidateDistance = qAbs(candidateTenths - referenceTenths);
    const int selectedDistance = qAbs(selectedTenths - referenceTenths);
    return candidateDistance < selectedDistance
        || (candidateDistance == selectedDistance
            && ((preferredDirection < 0
                 && candidateTenths < selectedTenths)
                || (preferredDirection >= 0
                    && candidateTenths > selectedTenths)));
}

} // namespace

bool Mt11ZoomPolicy::isDisplayTarget(double zoomLevel,
                                     double zoomStep,
                                     double deviceMaximumZoom)
{
    double maximumDisplayZoom = MinimumZoom;
    if (!displayGridMaximum(
            zoomStep, deviceMaximumZoom, &maximumDisplayZoom)) {
        return false;
    }

    return ZoomStepPolicy::isAlignedZoom(zoomLevel,
                                         zoomStep,
                                         MinimumZoom,
                                         maximumDisplayZoom)
        || isAbsoluteBoundaryTarget(zoomLevel, deviceMaximumZoom);
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
    // ZoomStepPolicy treats its exact maximum as a legal terminal stop. Pass
    // the documented 30x protocol boundary through unchanged so a 2x grid is
    // 1, 3, ..., 29, 30 in both directions, rather than truncating at 29x.
    double tapMaximum = tapPhysicalMaximum;
    if (!absoluteBoundaryAvailable(deviceMaximumZoom)
        && !displayGridMaximum(zoomStep,
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
    double selectedTarget = MinimumZoom;
    if (!ZoomStepPolicy::alignmentTarget(boundedMeasured,
                                         zoomStep,
                                         MinimumZoom,
                                         maximumDisplayZoom,
                                         preferredDirection,
                                         &selectedTarget)) {
        return false;
    }

    if (absoluteBoundaryAvailable(deviceMaximumZoom)
        && preferNearestTarget(measuredZoom,
                               AbsoluteCommandMaximumZoom,
                               selectedTarget,
                               preferredDirection)) {
        selectedTarget = AbsoluteCommandMaximumZoom;
    }
    *displayTarget = selectedTarget;
    return true;
}
