/****************************************************************************
 *
 * Unrounded flight-controller heading samples for gimbal frame conversion.
 *
 ****************************************************************************/

#include "GimbalHeadingTelemetry.h"

#include <cmath>

namespace {

constexpr std::int64_t kMaximumAgeMs = 2000;
constexpr std::uint32_t kRestartRollbackMs = 1000;

int sourceIndex(GimbalHeadingTelemetry::Source source) {
    switch (source) {
        case GimbalHeadingTelemetry::Source::Quaternion:
            return 0;
        case GimbalHeadingTelemetry::Source::Attitude:
            return 1;
        case GimbalHeadingTelemetry::Source::HighLatency:
            return 2;
        case GimbalHeadingTelemetry::Source::None:
            return -1;
    }
    return -1;
}

}  // namespace

bool GimbalHeadingTelemetry::update(Source source, double yawDegrees, std::int64_t receivedAtMs,
                                    std::optional<std::uint32_t> timeBootMs) {
    const int index = sourceIndex(source);
    if (index < 0 || !std::isfinite(yawDegrees) || receivedAtMs < 0) {
        return false;
    }

    Entry &entry = _entries[static_cast<std::size_t>(index)];
    if (entry.sample.valid && receivedAtMs < entry.sample.receivedAtMs) {
        return false;
    }

    if (entry.sample.valid && entry.timeBootMs && timeBootMs) {
        const std::uint32_t forward = *timeBootMs - *entry.timeBootMs;
        if (forward == 0) {
            return false;
        }
        if (forward >= 0x80000000U) {
            const std::uint32_t rollback = *entry.timeBootMs - *timeBootMs;
            if (rollback <= kRestartRollbackMs) {
                return false;
            }
            // A substantial rollback means that samples from the previous
            // flight-controller boot must not win source priority. Natural
            // uint32 wrapping is forward progress and does not enter here.
            clear();
        }
    }

    double normalizedYaw = std::fmod(yawDegrees, 360.0);
    if (normalizedYaw < 0.0) {
        normalizedYaw += 360.0;
    }
    entry.sample = {true, normalizedYaw, source, receivedAtMs, 0};
    entry.timeBootMs = timeBootMs;
    return true;
}

GimbalHeadingTelemetry::Sample GimbalHeadingTelemetry::heading(std::int64_t nowMs) const {
    const Entry *selected = nullptr;
    for (const Entry &entry : _entries) {
        if (entry.sample.valid && nowMs >= entry.sample.receivedAtMs &&
            (nowMs - entry.sample.receivedAtMs) <= kMaximumAgeMs) {
            if (!selected) {
                selected = &entry;
            } else if (entry.sample.source != Source::HighLatency) {
                // Both high-resolution messages are emitted by the same FC
                // and share a boot clock. Arrival order need not be sample
                // order. A low-rate quaternion must not suppress newer Euler
                // headings for the entire freshness window.
                const bool comparable = entry.timeBootMs && selected->timeBootMs;
                const std::uint32_t forward = comparable ? *entry.timeBootMs - *selected->timeBootMs : 0U;
                const bool newer = comparable ? forward > 0U && forward < 0x80000000U
                                              : entry.sample.receivedAtMs > selected->sample.receivedAtMs;
                if (newer) {
                    selected = &entry;
                }
            }
        }
    }
    if (!selected) {
        return {};
    }
    Sample result = selected->sample;
    result.ageMs = nowMs - result.receivedAtMs;
    return result;
}

void GimbalHeadingTelemetry::clear() { _entries = {}; }
