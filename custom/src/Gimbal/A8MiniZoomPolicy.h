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
    static constexpr qint64 kMinimumHeldZoomElapsedMs = 420;
    static constexpr qint64 kDefaultHeldZoomStepPeriodMs = 600;

    enum class TargetObservation {
        Waiting,
        TargetReached,
    };

    class TargetTracker
    {
    public:
        void reset(double targetZoom);
        void clear();
        TargetObservation observe(double actualZoom);

        double targetZoom() const { return _targetZoom; }

    private:
        double _targetZoom = 1.0;
        int _targetMatchCount = 0;
        bool _active = false;
    };

    static bool isSupportedPulledVideoResolution(quint16 width,
                                                  quint16 height);
    static bool maximumZoomForRecordingResolution(quint16 width,
                                                   quint16 height,
                                                   double* maximumZoom);
    static bool alignedMaximumZoom(double capabilityMaximumZoom,
                                   double zoomStep,
                                   double minimumZoom,
                                   double* maximumZoom);
    static bool isAlignedZoom(double zoomLevel,
                              double zoomStep,
                              double minimumZoom,
                              double maximumZoom);
    static bool feedbackReachedStop(double actualZoom,
                                    double targetZoom,
                                    int direction);
    static bool exactDirectionalProgressStop(double currentZoom,
                                             double targetZoom,
                                             double actualZoom,
                                             double zoomStep,
                                             double minimumZoom,
                                             double maximumZoom,
                                             double* progressZoom);
    static bool terminalHandoffStop(double zoomStep,
                                    double minimumZoom,
                                    double maximumZoom,
                                    int direction,
                                    double* handoffZoom);
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
    static bool heldTarget(double startZoom,
                           int direction,
                           qint64 elapsedMs,
                           double zoomStep,
                           double minimumZoom,
                           double maximumZoom,
                           double* targetZoom);
    static bool heldTarget(double startZoom,
                           int direction,
                           qint64 elapsedMs,
                           double zoomStep,
                           double minimumZoom,
                           double maximumZoom,
                           qint64 stepPeriodMs,
                           double* targetZoom);
};
