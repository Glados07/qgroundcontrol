/****************************************************************************
 *
 * 思翼 A8 Mini 视频分辨率与缩放策略。
 *
 ****************************************************************************/

#include "A8MiniZoomPolicy.h"

#include <QtCore/QtMath>

namespace {

static constexpr double kComparisonTolerance = 0.051;
// A validated solicited 0x18 packet that exactly equals the legal target is
// sufficient. Requiring two target samples made normal zooms remain hidden and
// let a newer gesture reset the tracker before any value could be published.
static constexpr int kTargetConfirmationCount = 1;
static constexpr int kStableDifferentConfirmationCount = 5;

static int maximumAnchoredBase(int minimumTenths,
                               int maximumTenths,
                               int stepTenths)
{
    return maximumTenths
        - ((maximumTenths - minimumTenths) / stepTenths) * stepTenths;
}

static bool isLegalZoomTenths(int zoomTenths,
                              int stepTenths,
                              int minimumTenths,
                              int maximumTenths)
{
    if (zoomTenths < minimumTenths || zoomTenths > maximumTenths) {
        return false;
    }

    return (zoomTenths - minimumTenths) % stepTenths == 0
        || (maximumTenths - zoomTenths) % stepTenths == 0;
}

static void considerNearestCandidate(int candidateTenths,
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

static void considerLatticeNeighbours(int latticeBaseTenths,
                                      int stepTenths,
                                      int currentTenths,
                                      int minimumTenths,
                                      int maximumTenths,
                                      int tieDirection,
                                      bool* selectedValid,
                                      int* selectedTenths,
                                      int* selectedDistance)
{
    if (currentTenths < latticeBaseTenths) {
        considerNearestCandidate(latticeBaseTenths,
                                 currentTenths,
                                 minimumTenths,
                                 maximumTenths,
                                 tieDirection,
                                 selectedValid,
                                 selectedTenths,
                                 selectedDistance);
        return;
    }

    const int lowerTenths =
        latticeBaseTenths
        + ((currentTenths - latticeBaseTenths) / stepTenths) * stepTenths;
    considerNearestCandidate(lowerTenths,
                             currentTenths,
                             minimumTenths,
                             maximumTenths,
                             tieDirection,
                             selectedValid,
                             selectedTenths,
                             selectedDistance);
    considerNearestCandidate(lowerTenths + stepTenths,
                             currentTenths,
                             minimumTenths,
                             maximumTenths,
                             tieDirection,
                             selectedValid,
                             selectedTenths,
                             selectedDistance);
}

static void considerDirectionalCandidate(int candidateTenths,
                                         int currentTenths,
                                         int minimumTenths,
                                         int maximumTenths,
                                         int direction,
                                         bool* selectedValid,
                                         int* selectedTenths)
{
    if (!selectedValid
        || !selectedTenths
        || candidateTenths < minimumTenths
        || candidateTenths > maximumTenths
        || (direction > 0 && candidateTenths <= currentTenths)
        || (direction < 0 && candidateTenths >= currentTenths)) {
        return;
    }

    if (!*selectedValid
        || (direction > 0 && candidateTenths < *selectedTenths)
        || (direction < 0 && candidateTenths > *selectedTenths)) {
        *selectedValid = true;
        *selectedTenths = candidateTenths;
    }
}

static void considerStrictDirectionalLattice(int latticeBaseTenths,
                                             int stepTenths,
                                             int currentTenths,
                                             int minimumTenths,
                                             int maximumTenths,
                                             int direction,
                                             bool* selectedValid,
                                             int* selectedTenths)
{
    int candidateTenths = latticeBaseTenths;
    if (direction > 0) {
        if (currentTenths >= latticeBaseTenths) {
            candidateTenths =
                latticeBaseTenths
                + (((currentTenths - latticeBaseTenths) / stepTenths) + 1)
                    * stepTenths;
        }
    } else {
        if (currentTenths <= latticeBaseTenths) {
            return;
        }
        candidateTenths =
            latticeBaseTenths
            + ((currentTenths - latticeBaseTenths - 1) / stepTenths)
                * stepTenths;
    }

    considerDirectionalCandidate(candidateTenths,
                                 currentTenths,
                                 minimumTenths,
                                 maximumTenths,
                                 direction,
                                 selectedValid,
                                 selectedTenths);
}

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
    _differentCandidate = 1.0;
    _targetMatchCount = 0;
    _differentMatchCount = 0;
    _differentCandidateValid = false;
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
        _differentCandidateValid = false;
        _differentMatchCount = 0;
        ++_targetMatchCount;
        return _targetMatchCount >= kTargetConfirmationCount
            ? TargetObservation::TargetReached
            : TargetObservation::Waiting;
    }

    _targetMatchCount = 0;
    if (!_differentCandidateValid
        || qAbs(normalizedActual - _differentCandidate) > kComparisonTolerance) {
        _differentCandidate = normalizedActual;
        _differentCandidateValid = true;
        _differentMatchCount = 1;
    } else {
        ++_differentMatchCount;
    }

    // Repeated one-decimal samples are not immediate proof of a stopped lens:
    // a moving A8 can report 1.6x several times on its way to 2.0x. The SDK
    // sequence is fixed to zero, so even a negative ACK cannot safely shorten
    // this evidence window after targets are replaced. The Manager adds a
    // command-age gate before acting on these five matching samples.
    return _differentMatchCount >= kStableDifferentConfirmationCount
        ? TargetObservation::StableDifferent
        : TargetObservation::Waiting;
}

bool A8MiniZoomPolicy::maximumZoomForVideoResolution(quint16 width,
                                                     quint16 height,
                                                     double* maximumZoom)
{
    if (!maximumZoom) {
        return false;
    }

    double resolvedMaximum = 0.0;
    if ((width == 3840 || width == 4096) && height == 2160) {
        // A8 Mini在4K模式下不支持数字变焦；1.0x是已知且不可继续的边界。
        resolvedMaximum = 1.0;
    } else if (width == 2560 && height == 1440) {
        resolvedMaximum = 3.5;
    } else if (width == 1920 && (height == 1080 || height == 1088)) {
        // Some device decoder paths expose 1080P with an aligned coded height.
        resolvedMaximum = 5.5;
    } else if (width == 1280 && (height == 720 || height == 736)) {
        // Some decoder paths align 720P coded height to 16 pixels.
        resolvedMaximum = 6.0;
    } else {
        return false;
    }

    *maximumZoom = resolvedMaximum;
    return true;
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

    // The pulled-video ceiling is itself a legal terminal stop even when the
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

    // Zoom-in follows the minimum-anchored lattice, while zoom-out from the
    // exact stream ceiling follows the maximum-anchored lattice. Both sets are
    // legal so reversing a gesture preserves full configured steps:
    // 1 -> 2 -> ... -> 5 -> 5.5 and 5.5 -> 4.5 -> ... -> 1.
    const int maximumTenths = qRound(maximumZoom * 10.0);
    return isLegalZoomTenths(
        zoomTenths, stepTenths, minimumTenths, maximumTenths);
}

bool A8MiniZoomPolicy::feedbackReachesTarget(double actualZoom,
                                             double sourceZoom,
                                             double targetZoom)
{
    if (!qIsFinite(actualZoom)
        || !qIsFinite(sourceZoom)
        || !qIsFinite(targetZoom)) {
        return false;
    }

    if (qAbs(targetZoom - sourceZoom) <= kComparisonTolerance) {
        return qAbs(actualZoom - targetZoom) <= kComparisonTolerance;
    }

    const double midpoint = (sourceZoom + targetZoom) / 2.0;
    const double halfInterval = qAbs(targetZoom - sourceZoom) / 2.0;
    if (targetZoom > sourceZoom) {
        // Do not apply the normal one-decimal equality tolerance at a
        // midpoint. At the supported 0.1x minimum step, 0.051 is wider than
        // the half-step and would classify an unchanged source sample as
        // having reached the target bucket.
        return actualZoom >= midpoint
            && actualZoom < targetZoom + halfInterval;
    }
    return actualZoom <= midpoint
        && actualZoom > targetZoom - halfInterval;
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

    const int fullStepsBeforeTerminal =
        (maximumTenths - minimumTenths - 1) / stepTenths;
    const int handoffTenths = direction > 0
        ? minimumTenths + fullStepsBeforeTerminal * stepTenths
        : maximumTenths - fullStepsBeforeTerminal * stepTenths;
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

    const int currentTenths = qRound(currentZoom * 10.0);
    const int stepTenths = qRound(zoomStep * 10.0);
    const int minimumTenths = qRound(minimumZoom * 10.0);
    const int maximumTenths = qRound(maximumZoom * 10.0);
    if (stepTenths < 1) {
        return false;
    }

    const int maximumBaseTenths =
        maximumAnchoredBase(minimumTenths, maximumTenths, stepTenths);
    bool selectedValid = false;
    int selectedTenths = minimumTenths;
    int selectedDistance = 0;
    considerLatticeNeighbours(minimumTenths,
                              stepTenths,
                              currentTenths,
                              minimumTenths,
                              maximumTenths,
                              direction,
                              &selectedValid,
                              &selectedTenths,
                              &selectedDistance);
    considerLatticeNeighbours(maximumBaseTenths,
                              stepTenths,
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

bool A8MiniZoomPolicy::stepTarget(double currentZoom,
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

    // 正常单击和长按从固定分度点出发。若硬件拒绝自动归整而留下非网格
    // 实际值，则下一次人工操作只移动到该方向最近的合法网格点，绝不发送
    // current+step形成1.8→2.8一类漂移网格。
    const int currentTenths = qRound(currentZoom * 10.0);
    const int stepTenths = qRound(zoomStep * 10.0);
    const int minimumTenths = qRound(minimumZoom * 10.0);
    const int maximumTenths = qRound(maximumZoom * 10.0);
    if (stepTenths < 1) {
        return false;
    }

    if (currentTenths > maximumTenths) {
        if (direction < 0) {
            *targetZoom = maximumTenths / 10.0;
            return true;
        }
        return false;
    }
    if (currentTenths < minimumTenths) {
        if (direction > 0) {
            *targetZoom = minimumTenths / 10.0;
            return true;
        }
        return false;
    }

    if (isLegalZoomTenths(
            currentTenths, stepTenths, minimumTenths, maximumTenths)) {
        const int candidateTenths = qBound(
            minimumTenths,
            currentTenths + direction * stepTenths,
            maximumTenths);
        if (candidateTenths == currentTenths) {
            return false;
        }
        *targetZoom = candidateTenths / 10.0;
        return true;
    }

    // A transient hardware value such as 1.6x is never used as a new grid
    // anchor. Select the closest legal point strictly in the requested
    // direction from the union of the minimum- and maximum-anchored lattices.
    const int maximumBaseTenths =
        maximumAnchoredBase(minimumTenths, maximumTenths, stepTenths);
    bool selectedValid = false;
    int candidateTenths = currentTenths;
    considerStrictDirectionalLattice(minimumTenths,
                                     stepTenths,
                                     currentTenths,
                                     minimumTenths,
                                     maximumTenths,
                                     direction,
                                     &selectedValid,
                                     &candidateTenths);
    considerStrictDirectionalLattice(maximumBaseTenths,
                                     stepTenths,
                                     currentTenths,
                                     minimumTenths,
                                     maximumTenths,
                                     direction,
                                     &selectedValid,
                                     &candidateTenths);
    if (!selectedValid) {
        return false;
    }

    *targetZoom = candidateTenths / 10.0;
    return true;
}
