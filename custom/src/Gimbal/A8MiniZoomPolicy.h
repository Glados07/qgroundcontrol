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
    enum class TargetObservation {
        Waiting,
        TargetReached,
        StableDifferent,
    };

    class TargetTracker
    {
    public:
        void reset(double targetZoom);
        void clear();
        TargetObservation observe(double actualZoom);

        double targetZoom() const { return _targetZoom; }
        double stableDifferentZoom() const { return _differentCandidate; }

    private:
        double _targetZoom = 1.0;
        double _differentCandidate = 1.0;
        int _targetMatchCount = 0;
        int _differentMatchCount = 0;
        bool _differentCandidateValid = false;
        bool _active = false;
    };

    static bool maximumZoomForVideoResolution(quint16 width,
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
    static bool feedbackReachesTarget(double actualZoom,
                                      double sourceZoom,
                                      double targetZoom);
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
};
