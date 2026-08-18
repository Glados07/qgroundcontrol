/****************************************************************************
 *
 * Android video decoder selection policy.
 *
 ****************************************************************************/

#pragma once

class AndroidVideoDecoderPolicy
{
public:
    /// Prefer vendor MediaCodec decoders for Android H.264/H.265. This must
    /// run after GStreamer initialization and before decodebin3 creates its
    /// pipeline.
    static void apply(bool forceHardwareDecoding);
};
