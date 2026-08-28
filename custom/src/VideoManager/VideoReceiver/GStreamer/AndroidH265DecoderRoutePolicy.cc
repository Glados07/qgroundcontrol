/****************************************************************************
 *
 * Pure ordering and progression policy for Android H.265 hardware routes.
 *
 ****************************************************************************/

#include "AndroidH265DecoderRoutePolicy.h"

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
    const QStringList &directHvc1Factories)
{
    QStringList orderedFactories;
    orderedFactories.reserve(alternativeAdapterFactories.size()
                             + directHvc1Factories.size());
    appendUniqueFactories(orderedFactories, alternativeAdapterFactories);
    appendUniqueFactories(orderedFactories, directHvc1Factories);
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
