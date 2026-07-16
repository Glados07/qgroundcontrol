/****************************************************************************
 *
 * custom 默认通信链路安装器。
 * 直接使用 QGC 标准 QSettings 结构写入配置，后续仍由原生 LinkManager
 * 负责加载、编辑、连接和持久化。
 *
 ****************************************************************************/

#include "DefaultCommunicationLinkInstaller.h"

#include "LinkConfiguration.h"

#include <QtCore/QLoggingCategory>
#include <QtCore/QSettings>
#include <QtCore/QString>
#include <QtCore/QtGlobal>

Q_LOGGING_CATEGORY(DefaultCommunicationLinkLog, "gcs.custom.communicationlink")

namespace {
const QString kDefaultLinkName = QStringLiteral("local");
const QString kDefaultHost = QStringLiteral("192.168.144.20");
constexpr quint16 kDefaultRemotePort = 19856;
constexpr quint16 kDefaultLocalPort = 0;
}

void DefaultCommunicationLinkInstaller::ensureInstalled()
{
    QSettings settings;
    const QString settingsRoot = LinkConfiguration::settingsRoot();
    const QString countKey = settingsRoot + QStringLiteral("/count");
    const int linkCount = qMax(0, settings.value(countKey, 0).toInt());

    // 名称比较忽略大小写，兼容用户已经手动创建的 Local，避免升级后出现重复链路。
    for (int index = 0; index < linkCount; ++index) {
        const QString linkRoot = settingsRoot + QStringLiteral("/Link%1").arg(index);
        const QString linkName = settings.value(linkRoot + QStringLiteral("/name")).toString().trimmed();
        if (linkName.compare(kDefaultLinkName, Qt::CaseInsensitive) == 0) {
            qCDebug(DefaultCommunicationLinkLog) << "Default communication link already exists:" << linkName;
            return;
        }
    }

    const QString linkRoot = settingsRoot + QStringLiteral("/Link%1").arg(linkCount);
    settings.setValue(linkRoot + QStringLiteral("/name"), kDefaultLinkName);
    settings.setValue(linkRoot + QStringLiteral("/type"), static_cast<int>(LinkConfiguration::TypeUdp));
    settings.setValue(linkRoot + QStringLiteral("/auto"), false);
    settings.setValue(linkRoot + QStringLiteral("/high_latency"), false);
    settings.setValue(linkRoot + QStringLiteral("/port"), kDefaultLocalPort);
    settings.setValue(linkRoot + QStringLiteral("/hostCount"), 1);
    settings.setValue(linkRoot + QStringLiteral("/host0"), kDefaultHost);
    settings.setValue(linkRoot + QStringLiteral("/port0"), kDefaultRemotePort);
    settings.setValue(countKey, linkCount + 1);
    settings.sync();

    if (settings.status() != QSettings::NoError) {
        qCWarning(DefaultCommunicationLinkLog) << "Failed to persist the default communication link";
        return;
    }

    qCInfo(DefaultCommunicationLinkLog)
        << "Installed default UDP communication link"
        << kDefaultLinkName
        << QStringLiteral("%1:%2").arg(kDefaultHost).arg(kDefaultRemotePort);
}
