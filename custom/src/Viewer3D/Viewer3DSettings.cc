/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "Viewer3DSettings.h"

#include <QtQml/QQmlEngine>

DECLARE_SETTINGGROUP(Viewer3D, "Viewer3D")
{
    qmlRegisterUncreatableType<Viewer3DSettings>("QGroundControl.Viewer3D", 1, 0, "Viewer3DSettings", "Reference only");
}

DECLARE_SETTINGSFACT(Viewer3DSettings, enabled)
DECLARE_SETTINGSFACT(Viewer3DSettings, useGoogle3DMapSource)
DECLARE_SETTINGSFACT(Viewer3DSettings, google3DMapsApiKey)
DECLARE_SETTINGSFACT(Viewer3DSettings, useExternal3DMapSource)
DECLARE_SETTINGSFACT(Viewer3DSettings, external3DMapFilePath)
DECLARE_SETTINGSFACT(Viewer3DSettings, external3DMapOriginLatitude)
DECLARE_SETTINGSFACT(Viewer3DSettings, external3DMapOriginLongitude)
DECLARE_SETTINGSFACT(Viewer3DSettings, external3DMapOriginAltitude)
DECLARE_SETTINGSFACT(Viewer3DSettings, external3DMapUnitToMeters)
DECLARE_SETTINGSFACT(Viewer3DSettings, external3DMapScale)
DECLARE_SETTINGSFACT(Viewer3DSettings, external3DMapYaw)
DECLARE_SETTINGSFACT(Viewer3DSettings, osmFilePath)
DECLARE_SETTINGSFACT(Viewer3DSettings, buildingLevelHeight)
DECLARE_SETTINGSFACT(Viewer3DSettings, altitudeBias)
