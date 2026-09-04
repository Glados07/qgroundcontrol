/****************************************************************************
 *
 * Pure ordering and progression policy for Android H.265 hardware routes.
 *
 ****************************************************************************/

#pragma once

#include <QtCore/QString>
#include <QtCore/QStringList>

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
};
