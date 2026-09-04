/****************************************************************************
 *
 * Safety state machine for UniRC gimbal-related channel input.
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
    static constexpr qint16 ManualAttitudeCenter = 1500;
    static constexpr qint16 ManualAttitudeDeadband = 100;
    static constexpr qint16 ManualAttitudeNeutralMinimum =
        ManualAttitudeCenter - ManualAttitudeDeadband;
    static constexpr qint16 ManualAttitudeNeutralMaximum =
        ManualAttitudeCenter + ManualAttitudeDeadband;

    struct Result {
        bool channelsValid = false;
        bool zoomDirectionChanged = false;
        int zoomDirection = 0;
        bool manualAttitudeInputDetected = false;
        bool ch10Pressed = false;
    };

    Result update(qint16 channel7,
                  qint16 channel8,
                  qint16 channel9,
                  qint16 channel10,
                  bool zoomDirectionReversed = false);
    Result linkLost();
    void reset();

    int zoomDirection() const { return _zoomDirection; }
    static bool isManualAttitudeInput(qint16 value);

private:
    static bool _isReasonableValue(qint16 value);

    bool _zoomArmed = false;
    bool _buttonArmed = false;
    bool _buttonPressed = false;
    int _zoomDirection = 0;
};
