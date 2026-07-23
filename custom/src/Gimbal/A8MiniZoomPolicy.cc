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
    if (width == 3840 && height == 2160) {
        // A8 Mini 在4K视频模式下不支持数字变焦。
        resolvedMaximum = 1.0;
    } else if (width == 2560 && height == 1440) {
        resolvedMaximum = 3.5;
    } else if (width == 1920 && height == 1080) {
        resolvedMaximum = 5.5;
    } else if (width == 1280 && height == 720) {
        resolvedMaximum = 6.0;
    } else {
        return false;
    }

    *maximumZoom = resolvedMaximum;
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

    // 0x0f 仅支持一位小数。先按协议精度计算，再检查完整步长是否仍在边界内；
    // 不能先 qBound，否则配置 1.0x 时会在边界静默变成 0.5x。
    const double normalizedCurrent = qRound(currentZoom * 10.0) / 10.0;
    const double normalizedStep = qRound(zoomStep * 10.0) / 10.0;
    const double candidate = qRound((normalizedCurrent + direction * normalizedStep) * 10.0) / 10.0;
    if (candidate < minimumZoom - kComparisonTolerance
        || candidate > maximumZoom + kComparisonTolerance) {
        return false;
    }

    const double boundedCandidate = qBound(minimumZoom, candidate, maximumZoom);
    if (qAbs(boundedCandidate - normalizedCurrent) < kComparisonTolerance) {
        return false;
    }

    *targetZoom = boundedCandidate;
    return true;
}
