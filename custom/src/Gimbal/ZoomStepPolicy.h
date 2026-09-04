/****************************************************************************
 *
 * Shared fixed-step zoom-grid policy.
 *
 ****************************************************************************/

#pragma once

class ZoomStepPolicy
{
public:
    // A legal grid is anchored at minimumZoom. The exact maximumZoom is also
    // a legal terminal stop when the last interval is shorter than zoomStep.
    static bool isAlignedZoom(double zoomLevel,
                              double zoomStep,
                              double minimumZoom,
                              double maximumZoom);
    static bool alignmentTarget(double currentZoom,
                                double zoomStep,
                                double minimumZoom,
                                double maximumZoom,
                                int direction,
                                double* targetZoom);
    static bool stepTarget(double currentZoom,
                           double zoomStep,
                           double minimumZoom,
                           double maximumZoom,
                           int direction,
                           double* targetZoom);
};
