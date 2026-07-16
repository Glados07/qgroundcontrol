/****************************************************************************
 *
 * 思翼 A8 Mini 视频流适配。
 * 该类仅处理 custom 云台模块需要的视频默认值和 MAVLink 自动流过滤，
 * 不修改 QGC 原生 VideoManager。
 *
 ****************************************************************************/

#pragma once

class GimbalControlSettings;
typedef struct __mavlink_message mavlink_message_t;

class GimbalVideoStreamSupport
{
public:
    static void installA8MiniDefaults();
    static bool shouldFilterMavlinkMessage(GimbalControlSettings* settings, const mavlink_message_t& message);
};
