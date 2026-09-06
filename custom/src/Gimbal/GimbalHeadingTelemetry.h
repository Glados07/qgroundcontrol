/****************************************************************************
 *
 * Unrounded flight-controller heading samples for gimbal frame conversion.
 *
 ****************************************************************************/

#pragma once

#include <array>
#include <cstdint>
#include <optional>

class GimbalHeadingTelemetry {
   public:
    enum class Source { None, Quaternion, Attitude, HighLatency };

    struct Sample {
        bool valid = false;
        double yawDegrees = 0.0;
        Source source = Source::None;
        std::int64_t receivedAtMs = -1;
        std::int64_t ageMs = -1;
    };

    // Call only for decoded, physical flight-controller heading. Quaternion
    // display offsets must not be applied. Time and value are accepted together;
    // invalid or out-of-order packets cannot renew a previous sample's lifetime.
    // High-latency messages without a common boot clock omit timeBootMs.
    bool update(Source source, double yawDegrees, std::int64_t receivedAtMs,
                std::optional<std::uint32_t> timeBootMs = std::nullopt);

    // Use the newest physical ATTITUDE / ATTITUDE_QUATERNION measurement;
    // prefer the quaternion only on a tie. High-latency heading is a fallback.
    // Each source expires independently after 2000 ms.
    [[nodiscard]] Sample heading(std::int64_t nowMs) const;
    void clear();

   private:
    struct Entry {
        Sample sample;
        std::optional<std::uint32_t> timeBootMs;
    };

    std::array<Entry, 3> _entries{};
};
