/****************************************************************************
 *
 * 思翼云台控制设置组。
 * 与 QGC 原生 GimbalControllerSettings 分离，避免影响 MAVLink 云台控制。
 *
 ****************************************************************************/

#pragma once

#include "SettingsGroup.h"

class GimbalControlSettings : public SettingsGroup
{
    Q_OBJECT

public:
    explicit GimbalControlSettings(QObject* parent = nullptr);

    DEFINE_SETTING_NAME_GROUP()

    DEFINE_SETTINGFACT(enabled)
    DEFINE_SETTINGFACT(sdkHost)
    DEFINE_SETTINGFACT(sdkPort)
    DEFINE_SETTINGFACT(zoomStep)
    DEFINE_SETTINGFACT(mavlinkAutoVideoStream)
    DEFINE_SETTINGFACT(forceAndroidH265HardwareDecoder)
};
