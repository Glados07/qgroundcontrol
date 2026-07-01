/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#pragma once

#include "SettingsGroup.h"

///     @author Omid Esrafilian <esrafilian.omid@gmail.com>

class Viewer3DSettings : public SettingsGroup
{
    Q_OBJECT
public:
    Viewer3DSettings(QObject* parent = nullptr);

    DEFINE_SETTING_NAME_GROUP()

    DEFINE_SETTINGFACT(enabled)
    DEFINE_SETTINGFACT(useGoogle3DMapSource)
    DEFINE_SETTINGFACT(google3DMapsApiKey)
    DEFINE_SETTINGFACT(useExternal3DMapSource)
    DEFINE_SETTINGFACT(external3DMapFilePath)
    DEFINE_SETTINGFACT(external3DMapOriginLatitude)
    DEFINE_SETTINGFACT(external3DMapOriginLongitude)
    DEFINE_SETTINGFACT(external3DMapOriginAltitude)
    DEFINE_SETTINGFACT(external3DMapUnitToMeters)
    DEFINE_SETTINGFACT(external3DMapScale)
    DEFINE_SETTINGFACT(external3DMapYaw)
    DEFINE_SETTINGFACT(osmFilePath)
    DEFINE_SETTINGFACT(buildingLevelHeight)
    DEFINE_SETTINGFACT(altitudeBias)
};
