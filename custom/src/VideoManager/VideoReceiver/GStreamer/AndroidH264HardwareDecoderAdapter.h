/****************************************************************************
 * Normalizes packetized H.264 for Android vendor MediaCodec decoders.
 ****************************************************************************/
#pragma once

class AndroidH264HardwareDecoderAdapter
{
public:
    /// Register before decodebin creation. Never selects a software decoder.
    static bool registerElement();
    static const char *elementFactoryName();
};
