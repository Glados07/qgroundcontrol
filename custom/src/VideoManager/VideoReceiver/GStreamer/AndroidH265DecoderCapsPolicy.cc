/****************************************************************************
 *
 * Android H.265 decoder-facing caps policy.
 *
 ****************************************************************************/

#include "AndroidH265DecoderCapsPolicy.h"

namespace {

constexpr const char *kByteStreamAccessUnitCaps =
    "video/x-h265,stream-format=(string)byte-stream,alignment=(string)au,"
    "parsed=(boolean)true,framerate=(fraction)[0/1,2147483647/1]";

} // namespace

const char *AndroidH265DecoderCapsPolicy::byteStreamAccessUnitCaps()
{
    return kByteStreamAccessUnitCaps;
}
