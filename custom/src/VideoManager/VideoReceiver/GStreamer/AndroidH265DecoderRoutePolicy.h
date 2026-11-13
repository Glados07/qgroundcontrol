/****************************************************************************
 *
 * Pure ordering and progression policy for Android H.265 hardware routes.
 *
 ****************************************************************************/

#pragma once

#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QtGlobal>

class AndroidH265DecoderRoutePolicy final
{
public:
    struct RouteSelection {
        QString factoryName;
        int candidateIndex = -1;
        bool exhausted = false;
    };

    /// Annex-B adapters are tried before direct factories compatible with the
    /// selected packetization. Empty names and duplicates are discarded
    /// without changing relative order.
    static QStringList orderedRetryFactories(
        const QStringList &alternativeAdapterFactories,
        const QStringList &directFactories);

    /// Select the next route exactly once. An empty factory with exhausted=true
    /// means return to the preferred adapter as the final stable route.
    static RouteSelection nextRoute(const QStringList &orderedFactories,
                                    const QString &previousFactory,
                                    int previousCandidateIndex);

    /// A native source timeout can precede the first compressed buffer even
    /// after H.265 CAPS selected a decoder. Permit one packetization retry,
    /// never a decoder-factory rotation based only on missing network data.
    static bool canRetryCapsOnlyTimeout(const QString &currentUri,
                                        const QString &pipelineUri,
                                        quint64 generation,
                                        bool h265Confirmed,
                                        bool nativeByteStream,
                                        bool sourceFrameReceived,
                                        bool decoderFrameReceived,
                                        bool sinkFrameReceived);
};
