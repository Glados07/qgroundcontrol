/****************************************************************************
 *
 * 思翼云台控制设置组。
 *
 ****************************************************************************/

#include "GimbalControlSettings.h"

#include <QtCore/QSettings>
#include <QtQml/QQmlEngine>

namespace {

constexpr auto kMt11SdkHostDefaultMigrationVersionKey = "mt11SdkHostDefaultMigrationVersion";
constexpr int kMt11SdkHostDefaultMigrationVersion = 1;
constexpr auto kLegacyMt11SdkHostDefault = "192.168.144.25";
constexpr auto kCurrentMt11SdkHostDefault = "192.168.144.24";

} // namespace

DECLARE_SETTINGGROUP(GimbalControl, "GimbalControl")
{
    // Migrate only the previously shipped MT11 SDK host. User-provided hosts
    // must be retained, including another endpoint on the same subnet.
    QSettings settings;
    settings.beginGroup(QString::fromLatin1(settingsGroup));
    if (settings.value(QLatin1String(kMt11SdkHostDefaultMigrationVersionKey), 0).toInt()
        < kMt11SdkHostDefaultMigrationVersion) {
        const QString sdkHostKey = QString::fromLatin1(mt11SdkHostName);
        if (settings.contains(sdkHostKey)
            && settings.value(sdkHostKey).toString()
                   == QLatin1String(kLegacyMt11SdkHostDefault)) {
            settings.setValue(sdkHostKey,
                              QLatin1String(kCurrentMt11SdkHostDefault));
        }
        settings.setValue(QLatin1String(kMt11SdkHostDefaultMigrationVersionKey),
                          kMt11SdkHostDefaultMigrationVersion);
    }
    settings.endGroup();

    qmlRegisterUncreatableType<GimbalControlSettings>("QGroundControl.GimbalControl", 1, 0, "GimbalControlSettings", "Reference only");
}

DECLARE_SETTINGSFACT(GimbalControlSettings, enabled)
DECLARE_SETTINGSFACT(GimbalControlSettings, localMediaStorageEnabled)
DECLARE_SETTINGSFACT(GimbalControlSettings, sdkHost)
DECLARE_SETTINGSFACT(GimbalControlSettings, sdkPort)
DECLARE_SETTINGSFACT(GimbalControlSettings, zoomStep)
DECLARE_SETTINGSFACT(GimbalControlSettings, mt11Enabled)
DECLARE_SETTINGSFACT(GimbalControlSettings, mt11SdkHost)
DECLARE_SETTINGSFACT(GimbalControlSettings, mt11SdkPort)
DECLARE_SETTINGSFACT(GimbalControlSettings, mt11ZoomStep)
DECLARE_SETTINGSFACT(GimbalControlSettings, mavlinkAutoVideoStream)
DECLARE_SETTINGSFACT(GimbalControlSettings, forceAndroidH265HardwareDecoder)
DECLARE_SETTINGSFACT(GimbalControlSettings, uniRcChannelControlEnabled)
DECLARE_SETTINGSFACT(GimbalControlSettings, uniRcSdkBluetoothAddress)
