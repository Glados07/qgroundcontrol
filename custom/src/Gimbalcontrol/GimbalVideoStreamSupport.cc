/****************************************************************************
 *
 * 思翼 A8 Mini 视频流适配。
 *
 ****************************************************************************/

#include "GimbalVideoStreamSupport.h"

#include "GimbalControlSettings.h"
#include "Fact.h"
#include "QGCMAVLink.h"
#include "SettingsManager.h"
#include "VideoSettings.h"

#include <QtCore/QLoggingCategory>
#include <QtCore/QSettings>

namespace {

Q_LOGGING_CATEGORY(GimbalVideoStreamLog, "gcs.custom.gimbal.videostream")

constexpr const char* kSettingsGroup = "GimbalControl";
constexpr const char* kDefaultsInstalledKey = "a8MiniVideoDefaultsInstalled";
constexpr const char* kDefaultsVersionKey = "a8MiniVideoDefaultsVersion";
constexpr int kCurrentDefaultsVersion = 2;
constexpr quint32 kA8MiniRtspTimeoutSeconds = 20;
constexpr const char* kA8MiniLegacyRtspUrl = "rtsp://192.168.144.25:8554/main.264";
constexpr const char* kA8MiniRtspUrl = "rtspt://192.168.144.25:8554/main.264";

} // namespace

void GimbalVideoStreamSupport::installA8MiniDefaults()
{
    QSettings settings;
    settings.beginGroup(QString::fromLatin1(kSettingsGroup));
    const int installedVersion = settings.value(QString::fromLatin1(kDefaultsVersionKey), 0).toInt();
    settings.endGroup();
    if (installedVersion >= kCurrentDefaultsVersion) {
        return;
    }

    VideoSettings* const videoSettings = SettingsManager::instance()->videoSettings();
    if (!videoSettings) {
        qCWarning(GimbalVideoStreamLog) << "VideoSettings is unavailable; A8 Mini defaults were not installed";
        return;
    }

    Fact* const videoSourceFact = videoSettings->videoSource();
    const QString rtspSource = QString::fromLatin1(VideoSettings::videoSourceRTSP);
    if (!videoSourceFact->enumValues().contains(rtspSource)) {
        qCWarning(GimbalVideoStreamLog) << "RTSP is unavailable in this build; check the QGC GStreamer configuration";
        return;
    }

    // rtspt 是 GStreamer 的 RTSP-over-TCP URI，避免虚拟机丢失动态 RTP/UDP 回包。
    // 仅迁移空值和旧版 A8 Mini 默认地址，不覆盖用户配置的其他相机地址。
    const QString configuredUrl = videoSettings->rtspUrl()->rawValue().toString().trimmed();
    const bool isLegacyA8MiniUrl = configuredUrl.compare(
        QString::fromLatin1(kA8MiniLegacyRtspUrl), Qt::CaseInsensitive) == 0;
    if (configuredUrl.isEmpty() || isLegacyA8MiniUrl) {
        videoSettings->rtspUrl()->setRawValue(QString::fromLatin1(kA8MiniRtspUrl));
    }

    const QString effectiveUrl = videoSettings->rtspUrl()->rawValue().toString().trimmed();
    const bool isA8MiniUrl = effectiveUrl.compare(
        QString::fromLatin1(kA8MiniRtspUrl), Qt::CaseInsensitive) == 0;
    if (isA8MiniUrl && videoSettings->rtspTimeout()->rawValue().toUInt() < kA8MiniRtspTimeoutSeconds) {
        // QGC 原生默认值为 8 秒，可能在云台上电或 TCP 建链尚未完成时提前重启管线。
        videoSettings->rtspTimeout()->setRawValue(kA8MiniRtspTimeoutSeconds);
    }

    const QString currentSource = videoSourceFact->rawValue().toString();
    if (currentSource.isEmpty() ||
        currentSource == QString::fromLatin1(VideoSettings::videoDisabled) ||
        currentSource == QString::fromLatin1(VideoSettings::videoSourceNoVideo)) {
        videoSourceFact->setRawValue(rtspSource);
    }

    settings.beginGroup(QString::fromLatin1(kSettingsGroup));
    settings.setValue(QString::fromLatin1(kDefaultsInstalledKey), true);
    settings.setValue(QString::fromLatin1(kDefaultsVersionKey), kCurrentDefaultsVersion);
    settings.endGroup();
    settings.sync();

    qCInfo(GimbalVideoStreamLog) << "Installed A8 Mini video defaults"
                                << kA8MiniRtspUrl
                                << "timeout" << kA8MiniRtspTimeoutSeconds;
}

bool GimbalVideoStreamSupport::shouldFilterMavlinkMessage(GimbalControlSettings* settings,
                                                          const mavlink_message_t& message)
{
    if (!settings || message.msgid != MAVLINK_MSG_ID_VIDEO_STREAM_INFORMATION) {
        return false;
    }

    // 关闭自动流后丢弃相机上报的 URI，避免原生 VideoManager 进入不可编辑的自动模式。
    const bool gimbalControlEnabled = settings->enabled()->rawValue().toBool();
    const bool useMavlinkAutoStream = settings->mavlinkAutoVideoStream()->rawValue().toBool();
    return gimbalControlEnabled && !useMavlinkAutoStream;
}
