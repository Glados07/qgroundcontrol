/****************************************************************************
 * Vendor gimbal_speed_ctrl encoding: signed steps of 35, zero stops the axis.
 ****************************************************************************/
#pragma once

namespace RoukeGimbalSpeed {
constexpr int Step = 35;
constexpr int Maximum = 525;
constexpr int DefaultYaw = 350;
constexpr int DefaultPitch = 350;

constexpr bool isValid(int value)
{
    return value == 0 || (value >= Step && value <= Maximum)
        || (value <= -Step && value >= -Maximum);
}

// Integer division truncates toward zero for both directions.
constexpr int quantize(int value) { return value / Step * Step; }

// A limit below one effective step cannot drive the axis. Use its default.
constexpr int normalizeLimit(int value, int fallback)
{
    return value < Step ? fallback : quantize(value > Maximum ? Maximum : value);
}
} // namespace RoukeGimbalSpeed
