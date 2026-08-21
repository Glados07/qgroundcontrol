/****************************************************************************
 *
 * Shared fixed-step zoom-grid policy.
 *
 ****************************************************************************/

#include "ZoomStepPolicy.h"

#include <QtCore/QtMath>

namespace {

constexpr double kComparisonTolerance = 0.051;

bool isLegalZoomTenths(int zoomTenths,
                       int stepTenths,
                       int minimumTenths,
                       int maximumTenths)
{
    if (zoomTenths < minimumTenths || zoomTenths > maximumTenths) {
        return false;
    }

    return zoomTenths == maximumTenths
        || (zoomTenths - minimumTenths) % stepTenths == 0;
}

void considerNearestCandidate(int candidateTenths,
                              int currentTenths,
                              int minimumTenths,
                              int maximumTenths,
                              int tieDirection,
                              bool* selectedValid,
                              int* selectedTenths,
                              int* selectedDistance)
{
    if (!selectedValid
        || !selectedTenths
        || !selectedDistance
        || candidateTenths < minimumTenths
        || candidateTenths > maximumTenths) {
        return;
    }

    const int candidateDistance = qAbs(currentTenths - candidateTenths);
    const bool preferredTie =
        *selectedValid
        && candidateDistance == *selectedDistance
        && ((tieDirection < 0 && candidateTenths < *selectedTenths)
            || (tieDirection >= 0 && candidateTenths > *selectedTenths));
    if (!*selectedValid
        || candidateDistance < *selectedDistance
        || preferredTie) {
        *selectedValid = true;
        *selectedTenths = candidateTenths;
        *selectedDistance = candidateDistance;
    }
}

} // namespace

bool ZoomStepPolicy::isAlignedZoom(double zoomLevel,
                                   double zoomStep,
                                   double minimumZoom,
                                   double maximumZoom)
{
    if (!qIsFinite(zoomLevel)
        || !qIsFinite(zoomStep)
        || !qIsFinite(minimumZoom)
        || !qIsFinite(maximumZoom)
        || zoomStep <= 0.0
        || minimumZoom > maximumZoom
        || zoomLevel < minimumZoom - kComparisonTolerance
        || zoomLevel > maximumZoom + kComparisonTolerance) {
        return false;
    }

    const int zoomTenths = qRound(zoomLevel * 10.0);
    const int stepTenths = qRound(zoomStep * 10.0);
    const int minimumTenths = qRound(minimumZoom * 10.0);
    if (stepTenths < 1) {
        return false;
    }

    const int maximumTenths = qRound(maximumZoom * 10.0);
    return isLegalZoomTenths(
        zoomTenths, stepTenths, minimumTenths, maximumTenths);
}

bool ZoomStepPolicy::alignmentTarget(double currentZoom,
                                     double zoomStep,
                                     double minimumZoom,
                                     double maximumZoom,
                                     int direction,
                                     double* targetZoom)
{
    if (!targetZoom
        || !qIsFinite(currentZoom)
        || !qIsFinite(zoomStep)
        || !qIsFinite(minimumZoom)
        || !qIsFinite(maximumZoom)
        || zoomStep <= 0.0
        || minimumZoom > maximumZoom
        || currentZoom < minimumZoom - kComparisonTolerance
        || currentZoom > maximumZoom + kComparisonTolerance
        || direction < -1
        || direction > 1) {
        return false;
    }

    const int stepTenths = qRound(zoomStep * 10.0);
    const int minimumTenths = qRound(minimumZoom * 10.0);
    const int maximumTenths = qRound(maximumZoom * 10.0);
    if (stepTenths < 1 || minimumTenths > maximumTenths) {
        return false;
    }
    const int currentTenths = qBound(
        minimumTenths,
        qRound(currentZoom * 10.0),
        maximumTenths);

    bool selectedValid = false;
    int selectedTenths = minimumTenths;
    int selectedDistance = 0;

    const int lowerTenths =
        minimumTenths
        + ((currentTenths - minimumTenths) / stepTenths) * stepTenths;
    considerNearestCandidate(lowerTenths,
                             currentTenths,
                             minimumTenths,
                             maximumTenths,
                             direction,
                             &selectedValid,
                             &selectedTenths,
                             &selectedDistance);
    considerNearestCandidate(lowerTenths + stepTenths,
                             currentTenths,
                             minimumTenths,
                             maximumTenths,
                             direction,
                             &selectedValid,
                             &selectedTenths,
                             &selectedDistance);
    considerNearestCandidate(maximumTenths,
                             currentTenths,
                             minimumTenths,
                             maximumTenths,
                             direction,
                             &selectedValid,
                             &selectedTenths,
                             &selectedDistance);
    if (!selectedValid) {
        return false;
    }

    *targetZoom = selectedTenths / 10.0;
    return true;
}

bool ZoomStepPolicy::stepTarget(double currentZoom,
                                double zoomStep,
                                double minimumZoom,
                                double maximumZoom,
                                int direction,
                                double* targetZoom)
{
    if (!targetZoom
        || !qIsFinite(currentZoom)
        || !qIsFinite(zoomStep)
        || !qIsFinite(minimumZoom)
        || !qIsFinite(maximumZoom)
        || zoomStep <= 0.0
        || minimumZoom > maximumZoom
        || (direction != -1 && direction != 1)) {
        return false;
    }

    const int currentTenths = qRound(currentZoom * 10.0);
    const int stepTenths = qRound(zoomStep * 10.0);
    const int minimumTenths = qRound(minimumZoom * 10.0);
    const int maximumTenths = qRound(maximumZoom * 10.0);
    if (stepTenths < 1 || minimumTenths > maximumTenths) {
        return false;
    }

    int candidateTenths = currentTenths;
    if (direction > 0) {
        if (currentTenths >= maximumTenths) {
            return false;
        }
        if (currentTenths < minimumTenths) {
            candidateTenths = minimumTenths;
        } else {
            const int nextMinimumGridTenths =
                minimumTenths
                + (((currentTenths - minimumTenths) / stepTenths) + 1)
                    * stepTenths;
            candidateTenths = qMin(maximumTenths, nextMinimumGridTenths);
        }
    } else {
        if (currentTenths <= minimumTenths) {
            return false;
        }
        if (currentTenths > maximumTenths) {
            candidateTenths = maximumTenths;
        } else {
            candidateTenths =
                minimumTenths
                + ((currentTenths - minimumTenths - 1) / stepTenths)
                    * stepTenths;
        }
    }

    if (candidateTenths < minimumTenths
        || candidateTenths > maximumTenths
        || !isLegalZoomTenths(candidateTenths,
                              stepTenths,
                              minimumTenths,
                              maximumTenths)
        || (direction > 0 && candidateTenths <= currentTenths)
        || (direction < 0 && candidateTenths >= currentTenths)) {
        return false;
    }

    *targetZoom = candidateTenths / 10.0;
    return true;
}
