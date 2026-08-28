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
    int currentIndex = previousCandidateIndex;
    if (!previousFactory.isEmpty()) {
        const int matchingIndex = orderedFactories.indexOf(previousFactory);
        if (matchingIndex >= 0) {
            currentIndex = matchingIndex;
        }
    }

    const int nextIndex = currentIndex + 1;
    if (nextIndex >= 0 && nextIndex < orderedFactories.size()) {
        return RouteSelection{orderedFactories.at(nextIndex), nextIndex, false};
    }

    return RouteSelection{QString(), orderedFactories.size(), true};
}
