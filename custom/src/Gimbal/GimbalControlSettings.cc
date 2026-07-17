/****************************************************************************
 *
 * 思翼云台控制设置组。
 *
 ****************************************************************************/

#include "GimbalControlSettings.h"

#include <QtQml/QQmlEngine>

DECLARE_SETTINGGROUP(GimbalControl, "GimbalControl")
{
    qmlRegisterUncreatableType<GimbalControlSettings>("QGroundControl.GimbalControl", 1, 0, "GimbalControlSettings", "Reference only");
}

DECLARE_SETTINGSFACT(GimbalControlSettings, enabled)
DECLARE_SETTINGSFACT(GimbalControlSettings, sdkHost)
DECLARE_SETTINGSFACT(GimbalControlSettings, sdkPort)
DECLARE_SETTINGSFACT(GimbalControlSettings, zoomStep)
DECLARE_SETTINGSFACT(GimbalControlSettings, mavlinkAutoVideoStream)
DECLARE_SETTINGSFACT(GimbalControlSettings, forceAndroidH265HardwareDecoder)
