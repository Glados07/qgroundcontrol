/****************************************************************************
 *
 * Additional video settings for the custom build.
 *
 ****************************************************************************/

#pragma once

#include "SettingsGroup.h"

class VideoCustomSettings : public SettingsGroup
{
    Q_OBJECT

public:
    explicit VideoCustomSettings(QObject *parent = nullptr);

    DEFINE_SETTING_NAME_GROUP()

    DEFINE_SETTINGFACT(secondaryRtspUrl)
    DEFINE_SETTINGFACT(primaryRtspTcpOnly)
    DEFINE_SETTINGFACT(secondaryRtspTcpOnly)
};
