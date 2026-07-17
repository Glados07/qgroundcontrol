/****************************************************************************
 *
 * Android H.265 视频解码策略。
 *
 ****************************************************************************/

#pragma once

class AndroidVideoDecoderPolicy
{
public:
    /// 在 Android 上优先使用兼容的厂商 MediaCodec H.265 硬件解码器。
    /// 必须在 GStreamer 初始化完成、VideoReceiver 创建之前调用。
    static void apply(bool forceHardwareDecoding);
};
