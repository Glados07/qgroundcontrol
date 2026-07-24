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
static constexpr int kStableDifferentConfirmationCount = 3;
static constexpr int kRejectedStableConfirmationCount = 2;

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
    _commandRejected = false;
    _active = false;
}

void A8MiniZoomPolicy::TargetTracker::markCommandRejected()
{
    if (_active) {
        _commandRejected = true;
    }
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

    const int requiredCount = _commandRejected
        ? kRejectedStableConfirmationCount
        : kStableDifferentConfirmationCount;
    return _differentMatchCount >= requiredCount
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

    const int completeStepCount =
        (capabilityTenths - minimumTenths) / stepTenths;
    *maximumZoom =
        (minimumTenths + completeStepCount * stepTenths) / 10.0;
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

    // 所有可发布停点都固定锚定在minimumZoom，并严格落在完整步长网格。
    // 分辨率能力上限只负责“不可越过”，不再作为非网格特殊停点。
    return (zoomTenths - minimumTenths) % stepTenths == 0;
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

    if (!isAlignedZoom(currentTenths / 10.0,
                       stepTenths / 10.0,
                       minimumZoom,
                       maximumZoom)) {
        const int offsetTenths = currentTenths - minimumTenths;
        const int candidateTenths = direction > 0
            ? minimumTenths
                + ((offsetTenths + stepTenths - 1) / stepTenths) * stepTenths
            : minimumTenths
                + (offsetTenths / stepTenths) * stepTenths;
        if (candidateTenths < minimumTenths
            || candidateTenths > maximumTenths
            || candidateTenths == currentTenths) {
            return false;
        }
        *targetZoom = candidateTenths / 10.0;
        return true;
    }

    const int candidateTenths =
        currentTenths + direction * stepTenths;

    if (candidateTenths < minimumTenths
        || candidateTenths > maximumTenths
        || candidateTenths == currentTenths) {
        return false;
    }

    *targetZoom = candidateTenths / 10.0;
    return true;
}
