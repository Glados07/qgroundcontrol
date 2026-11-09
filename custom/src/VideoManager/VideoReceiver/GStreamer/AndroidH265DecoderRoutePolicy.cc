/****************************************************************************
 *
 * Pure ordering and progression policy for Android H.265 hardware routes.
 *
 ****************************************************************************/

#include "AndroidH265DecoderRoutePolicy.h"

#include <QtCore/QUrl>

namespace {

void appendUniqueFactories(QStringList &destination,
                           const QStringList &factories)
{
    for (const QString &factory : factories) {
        if (!factory.isEmpty() && !destination.contains(factory)) {
            destination.append(factory);
        }
    }
}

} // namespace

QStringList AndroidH265DecoderRoutePolicy::orderedRetryFactories(
    const QStringList &alternativeAdapterFactories,
    const QStringList &directFactories)
{
    QStringList orderedFactories;
    orderedFactories.reserve(alternativeAdapterFactories.size()
                             + directFactories.size());
    appendUniqueFactories(orderedFactories, alternativeAdapterFactories);
    appendUniqueFactories(orderedFactories, directFactories);
    return orderedFactories;
}

AndroidH265DecoderRoutePolicy::RouteSelection
AndroidH265DecoderRoutePolicy::nextRoute(
    const QStringList &orderedFactories,
    const QString &previousFactory,
    int previousCandidateIndex)
{
    // Route indices are persisted as int-valued QObject properties. Qt 6
    // changed container sizes and indices to qsizetype, so normalize the
    // small decoder-factory list explicitly at this boundary.
    const int factoryCount = static_cast<int>(orderedFactories.size());
    int currentIndex = previousCandidateIndex;
    if (!previousFactory.isEmpty()) {
        const int matchingIndex =
            static_cast<int>(orderedFactories.indexOf(previousFactory));
        if (matchingIndex >= 0) {
            currentIndex = matchingIndex;
        }
    }

    const int nextIndex = currentIndex + 1;
    if (nextIndex >= 0 && nextIndex < factoryCount) {
        return RouteSelection{orderedFactories.at(nextIndex), nextIndex, false};
    }

    return RouteSelection{QString(), factoryCount, true};
}

bool AndroidH265DecoderRoutePolicy::canRetryCapsOnlyTimeout(
    const QString &currentUri,
    const QString &pipelineUri,
    quint64 generation,
    bool h265Confirmed,
    bool nativeByteStream,
    bool sourceFrameReceived,
    bool decoderFrameReceived,
    bool sinkFrameReceived)
{
    const QString scheme = QUrl(pipelineUri).scheme();
    const bool rtsp = scheme.compare(QStringLiteral("rtsp"), Qt::CaseInsensitive) == 0
        || scheme.compare(QStringLiteral("rtsps"), Qt::CaseInsensitive) == 0;
    return generation != 0 && !pipelineUri.isEmpty()
        && currentUri == pipelineUri && rtsp && h265Confirmed
        && !nativeByteStream && !sourceFrameReceived
        && !decoderFrameReceived && !sinkFrameReceived;
}
