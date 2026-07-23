/****************************************************************************
 *
 * 思翼 A8 Mini 视频分辨率与缩放策略。
 *
 ****************************************************************************/

#pragma once

#include <QtCore/QtGlobal>

class A8MiniZoomPolicy
{
public:
    static bool maximumZoomForVideoResolution(quint16 width,
                                               quint16 height,
                                               double* maximumZoom);
    static bool stepTarget(double currentZoom,
                           double zoomStep,
                           double minimumZoom,
                           double maximumZoom,
                           int direction,
                           double* targetZoom);
};
