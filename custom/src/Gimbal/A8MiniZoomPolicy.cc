/****************************************************************************
 *
 * 思翼 A8 Mini 视频分辨率与缩放策略。
 *
 ****************************************************************************/

#include "A8MiniZoomPolicy.h"

#include <QtCore/QtMath>

namespace {

static constexpr double kComparisonTolerance = 0.051;

} // namespace

bool A8MiniZoomPolicy::maximumZoomForVideoResolution(quint16 width,
                                                     quint16 height,
                                                     double* maximumZoom)
{
    if (!maximumZoom) {
        return false;
    }

    double resolvedMaximum = 0.0;
    if (width == 2560 && height == 1440) {
        resolvedMaximum = 3.5;
    } else if (width == 1920 && height == 1080) {
        resolvedMaximum = 5.5;
    } else {
        return false;
    }

    *maximumZoom = resolvedMaximum;
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
    const int maximumTenths = qRound(maximumZoom * 10.0);
    if (stepTenths < 1) {
        return false;
    }

    // 中间倍率固定锚定在minimumZoom；真实分辨率上限始终是额外的合法停点。
    // 例如步长1.0时，1080P允许1/2/3/4/5/5.5，2K允许1/2/3/3.5。
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
    // 上限即使不落在固定步长网格上，也必须参与最近合法停点选择。
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

    // 单击和长按重复只能从固定分度点或真实上限出发。启动时或外部控制
    // 留下的1.8x等任意小数必须先由Manager对齐，不能直接计算成2.8x。
    const int currentTenths = qRound(currentZoom * 10.0);
    const int stepTenths = qRound(zoomStep * 10.0);
    const int minimumTenths = qRound(minimumZoom * 10.0);
    const int maximumTenths = qRound(maximumZoom * 10.0);
    if (stepTenths < 1
        || !isAlignedZoom(currentTenths / 10.0,
                          stepTenths / 10.0,
                          minimumZoom,
                          maximumZoom)) {
        return false;
    }

    int candidateTenths = currentTenths + direction * stepTenths;
    if (direction > 0) {
        if (currentTenths >= maximumTenths) {
            return false;
        }
        // 完整步长将越过上限时，最后一次操作精确吸附到真实能力边界。
        candidateTenths = qMin(candidateTenths, maximumTenths);
    } else {
        if (currentTenths <= minimumTenths) {
            return false;
        }
        const bool maximumIsOnGrid =
            (maximumTenths - minimumTenths) % stepTenths == 0;
        if (currentTenths == maximumTenths && !maximumIsOnGrid) {
            // 从小数上限缩小时先回到最高固定分度，不能以max-step制造偏移网格。
            const int highestGridIndex =
                (maximumTenths - minimumTenths) / stepTenths;
            candidateTenths = minimumTenths + highestGridIndex * stepTenths;
        }
    }

    if (candidateTenths < minimumTenths
        || candidateTenths > maximumTenths
        || candidateTenths == currentTenths) {
        return false;
    }

    *targetZoom = candidateTenths / 10.0;
    return true;
}
