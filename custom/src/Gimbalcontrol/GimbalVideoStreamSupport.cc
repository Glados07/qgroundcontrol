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
constexpr const char* kA8MiniRtspUrl = "rtsp://192.168.144.25:8554/main.264";

} // namespace

void GimbalVideoStreamSupport::installA8MiniDefaults()
{
    QSettings settings;
    settings.beginGroup(QString::fromLatin1(kSettingsGroup));
    const bool defaultsInstalled = settings.value(QString::fromLatin1(kDefaultsInstalledKey), false).toBool();
    settings.endGroup();
    if (defaultsInstalled) {
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

    // 只补充空值或 QGC 初始禁用值，保留用户已经配置的其他视频源。
    if (videoSettings->rtspUrl()->rawValue().toString().trimmed().isEmpty()) {
        videoSettings->rtspUrl()->setRawValue(QString::fromLatin1(kA8MiniRtspUrl));
    }

    const QString currentSource = videoSourceFact->rawValue().toString();
    if (currentSource.isEmpty() ||
        currentSource == QString::fromLatin1(VideoSettings::videoDisabled) ||
        currentSource == QString::fromLatin1(VideoSettings::videoSourceNoVideo)) {
        videoSourceFact->setRawValue(rtspSource);
    }

    settings.beginGroup(QString::fromLatin1(kSettingsGroup));
    settings.setValue(QString::fromLatin1(kDefaultsInstalledKey), true);
    settings.endGroup();
    settings.sync();

    qCInfo(GimbalVideoStreamLog) << "Installed A8 Mini video defaults" << kA8MiniRtspUrl;
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
