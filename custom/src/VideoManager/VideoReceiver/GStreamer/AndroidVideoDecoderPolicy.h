/****************************************************************************
 *
 * Android video decoder selection policy.
 *
 ****************************************************************************/

#pragma once

class AndroidVideoDecoderPolicy
{
public:
    /// Require compatible vendor MediaCodec decoders for Android H.264/H.265.
    /// H.265 reuses the A8 Mini hvc1-to-Annex-B adapter before direct hvc1
    /// candidates. This must run after GStreamer initialization and before
    /// decodebin3 creates either video pipeline.
    static void apply(bool forceHardwareDecoding);
};
