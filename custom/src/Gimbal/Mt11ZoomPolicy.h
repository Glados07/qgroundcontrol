/****************************************************************************
 *
 * UniPod MT11 display-target and gesture zoom policy.
 *
 ****************************************************************************/

#pragma once

class Mt11ZoomPolicy
{
public:
    static constexpr double MinimumZoom = 1.0;
    static constexpr double AbsoluteCommandMaximumZoom = 30.0;

    // MT11 never publishes the device's off-grid physical endpoint. Unlike
    // A8 recording-resolution limits, its display range ends at the last full
    // minimum-anchored step at or below deviceMaximumZoom.
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

    // Select the legal display origin for a native held gesture. When a tap
    // target is still being confirmed, that newer operator target must remain
    // visible instead of being rolled back to an older measured sample during
    // the 0x0f -> 0x05 handoff. An idle hold starts from measured feedback.
    static bool holdStartDisplayTarget(double measuredZoom,
                                       double displayZoom,
                                       double zoomStep,
                                       double deviceMaximumZoom,
                                       int direction,
                                       bool absoluteCommandPending,
                                       double* displayTarget);

    // Report whether native held motion is meaningful in direction. While an
    // absolute tap is still moving, either its published target or the latest
    // measured position may make the takeover direction feasible.
    static bool holdDirectionAvailable(double measuredZoom,
                                       double displayZoom,
                                       double zoomStep,
                                       double deviceMaximumZoom,
                                       int direction,
                                       bool absoluteCommandPending);

    // Advance an existing legal display target only across grid stops which
    // measuredZoom has reached or passed in direction. Valid input returns the
    // resulting legal target even when no new stop has been reached.
    static bool heldProgressTarget(double displayZoom,
                                   double measuredZoom,
                                   double zoomStep,
                                   double deviceMaximumZoom,
                                   int direction,
                                   double* displayTarget);
};
