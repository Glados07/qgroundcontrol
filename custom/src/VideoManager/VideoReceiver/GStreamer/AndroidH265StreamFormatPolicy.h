/****************************************************************************
 *
 * Per-URI Android H.265 parser output policy.
 *
 ****************************************************************************/

#pragma once

#include <QtCore/QString>

class AndroidH265StreamFormatPolicy final
{
public:
    /// Dynamic VideoReceiver property consumed by GstVideoReceiver when a
    /// pipeline generation is frozen.
    static const char *receiverPropertyName();

    /// The default product topology identifies MT11 when the RTSP host matches
    /// its configured SDK host, independent of Video 1/2 assignment. Returns
    /// "byte-stream" for that fast path and an empty string for the established
    /// QGC/A8 hvc1 route. Decoder recovery can also make a one-time
    /// hvc1-to-byte-stream switch when another H.265 stream reaches its
    /// source-watchdog or strict decoder-failure recovery gate.
    static QString parserOutputFormatForUri(const QString &uri,
                                            const QString &mt11Host);
};
