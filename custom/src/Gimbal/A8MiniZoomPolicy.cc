/****************************************************************************
 *
 * 思翼 A8 Mini 视频分辨率与缩放策略。
 *
 ****************************************************************************/

#include "A8MiniZoomPolicy.h"

#include <QtCore/QtMath>

namespace {

static constexpr double kComparisonTolerance = 0.051;
static constexpr int kTargetConfirmationCount = 2;
static constexpr int kStableDifferentConfirmationCount = 5;

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

    // Normal stops are anchored at minimumZoom. The exact stream-resolution
    // ceiling is also a legal terminal stop, even when the final interval is
    // shorter than one configured step (5.0 -> 5.5, or 3.0 -> 3.5).
    const int maximumTenths = qRound(maximumZoom * 10.0);
    return zoomTenths == maximumTenths
        || (zoomTenths - minimumTenths) % stepTenths == 0;
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

    const int offsetTenths = currentTenths - minimumTenths;
    const int lowerGridTenths =
        minimumTenths + (offsetTenths / stepTenths) * stepTenths;
    int selectedTenths = qBound(minimumTenths, lowerGridTenths, maximumTenths);
    int selectedDistance = qAbs(currentTenths - selectedTenths);
    const auto considerCandidate = [&](int candidateTenths) {
        if (candidateTenths < minimumTenths || candidateTenths > maximumTenths) {
            return;
        }

        const int candidateDistance = qAbs(currentTenths - candidateTenths);
        const bool preferredTie = candidateDistance == selectedDistance
            && ((direction < 0 && candidateTenths < selectedTenths)
                || (direction >= 0 && candidateTenths > selectedTenths));
        if (candidateDistance < selectedDistance || preferredTie) {
            selectedTenths = candidateTenths;
            selectedDistance = candidateDistance;
        }
    };

    considerCandidate(lowerGridTenths + stepTenths);
    // The physical ceiling is a first-class stop, not an arbitrary off-grid
    // value. This lets normalization select 5.5 from 5.3 and 3.5 from 3.3.
    considerCandidate(maximumTenths);
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

    const bool atMaximum = currentTenths == maximumTenths;
    const bool onRegularGrid =
        (currentTenths - minimumTenths) % stepTenths == 0;

    if (atMaximum) {
        if (direction > 0) {
            return false;
        }

        int previousGridTenths =
            minimumTenths
            + ((maximumTenths - minimumTenths) / stepTenths) * stepTenths;
        if (previousGridTenths >= maximumTenths) {
            previousGridTenths -= stepTenths;
        }
        if (previousGridTenths < minimumTenths) {
            return false;
        }
        *targetZoom = previousGridTenths / 10.0;
        return true;
    }

    if (onRegularGrid) {
        const int regularCandidateTenths =
            currentTenths + direction * stepTenths;
        if (direction > 0
            && regularCandidateTenths > maximumTenths
            && currentTenths < maximumTenths) {
            *targetZoom = maximumTenths / 10.0;
            return true;
        }
        if (regularCandidateTenths < minimumTenths
            || regularCandidateTenths > maximumTenths
            || regularCandidateTenths == currentTenths) {
            return false;
        }
        *targetZoom = regularCandidateTenths / 10.0;
        return true;
    }

    // A transient hardware value such as 1.6x is never used as a new grid
    // anchor. Move to the nearest legal stop in the requested direction.
    const int offsetTenths = currentTenths - minimumTenths;
    int candidateTenths = direction > 0
        ? minimumTenths
            + ((offsetTenths + stepTenths - 1) / stepTenths) * stepTenths
        : minimumTenths
            + (offsetTenths / stepTenths) * stepTenths;
    if (direction > 0 && candidateTenths > maximumTenths) {
        candidateTenths = maximumTenths;
    }
    if (candidateTenths < minimumTenths
        || candidateTenths > maximumTenths
        || candidateTenths == currentTenths) {
        return false;
    }

    *targetZoom = candidateTenths / 10.0;
    return true;
}
