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

    /// True while this receiver/URI is using the shared A8-compatible
    /// hvc1-to-Annex-B adapter rather than an explicit direct factory.
    static bool usesAdapterRoute(VideoReceiver *receiver,
                                 const QString &uri);

    /// Advance this receiver/URI to its next compatible vendor MediaCodec
    /// route. Direct factories are tried once in policy order; after the last
    /// one fails, the route returns to the shared adapter without enabling a
    /// software decoder. Returns true when the route changes.
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
    static bool canAdvanceHardwareRoute(VideoReceiver *receiver,
                                        const QString &uri,
                                        int videoCodec,
                                        bool adapterSelected,
                                        bool sinkFrameReceived);
};
