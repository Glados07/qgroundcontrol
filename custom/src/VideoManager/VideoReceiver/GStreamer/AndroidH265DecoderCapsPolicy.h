/****************************************************************************
 *
 * Android H.265 decoder-facing caps policy.
 *
 ****************************************************************************/

#pragma once

class AndroidH265DecoderCapsPolicy final
{
public:
    /// Annex-B/AU contract used between h265parse and Android MediaCodec.
    /// The framerate remains a range so a known A8 rate is preserved while
    /// streams without an advertised rate can negotiate the 0/1 sentinel.
    static const char *byteStreamAccessUnitCaps();
};
