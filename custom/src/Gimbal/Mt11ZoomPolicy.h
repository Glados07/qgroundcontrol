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

    // Report whether a held gesture has a legal first command in direction.
    // This uses the same pending-target takeover rule as
    // heldGestureStepTarget(), including at the minimum and display maximum.
    static bool holdDirectionAvailable(double measuredZoom,
                                       double displayZoom,
                                       double zoomStep,
                                       double deviceMaximumZoom,
                                       int direction,
                                       bool absoluteCommandPending);

    // Resolve exactly one held-gesture target over the complete MT11 display
    // range. The manager chooses 0x0f for targets at/below 30x and a bounded
    // feedback-controlled 0x05 pulse above it.
    static bool heldStepTarget(double displayZoom,
                               double zoomStep,
                               double deviceMaximumZoom,
                               int direction,
                               double* targetZoom);

    // Resolve the first command target when a held gesture takes ownership.
    // A same-direction absolute target which is still physically in flight is
    // returned unchanged so the manager can retransmit it once, instead of
    // skipping a configured step. Opposite-direction takeover moves by one
    // legal display step immediately.
    static bool heldGestureStepTarget(double measuredZoom,
                                      double displayZoom,
                                      double zoomStep,
                                      double deviceMaximumZoom,
                                      int direction,
                                      bool absoluteCommandPending,
                                      double* targetZoom);

    // Keep approximately the same target-rate for ordinary 1x/2x steps:
    // 1x per 600 ms and 2x per 1200 ms. Extreme settings are bounded so a
    // held gesture remains responsive and does not create a tight packet loop.
    static int heldStepCadenceMs(double zoomStep);

    // True once authoritative 0x18 feedback reaches or passes this step's
    // target in the commanded direction.
    static bool heldStepTargetReached(double measuredZoom,
                                      double targetZoom,
                                      int direction);
};
