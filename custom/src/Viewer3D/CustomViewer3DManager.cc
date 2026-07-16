/****************************************************************************
 *
 * Custom Viewer3D QML type registrar and object owner.
 *
 ****************************************************************************/

#include "CustomViewer3DManager.h"

#include "CityMapGeometry.h"
#include "OsmParser.h"
#include "Viewer3DQmlBackend.h"
#include "Viewer3DQmlVariableTypes.h"
#include "Viewer3DTerrainGeometry.h"
#include "Viewer3DTerrainTexture.h"

#include <QtQml/qqml.h>

CustomViewer3DManager::CustomViewer3DManager()
{
    _qmlBackend = new Viewer3DQmlBackend(this);
    _osmParser = new OsmParser();
    _qmlBackend->init(_osmParser);
}

CustomViewer3DManager::~CustomViewer3DManager()
{
    delete _osmParser;
    delete _qmlBackend;
}

void CustomViewer3DManager::registerQmlTypes()
{
    qmlRegisterUncreatableType<Viewer3DQmlBackend>("QGroundControl.Viewer3D", 1, 0, "Viewer3DQmlBackend", "Reference only");
    qmlRegisterUncreatableType<OsmParser>("QGroundControl.Viewer3D", 1, 0, "OsmParser", "Reference only");
    qmlRegisterType<GeoCoordinateType>("QGroundControl.Viewer3D", 1, 0, "GeoCoordinateType");
    qmlRegisterType<CityMapGeometry>("QGroundControl.Viewer3D", 1, 0, "CityMapGeometry");
    qmlRegisterType<CustomViewer3DManager>("QGroundControl.Viewer3D", 1, 0, "Viewer3DManager");
    qmlRegisterType<Viewer3DTerrainGeometry>("QGroundControl.Viewer3D", 1, 0, "Viewer3DTerrainGeometry");
    qmlRegisterType<Viewer3DTerrainTexture>("QGroundControl.Viewer3D", 1, 0, "Viewer3DTerrainTexture");
}
