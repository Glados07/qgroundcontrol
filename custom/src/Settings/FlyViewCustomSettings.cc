/****************************************************************************
 *
 * Fly View custom settings.
 *
 ****************************************************************************/

#include "FlyViewCustomSettings.h"

#include <QtQml/QQmlEngine>

// 元数据保持 custom 独立资源名，实际值写入原生 FlyView QSettings 分组。
DECLARE_SETTINGGROUP(FlyViewCustom, "FlyView")
{
    qmlRegisterUncreatableType<FlyViewCustomSettings>(
        "QGroundControl.CustomSettings", 1, 0, "FlyViewCustomSettings", "Reference only");
}

DECLARE_SETTINGSFACT(FlyViewCustomSettings, showHeadingCompassBar)
