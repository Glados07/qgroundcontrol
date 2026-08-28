/****************************************************************************
 *
 * Android video decoder selection policy.
 *
 ****************************************************************************/

#pragma once

#include <QtCore/QStringList>

class AndroidVideoDecoderPolicy
{
public:
    /// Require compatible vendor MediaCodec decoders for Android H.264/H.265.
    /// H.265 reuses the A8 Mini hvc1-to-Annex-B adapter before bounded
    /// receiver-specific adapter/direct retry routes. This must run after
    /// GStreamer initialization and before either decodebin3 is created.
    static void apply(bool forceHardwareDecoding);

    /// Ordered receiver-specific H.265 hardware retry factories. Alternative
    /// A8-style Annex-B adapters precede direct hvc1 MediaCodec factories.
    static QStringList hardwareRetryFactoryNames();
};
