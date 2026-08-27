/****************************************************************************
 *
 * Safety state machine for UniRC CH9 zoom and CH10 gimbal-centre input.
 *
 ****************************************************************************/

#pragma once

#include <QtCore/QtGlobal>

class UniRcChannelPolicy
{
public:
    static constexpr qint16 MinimumReasonableValue = 900;
    static constexpr qint16 MaximumReasonableValue = 2100;
    static constexpr qint16 ZoomOutThreshold = 1475;
    static constexpr qint16 ZoomInThreshold = 1525;
    static constexpr qint16 ButtonReleasedThreshold = 1250;
    static constexpr qint16 ButtonPressedThreshold = 1750;

    struct Result {
        bool channelsValid = false;
        bool zoomDirectionChanged = false;
        int zoomDirection = 0;
        bool centerRequested = false;
    };

    Result update(qint16 channel9, qint16 channel10);
    Result linkLost();
    void reset();

    int zoomDirection() const { return _zoomDirection; }

private:
    bool _zoomArmed = false;
    bool _buttonArmed = false;
    bool _buttonPressed = false;
    int _zoomDirection = 0;
};
