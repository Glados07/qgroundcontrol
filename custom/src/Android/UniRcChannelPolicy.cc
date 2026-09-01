/****************************************************************************
 *
 * Safety state machine for UniRC CH9 zoom and CH10 gimbal-centre input.
 *
 ****************************************************************************/

#include "UniRcChannelPolicy.h"

UniRcChannelPolicy::Result UniRcChannelPolicy::update(qint16 channel9,
                                                      qint16 channel10,
                                                      bool zoomDirectionReversed)
{
    if (channel9 < MinimumReasonableValue
        || channel9 > MaximumReasonableValue
        || channel10 < MinimumReasonableValue
        || channel10 > MaximumReasonableValue) {
        return linkLost();
    }

    Result result;
    result.channelsValid = true;

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
        result.centerRequested = true;
    }

    return result;
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
