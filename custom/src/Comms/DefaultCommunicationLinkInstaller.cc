/****************************************************************************
 *
 * Custom default communication-link installer.
 * Writes QGC's standard QSettings representation before LinkManager loads it;
 * LinkManager continues to own loading, editing, connecting, and persistence.
 *
 ****************************************************************************/

#include "DefaultCommunicationLinkInstaller.h"

#include "LinkConfiguration.h"

#include <QtCore/QLoggingCategory>
#include <QtCore/QSettings>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QtGlobal>

Q_LOGGING_CATEGORY(DefaultCommunicationLinkLog, "gcs.custom.communicationlink")

namespace {

struct DefaultUdpLink {
    QString name;
    QString host;
    quint16 remotePort;
};

const DefaultUdpLink kDefaultLinks[] = {
    {
        QStringLiteral("local"),
        QStringLiteral("192.168.144.125"),
        14550,
    },
    {
        QStringLiteral("testlocal"),
        QStringLiteral("192.168.144.20"),
        19856,
    },
};
const QString kPreviousLocalHost = QStringLiteral("192.168.144.20");
constexpr quint16 kPreviousLocalRemotePort = 19856;
constexpr quint16 kDefaultLocalPort = 0;

// Version 2 changes the managed local endpoint and adds testlocal. The marker
// prevents a user who later deliberately restores the old local endpoint from
// having that choice overwritten on every startup.
constexpr int kDefaultLinksVersion = 2;
const QString kDefaultLinksVersionKey =
    QStringLiteral("CustomCommunicationLinks/defaultsVersion");

QString findLinkRoot(QSettings& settings,
                     const QString& settingsRoot,
                     int linkCount,
                     const QString& linkName)
{
    for (int index = 0; index < linkCount; ++index) {
        const QString linkRoot =
            settingsRoot + QStringLiteral("/Link%1").arg(index);
        const QString existingName =
            settings.value(linkRoot + QStringLiteral("/name"))
                .toString()
                .trimmed();
        if (existingName.compare(linkName, Qt::CaseInsensitive) == 0) {
            return linkRoot;
        }
    }
    return QString();
}

void writeDefaultLink(QSettings& settings,
                      const QString& linkRoot,
                      const DefaultUdpLink& link)
{
    settings.setValue(linkRoot + QStringLiteral("/name"), link.name);
    settings.setValue(
        linkRoot + QStringLiteral("/type"),
        static_cast<int>(LinkConfiguration::TypeUdp));
    settings.setValue(linkRoot + QStringLiteral("/auto"), false);
    settings.setValue(linkRoot + QStringLiteral("/high_latency"), false);
    settings.setValue(linkRoot + QStringLiteral("/port"), kDefaultLocalPort);
    settings.setValue(linkRoot + QStringLiteral("/hostCount"), 1);
    settings.setValue(linkRoot + QStringLiteral("/host0"), link.host);
    settings.setValue(linkRoot + QStringLiteral("/port0"), link.remotePort);
}

bool isPreviousManagedLocalEndpoint(QSettings& settings,
                                    const QString& linkRoot)
{
    const QString nameKey = linkRoot + QStringLiteral("/name");
    const QString typeKey = linkRoot + QStringLiteral("/type");
    const QString autoKey = linkRoot + QStringLiteral("/auto");
    const QString highLatencyKey =
        linkRoot + QStringLiteral("/high_latency");
    const QString localPortKey = linkRoot + QStringLiteral("/port");
    const QString hostCountKey = linkRoot + QStringLiteral("/hostCount");
    const QString hostKey = linkRoot + QStringLiteral("/host0");
    const QString remotePortKey = linkRoot + QStringLiteral("/port0");

    return settings.contains(nameKey)
        && settings.contains(typeKey)
        && settings.contains(autoKey)
        && settings.contains(highLatencyKey)
        && settings.contains(localPortKey)
        && settings.contains(hostCountKey)
        && settings.contains(hostKey)
        && settings.contains(remotePortKey)
        // The previous installer always wrote this exact spelling. A casing
        // variant was created or renamed by the user and is not managed.
        && settings.value(nameKey).toString() == QStringLiteral("local")
        && settings.value(typeKey, -1).toInt()
            == static_cast<int>(LinkConfiguration::TypeUdp)
        && !settings.value(autoKey, true).toBool()
        && !settings.value(highLatencyKey, true).toBool()
        && settings.value(localPortKey, -1).toInt() == kDefaultLocalPort
        && settings.value(hostCountKey, 0).toInt() == 1
        && settings.value(hostKey).toString().trimmed()
            == kPreviousLocalHost
        && settings.value(remotePortKey, 0).toUInt()
            == kPreviousLocalRemotePort;
}

} // namespace

void DefaultCommunicationLinkInstaller::ensureInstalled()
{
    QSettings settings;
    const QString settingsRoot = LinkConfiguration::settingsRoot();
    const QString countKey = settingsRoot + QStringLiteral("/count");
    int linkCount = qMax(0, settings.value(countKey, 0).toInt());
    const int installedVersion =
        settings.value(kDefaultLinksVersionKey, 0).toInt();
    QStringList installedLinks;
    bool localEndpointMigrated = false;

    for (const DefaultUdpLink& defaultLink : kDefaultLinks) {
        QString linkRoot = findLinkRoot(
            settings,
            settingsRoot,
            linkCount,
            defaultLink.name);
        if (linkRoot.isEmpty()) {
            linkRoot =
                settingsRoot + QStringLiteral("/Link%1").arg(linkCount++);
            writeDefaultLink(settings, linkRoot, defaultLink);
            installedLinks.append(defaultLink.name);
            continue;
        }

        // Only migrate the exact endpoint installed by the previous custom
        // version. A differently configured link with the same name belongs
        // to the user and must not be overwritten.
        if (installedVersion < kDefaultLinksVersion
            && defaultLink.name == QStringLiteral("local")
            && isPreviousManagedLocalEndpoint(settings, linkRoot)) {
            settings.setValue(
                linkRoot + QStringLiteral("/host0"),
                defaultLink.host);
            settings.setValue(
                linkRoot + QStringLiteral("/port0"),
                defaultLink.remotePort);
            localEndpointMigrated = true;
        }
    }

    settings.setValue(countKey, linkCount);
    settings.sync();

    if (settings.status() != QSettings::NoError) {
        qCWarning(DefaultCommunicationLinkLog)
            << "Failed to persist the default communication links";
        return;
    }

    // Commit the migration marker only after the link data itself is durable.
    // If the first sync fails, a later startup must be allowed to retry.
    settings.setValue(kDefaultLinksVersionKey, kDefaultLinksVersion);
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        qCWarning(DefaultCommunicationLinkLog)
            << "Failed to persist the default communication-link version";
        return;
    }

    if (localEndpointMigrated) {
        qCInfo(DefaultCommunicationLinkLog)
            << "Migrated default UDP communication link local to"
            << QStringLiteral("192.168.144.125:14550");
    }
    for (const DefaultUdpLink& defaultLink : kDefaultLinks) {
        if (installedLinks.contains(defaultLink.name)) {
            qCInfo(DefaultCommunicationLinkLog)
                << "Installed default UDP communication link"
                << defaultLink.name
                << QStringLiteral("%1:%2")
                       .arg(defaultLink.host)
                       .arg(defaultLink.remotePort);
        }
    }
    if (!localEndpointMigrated && installedLinks.isEmpty()) {
        qCDebug(DefaultCommunicationLinkLog)
            << "Default communication links already exist";
    }
}
