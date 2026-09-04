/****************************************************************************
 *
 * Next-action state for the UniRC CH10 gimbal button.
 *
 ****************************************************************************/

#pragma once

class Ch10GimbalActionState
{
public:
    enum class Action {
        Recenter,
        Pitch90,
    };

    Action nextAction() const { return _nextAction; }

    bool recenterCommandDispatched() { return _setNextAction(Action::Pitch90); }
    bool pitch90CommandDispatched() { return _setNextAction(Action::Recenter); }
    bool yawLockCommandDispatched() { return _setNextAction(Action::Recenter); }
    bool manualAttitudeInputDetected() { return _setNextAction(Action::Recenter); }
    bool reset() { return _setNextAction(Action::Recenter); }

private:
    bool _setNextAction(Action action)
    {
        if (_nextAction == action) {
            return false;
        }
        _nextAction = action;
        return true;
    }

    Action _nextAction = Action::Recenter;
};
