/****************************************************************************
 *
 * Per-URI Android H.265 parser output policy.
 *
 ****************************************************************************/

#include "AndroidH265StreamFormatPolicy.h"

#include <QtCore/QUrl>

namespace {

constexpr const char *kParserOutputFormatProperty =
    "customAndroidH265ParserOutputFormat";

QString normalizedHost(QString host)
{
    host = host.trimmed();
    if (host.size() >= 2 && host.front() == QLatin1Char('[')
        && host.back() == QLatin1Char(']')) {
        host = host.mid(1, host.size() - 2);
    }
    return host.toLower();
}

} // namespace

const char *AndroidH265StreamFormatPolicy::receiverPropertyName()
{
    return kParserOutputFormatProperty;
}

QString AndroidH265StreamFormatPolicy::parserOutputFormatForUri(
    const QString &uri,
    const QString &mt11Host)
{
    const QUrl streamUrl(uri.trimmed());
    const QString scheme = streamUrl.scheme().toLower();
    if (!streamUrl.isValid()
        || (scheme != QStringLiteral("rtsp")
            && scheme != QStringLiteral("rtsps"))) {
        return {};
    }

    const QString streamHost = normalizedHost(streamUrl.host());
    const QString configuredMt11Host = normalizedHost(mt11Host);
    if (streamHost.isEmpty() || configuredMt11Host.isEmpty()
        || streamHost != configuredMt11Host) {
        return {};
    }

    return QStringLiteral("byte-stream");
}
