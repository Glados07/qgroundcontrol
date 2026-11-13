/****************************************************************************
 *
 * Additional video settings for the custom build.
 *
 ****************************************************************************/

#include "VideoCustomSettings.h"

#include <QtCore/QSettings>
#include <QtQml/QQmlEngine>

namespace {

constexpr auto kLegacySettingsGroup = "GimbalControl";
constexpr auto kLegacyRtspUrlKey = "mt11RtspUrl";
constexpr auto kLegacyRtspDefault = "rtsp://192.168.144.25:8554/video1";
constexpr auto kMt11RtspDefault = "rtsp://192.168.144.24:8554/video1";
constexpr auto kCurrentRtspDefault = "rtsp://192.168.144.100:8554/live";
constexpr auto kRoukeMigrationKey = "roukeRtspMigrationVersion";

} // namespace

// Keep the value in QGC's native Video settings group. The separate metadata
// file lets custom add a Fact without modifying src/Settings/VideoSettings.
DECLARE_SETTINGGROUP(VideoCustom, "Video")
{
    QSettings settings;
    settings.beginGroup(QString::fromLatin1(settingsGroup));
    const QString secondaryKey = QString::fromLatin1(secondaryRtspUrlName);
    const bool hasSecondaryUrl = settings.contains(secondaryKey);
    settings.endGroup();

    // Preserve the value configured by builds which exposed the second stream
    // as an MT11-specific setting. Do not remove it, so downgrades remain safe.
    if (!hasSecondaryUrl) {
        settings.beginGroup(QString::fromLatin1(kLegacySettingsGroup));
        const bool hasLegacyUrl = settings.contains(
            QString::fromLatin1(kLegacyRtspUrlKey));
        QString legacyUrl = settings.value(
            QString::fromLatin1(kLegacyRtspUrlKey)).toString();
        settings.endGroup();

        if (hasLegacyUrl) {
            // Only translate the exact default shipped by the older build.
            // Every user-defined value, including an empty string, is kept.
            if (legacyUrl == QLatin1String(kLegacyRtspDefault)
                || legacyUrl == QLatin1String(kMt11RtspDefault)) {
                legacyUrl = QLatin1String(kCurrentRtspDefault);
            }
            settings.beginGroup(QString::fromLatin1(settingsGroup));
            settings.setValue(secondaryKey, legacyUrl);
            settings.endGroup();
        }
    }

    // Upgrade only the two shipped MT11 defaults, once. Explicit custom URLs
    // and an empty (disabled) stream remain the user's choice.
    settings.beginGroup(QString::fromLatin1(settingsGroup));
    if (settings.value(QLatin1String(kRoukeMigrationKey), 0).toInt() < 1) {
        const QString url = settings.value(secondaryKey).toString();
        if (settings.contains(secondaryKey)
            && (url == QLatin1String(kLegacyRtspDefault)
                || url == QLatin1String(kMt11RtspDefault))) {
            settings.setValue(secondaryKey, QLatin1String(kCurrentRtspDefault));
        }
        settings.setValue(QLatin1String(kRoukeMigrationKey), 1);
    }
    settings.endGroup();

    qmlRegisterUncreatableType<VideoCustomSettings>(
        "QGroundControl.CustomSettings", 1, 0, "VideoCustomSettings", "Reference only");
}

DECLARE_SETTINGSFACT(VideoCustomSettings, secondaryRtspUrl)
