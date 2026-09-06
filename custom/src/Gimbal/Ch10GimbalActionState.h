/****************************************************************************
 *
 * Next-action state for the UniRC CH10 gimbal button.
 *
 ****************************************************************************/

#pragma once

#include <cstdint>

class Ch10GimbalActionState
{
public:
    enum class Action {
        Recenter,
        Pitch90,
    };

    Action nextAction() const { return _nextAction; }
    std::uint64_t revision() const { return _revision; }

    // An asynchronous ACK must not overwrite a newer manual/toolbar/reset
    // event, even if that event did not change the enum's value.
    bool commandAccepted(Action action, std::uint64_t requestRevision)
    {
        if (requestRevision != _revision) {
            return false;
        }
        return _setNextAction(action == Action::Recenter
                                 ? Action::Pitch90 : Action::Recenter);
    }

    bool recenterCommandDispatched() { return _setNextAction(Action::Pitch90); }
    bool pitch90CommandDispatched() { return _setNextAction(Action::Recenter); }
    bool yawLockCommandDispatched() { return _setNextAction(Action::Recenter); }
    bool manualAttitudeInputDetected() { return _setNextAction(Action::Recenter); }
    bool reset() { return _setNextAction(Action::Recenter); }

private:
    bool _setNextAction(Action action)
    {
        ++_revision;
        if (_nextAction == action) {
            return false;
        }
        _nextAction = action;
        return true;
    }

    Action _nextAction = Action::Recenter;
    std::uint64_t _revision = 0;
};
