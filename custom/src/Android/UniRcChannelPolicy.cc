/****************************************************************************
 *
 * Safety state machine for UniRC gimbal-related channel input.
 *
 ****************************************************************************/

#include "UniRcChannelPolicy.h"

UniRcChannelPolicy::Result UniRcChannelPolicy::update(qint16 channel7,
                                                      qint16 channel8,
                                                      qint16 channel9,
                                                      qint16 channel10,
                                                      bool zoomDirectionReversed)
{
    if (!_isReasonableValue(channel9) || !_isReasonableValue(channel10)) {
        return linkLost();
    }

    Result result;
    result.channelsValid = true;
    result.manualAttitudeInputDetected =
        isManualAttitudeInput(channel7) || isManualAttitudeInput(channel8);

    int nextZoomDirection = 0;
    const bool zoomIsNeutral = channel9 >= ZoomOutThreshold
        && channel9 <= ZoomInThreshold;
    if (!_zoomArmed) {
        if (zoomIsNeutral) {
            _zoomArmed = true;
        }
    } else if (channel9 < ZoomOutThreshold) {
        nextZoomDirection = -1;
    } else if (channel9 > ZoomInThreshold) {
        nextZoomDirection = 1;
    }
    if (zoomDirectionReversed) {
        nextZoomDirection = -nextZoomDirection;
    }

    result.zoomDirectionChanged = nextZoomDirection != _zoomDirection;
    _zoomDirection = nextZoomDirection;
    result.zoomDirection = _zoomDirection;

    const bool buttonIsReleased = channel10 <= ButtonReleasedThreshold;
    const bool buttonIsPressed = channel10 >= ButtonPressedThreshold;
    if (!_buttonArmed) {
        if (buttonIsReleased) {
            _buttonArmed = true;
            _buttonPressed = false;
        }
    } else if (buttonIsReleased) {
        _buttonPressed = false;
    } else if (buttonIsPressed && !_buttonPressed) {
        _buttonPressed = true;
        result.ch10Pressed = true;
    }

    return result;
}

bool UniRcChannelPolicy::isManualAttitudeInput(qint16 value)
{
    return _isReasonableValue(value)
           && (value < ManualAttitudeNeutralMinimum
               || value > ManualAttitudeNeutralMaximum);
}

bool UniRcChannelPolicy::_isReasonableValue(qint16 value)
{
    return value >= MinimumReasonableValue
           && value <= MaximumReasonableValue;
}

UniRcChannelPolicy::Result UniRcChannelPolicy::linkLost()
{
    Result result;
    result.zoomDirectionChanged = _zoomDirection != 0;
    reset();
    return result;
}

void UniRcChannelPolicy::reset()
{
    _zoomArmed = false;
    _buttonArmed = false;
    _buttonPressed = false;
    _zoomDirection = 0;
}
