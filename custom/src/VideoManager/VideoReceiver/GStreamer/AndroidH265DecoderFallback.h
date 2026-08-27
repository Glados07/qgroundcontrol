/****************************************************************************
 *
 * Per-receiver Android H.265 hardware decoder fallback.
 *
 ****************************************************************************/

#pragma once

#include <QtCore/QString>
#include <QtCore/QtGlobal>

class VideoReceiver;

class AndroidH265DecoderFallback final
{
public:
    /// Track URI changes so an explicit decoder selected for one stream can
    /// never leak into a later stream on the same receiver.
    static void install(VideoReceiver *receiver);

    /// Freeze the direct vendor MediaCodec factory on this receiver for its
    /// next complete pipeline generation. Returns true when the route changes.
    static bool prepareDirectRetry(VideoReceiver *receiver,
                                   const QString &uri,
                                   quint64 generation,
                                   int videoCodec,
                                   bool adapterSelected,
                                   bool sourceFrameReceived,
                                   bool confirmedDecoderBranchFailure,
                                   bool decoderFrameReceived,
                                   bool sinkFrameReceived,
                                   const char *reason);

private:
    static bool canRetryDirect(VideoReceiver *receiver,
                               const QString &uri,
                               int videoCodec,
                               bool adapterSelected,
                               bool decoderFrameReceived,
                               bool sinkFrameReceived);
};
