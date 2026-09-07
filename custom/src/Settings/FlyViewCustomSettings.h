/****************************************************************************
 *
 * Fly View custom settings.
 *
 ****************************************************************************/

#pragma once

#include "SettingsGroup.h"

class FlyViewCustomSettings : public SettingsGroup
{
    Q_OBJECT

public:
    explicit FlyViewCustomSettings(QObject *parent = nullptr);

    DEFINE_SETTING_NAME_GROUP()

    DEFINE_SETTINGFACT(showHeadingCompassBar)
    DEFINE_SETTINGFACT(showGimbalHeadingCompassBar)
};
