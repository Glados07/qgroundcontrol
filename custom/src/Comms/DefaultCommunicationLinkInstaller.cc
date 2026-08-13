/****************************************************************************
 *
 * Custom default communication-link installer.
 * Reconciles QGC's standard QSettings representation before LinkManager
 * loads it; LinkManager continues to own connection and persistence.
 *
 ****************************************************************************/

#include "DefaultCommunicationLinkInstaller.h"

#include "LinkConfiguration.h"

#include <QtCore/QLoggingCategory>
#include <QtCore/QSettings>
#include <QtCore/QString>
#include <QtCore/QVariant>
#include <QtCore/QtGlobal>

Q_LOGGING_CATEGORY(DefaultCommunicationLinkLog, "gcs.custom.communicationlink")

namespace {

const QString kDefaultLinkName = QStringLiteral("local");
const QString kDefaultHost = QStringLiteral("192.168.144.125");
constexpr quint16 kDefaultRemotePort = 14550;

// Use a stable source port by default for Ethernet MAVLink paths whose peer
// keeps a learned GCS UDP endpoint. This is only the initial value: once the
// link exists, later user edits are preserved.
constexpr quint16 kDefaultLocalPort = 14550;

void writeDefaultLink(QSettings& settings, const QString& linkRoot)
{
    // count=0 means Link0 is not active, but a stale group can remain after
    // interrupted persistence. Clear that target slot before installing the
    // complete default so unknown old keys cannot leak into it.
    settings.remove(linkRoot);
    settings.setValue(linkRoot + QStringLiteral("/name"), kDefaultLinkName);
    settings.setValue(
        linkRoot + QStringLiteral("/type"),
        static_cast<int>(LinkConfiguration::TypeUdp));
    settings.setValue(linkRoot + QStringLiteral("/auto"), false);
    settings.setValue(linkRoot + QStringLiteral("/high_latency"), false);
    settings.setValue(linkRoot + QStringLiteral("/port"), kDefaultLocalPort);
    settings.setValue(linkRoot + QStringLiteral("/hostCount"), 1);
    settings.setValue(linkRoot + QStringLiteral("/host0"), kDefaultHost);
    settings.setValue(linkRoot + QStringLiteral("/port0"), kDefaultRemotePort);
}

} // namespace

void DefaultCommunicationLinkInstaller::ensureInstalled()
{
    QSettings settings;
    const QString settingsRoot = LinkConfiguration::settingsRoot();
    const QString countKey = settingsRoot + QStringLiteral("/count");
    bool countIsValid = false;
    const int linkCount = settings.value(countKey, 0).toInt(&countIsValid);

    if (!countIsValid) {
        qCWarning(DefaultCommunicationLinkLog)
            << "Invalid stored communication-link count; leaving all "
               "configurations unchanged";
        return;
    }

    if (linkCount != 0) {
        qCDebug(DefaultCommunicationLinkLog)
            << "Stored communication-link count is non-zero; leaving all "
               "configurations unchanged. count="
            << linkCount;
        return;
    }

    const QString linkRoot = settingsRoot + QStringLiteral("/Link0");
    writeDefaultLink(settings, linkRoot);
    settings.setValue(countKey, 1);
    settings.sync();

    if (settings.status() != QSettings::NoError) {
        qCWarning(DefaultCommunicationLinkLog)
            << "Failed to persist the default communication link";
        return;
    }

    qCInfo(DefaultCommunicationLinkLog)
        << "Installed default UDP communication link"
        << kDefaultLinkName
        << "local port" << kDefaultLocalPort
        << "remote"
        << QStringLiteral("%1:%2")
               .arg(kDefaultHost)
               .arg(kDefaultRemotePort);
}
