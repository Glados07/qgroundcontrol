/****************************************************************************
 *
 * Android H.265 video decoder selection policy.
 *
 ****************************************************************************/

#pragma once

class AndroidVideoDecoderPolicy
{
public:
    /// Prefer a vendor MediaCodec decoder for Android H.265. This must run
    /// after GStreamer initialization and before decodebin3 creates its pipeline.
    static void apply(bool forceHardwareDecoding);
};
