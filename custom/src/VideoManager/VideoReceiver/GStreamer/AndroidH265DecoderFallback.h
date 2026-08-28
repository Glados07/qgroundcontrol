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

    /// Synchronize the bounded retry namespace after custom changes the
    /// receiver's generation-scoped H.265 input packetization.
    static void resetForCurrentInputFormat(VideoReceiver *receiver);

    /// Adapter factory active for this receiver/URI, including an explicitly
    /// selected alternative normalization-adapter route. Empty means a direct
    /// MediaCodec route or no matching active route.
    static QString activeAdapterFactoryName(VideoReceiver *receiver,
                                            const QString &uri);

    /// Advance this receiver/URI to its next compatible vendor MediaCodec
    /// route. Alternative normalization adapters and packetization-compatible
    /// direct factories are each tried once; after the last one fails, the
    /// route returns to the preferred adapter without enabling software
    /// decoding. Returns true on a change.
    static bool prepareHardwareRetry(VideoReceiver *receiver,
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
