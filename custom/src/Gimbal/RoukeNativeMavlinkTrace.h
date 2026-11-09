/****************************************************************************
 * Header-only sampling state for observation of native-link Rouke traffic.
 * It neither stores messages nor participates in payload acknowledgement.
 ****************************************************************************/
#pragma once

#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <array>

class RoukeNativeMavlinkTrace
{
public:
    static constexpr int ChannelCount = 16;
    struct Sample {
        bool log = false;
        quint64 count = 0;
    };

    Sample observe(QObject *link, int channel, qint64 nowMs)
    {
        if (!link || channel < 0 || channel >= ChannelCount) return {};
        auto &state = _channels[static_cast<std::size_t>(channel)];
        // QPointer clears on destruction, including when a later QObject uses
        // the same address. Channel reuse must produce a fresh first sample.
        if (state.link != link) {
            state = {};
            state.link = link;
        }
        ++state.count;
        const bool log = !state.logged || nowMs - state.lastLogMs >= 1000;
        if (log) {
            state.logged = true;
            state.lastLogMs = nowMs;
        }
        return {log, state.count};
    }

    void reset() { _channels = {}; }

private:
    struct Channel {
        QPointer<QObject> link;
        quint64 count = 0;
        qint64 lastLogMs = 0;
        bool logged = false;
    };
    std::array<Channel, ChannelCount> _channels{};
};
