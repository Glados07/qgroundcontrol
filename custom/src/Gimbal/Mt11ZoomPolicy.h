/****************************************************************************
 *
 * UniPod MT11 display-target and tap zoom policy.
 *
 ****************************************************************************/

#pragma once

class Mt11ZoomPolicy
{
public:
    static constexpr double MinimumZoom = 1.0;
    static constexpr double AbsoluteCommandMaximumZoom = 30.0;

    // MT11 never publishes the device's off-grid physical endpoint. The exact
    // 30x absolute-command boundary is nevertheless a legal terminal stop
    // even when the configured minimum-anchored step does not land on it.
    static bool isDisplayTarget(double zoomLevel,
                                double zoomStep,
                                double deviceMaximumZoom);

    // Resolve one absolute-command tap. measuredZoom owns the >30x protocol
    // gate, while displayZoom is the already-legal grid planning reference.
    static bool tapTarget(double measuredZoom,
                          double displayZoom,
                          double zoomStep,
                          double deviceMaximumZoom,
                          int direction,
                          double* targetZoom);

    // Convert a raw device observation to the nearest legal display target.
    // preferredDirection resolves an exact midpoint: -1 lower, 0/1 upper.
    static bool alignedDisplayTarget(double measuredZoom,
                                     double zoomStep,
                                     double deviceMaximumZoom,
                                     int preferredDirection,
                                     double* displayTarget);
};
