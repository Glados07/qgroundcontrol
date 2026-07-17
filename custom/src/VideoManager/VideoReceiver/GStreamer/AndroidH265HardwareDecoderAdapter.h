/****************************************************************************
 *
 * Android H.265 MediaCodec adapter for QGC's hvc1 decoder input.
 *
 ****************************************************************************/

#pragma once

class AndroidH265HardwareDecoderAdapter
{
public:
    /// Registers a high-rank decoder bin which converts hvc1 to Annex-B before
    /// handing the stream to a vendor Android MediaCodec decoder.
    ///
    /// Returns false without changing the registry when no compatible vendor
    /// decoder can be constructed. This must run after GStreamer is initialized
    /// and before decodebin3 is created.
    static bool registerElement();

    static const char *elementFactoryName();
    static const char *selectedHardwareDecoderFactoryName();

    /// Shared factory-name filter used by both adapter selection and rank
    /// policy so known Android software MediaCodec wrappers are never logged
    /// or prioritized as vendor hardware.
    static bool isVendorHardwareDecoderFactoryName(const char *factoryName);
};
