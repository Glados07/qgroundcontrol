/****************************************************************************
 *
 * Custom default communication-link installer.
 * Reconciles QGC's standard QSettings representation before LinkManager
 * loads it; LinkManager continues to own connection and persistence.
 *
 ****************************************************************************/

#include "DefaultCommunicationLinkInstaller.h"

#include "LinkConfiguration.h"

#include <QtCore/QList>
#include <QtCore/QLoggingCategory>
#include <QtCore/QMap>
#include <QtCore/QSettings>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QVariant>
#include <QtCore/QtGlobal>

Q_LOGGING_CATEGORY(DefaultCommunicationLinkLog, "gcs.custom.communicationlink")

namespace {

const QString kDefaultLinkName = QStringLiteral("local");
const QString kDefaultHost = QStringLiteral("192.168.144.125");
constexpr quint16 kDefaultRemotePort = 14550;
constexpr quint16 kDefaultLocalPort = 0;

// Removed profile from the previous two-default-link implementation. It is
// purged before LinkManager loads settings so an old saved test link cannot
// auto-connect or participate in QGC's primary/secondary-link selection.
const QString kRemovedTestLinkName = QStringLiteral("testlocal");
const QString kObsoleteDefaultLinksVersionKey =
    QStringLiteral("CustomCommunicationLinks/defaultsVersion");

using StoredLinkSettings = QMap<QString, QVariant>;

const QStringList kDefaultLinkKeys = {
    QStringLiteral("auto"),
    QStringLiteral("high_latency"),
    QStringLiteral("host0"),
    QStringLiteral("hostCount"),
    QStringLiteral("name"),
    QStringLiteral("port"),
    QStringLiteral("port0"),
    QStringLiteral("type"),
};

StoredLinkSettings readStoredLink(QSettings& settings,
                                  const QString& linkRoot)
{
    StoredLinkSettings storedLink;
    settings.beginGroup(linkRoot);
    const QStringList keys = settings.allKeys();
    for (const QString& key : keys) {
        storedLink.insert(key, settings.value(key));
    }
    settings.endGroup();
    return storedLink;
}

void writeStoredLink(QSettings& settings,
                     const QString& linkRoot,
                     const StoredLinkSettings& storedLink)
{
    settings.remove(linkRoot);
    for (auto iterator = storedLink.constBegin();
         iterator != storedLink.constEnd();
         ++iterator) {
        settings.setValue(
            linkRoot + QLatin1Char('/') + iterator.key(),
            iterator.value());
    }
}

StoredLinkSettings defaultLinkSettings()
{
    StoredLinkSettings settings;
    settings.insert(QStringLiteral("name"), kDefaultLinkName);
    settings.insert(
        QStringLiteral("type"),
        static_cast<int>(LinkConfiguration::TypeUdp));
    settings.insert(QStringLiteral("auto"), false);
    settings.insert(QStringLiteral("high_latency"), false);
    settings.insert(QStringLiteral("port"), kDefaultLocalPort);
    settings.insert(QStringLiteral("hostCount"), 1);
    settings.insert(QStringLiteral("host0"), kDefaultHost);
    settings.insert(QStringLiteral("port0"), kDefaultRemotePort);
    return settings;
}

bool isCanonicalDefaultLink(const StoredLinkSettings& storedLink)
{
    return storedLink.keys() == kDefaultLinkKeys
        && storedLink.value(QStringLiteral("name")).toString()
            == kDefaultLinkName
        && storedLink.value(QStringLiteral("type")).toInt()
            == static_cast<int>(LinkConfiguration::TypeUdp)
        && !storedLink.value(QStringLiteral("auto"), true).toBool()
        && !storedLink.value(QStringLiteral("high_latency"), true).toBool()
        && storedLink.value(QStringLiteral("port"), -1).toInt()
            == kDefaultLocalPort
        && storedLink.value(QStringLiteral("hostCount"), 0).toInt() == 1
        && storedLink.value(QStringLiteral("host0")).toString()
            == kDefaultHost
        && storedLink.value(QStringLiteral("port0"), 0).toUInt()
            == kDefaultRemotePort;
}

} // namespace

void DefaultCommunicationLinkInstaller::ensureInstalled()
{
    QSettings settings;
    const QString settingsRoot = LinkConfiguration::settingsRoot();
    const QString countKey = settingsRoot + QStringLiteral("/count");
    const int linkCount = qMax(0, settings.value(countKey, 0).toInt());

    QList<StoredLinkSettings> retainedLinks;
    retainedLinks.reserve(linkCount + 1);
    bool defaultLinkSeen = false;
    bool linksChanged = false;
    bool removedTestLink = false;
    bool resetDefaultLink = false;

    for (int index = 0; index < linkCount; ++index) {
        const QString linkRoot =
            settingsRoot + QStringLiteral("/Link%1").arg(index);
        const StoredLinkSettings storedLink =
            readStoredLink(settings, linkRoot);
        const QString storedName =
            storedLink.value(QStringLiteral("name")).toString().trimmed();

        if (storedName.compare(kRemovedTestLinkName, Qt::CaseInsensitive)
            == 0) {
            linksChanged = true;
            removedTestLink = true;
            continue;
        }

        if (storedName.compare(kDefaultLinkName, Qt::CaseInsensitive) == 0) {
            if (defaultLinkSeen) {
                linksChanged = true;
                resetDefaultLink = true;
                continue;
            }

            defaultLinkSeen = true;
            if (isCanonicalDefaultLink(storedLink)) {
                retainedLinks.append(storedLink);
            } else {
                retainedLinks.append(defaultLinkSettings());
                linksChanged = true;
                resetDefaultLink = true;
            }
            continue;
        }

        retainedLinks.append(storedLink);
    }

    if (!defaultLinkSeen) {
        retainedLinks.append(defaultLinkSettings());
        linksChanged = true;
        resetDefaultLink = true;
    }

    if (linksChanged) {
        for (int index = 0; index < linkCount; ++index) {
            settings.remove(
                settingsRoot + QStringLiteral("/Link%1").arg(index));
        }
        for (int index = 0; index < retainedLinks.count(); ++index) {
            writeStoredLink(
                settings,
                settingsRoot + QStringLiteral("/Link%1").arg(index),
                retainedLinks[index]);
        }
        settings.setValue(countKey, retainedLinks.count());
    }

    // The old marker controlled migration between the two former profiles.
    // A single authoritative local profile no longer needs it.
    settings.remove(kObsoleteDefaultLinksVersionKey);
    settings.sync();

    if (settings.status() != QSettings::NoError) {
        qCWarning(DefaultCommunicationLinkLog)
            << "Failed to persist the default communication link";
        return;
    }

    if (removedTestLink) {
        qCInfo(DefaultCommunicationLinkLog)
            << "Removed obsolete UDP communication link"
            << kRemovedTestLinkName;
    }
    if (resetDefaultLink) {
        qCInfo(DefaultCommunicationLinkLog)
            << "Installed or reset default UDP communication link"
            << kDefaultLinkName
            << QStringLiteral("%1:%2")
                   .arg(kDefaultHost)
                   .arg(kDefaultRemotePort);
    } else {
        qCDebug(DefaultCommunicationLinkLog)
            << "Default UDP communication link already matches"
            << QStringLiteral("%1:%2")
                   .arg(kDefaultHost)
                   .arg(kDefaultRemotePort);
    }
}
