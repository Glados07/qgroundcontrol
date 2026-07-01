/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "Viewer3DQmlBackend.h"
#include "CustomPlugin.h"
#include "Fact.h"
#include "MultiVehicleManager.h"
#include "Vehicle.h"
#include "Viewer3DSettings.h"
#include "OsmParser.h"

#include <QtCore/QDebug>

#define GPS_REF_NOT_SET                 0
#define GPS_REF_SET_BY_MAP              1
#define GPS_REF_SET_BY_VEHICLE          2
#define GPS_REF_SET_BY_EXTERNAL_MAP     3

Viewer3DQmlBackend::Viewer3DQmlBackend(QObject *parent)
    : QObject{parent}
{
    _gpsRefSet = GPS_REF_NOT_SET;
    _activeVehicle = nullptr;
    _viewer3DSettings = CustomPlugin::customInstance()->viewer3DSettingsFactGroup();
}

void Viewer3DQmlBackend::init(OsmParser* osmThr)
{
    _osmParserThread = osmThr;

    if (_viewer3DSettings) {
        // 外部 3D 模型地图的原点会成为本地 ENU 坐标系参考点；这些设置变化时必须重算车辆和航线位置。

        connect(_viewer3DSettings->useGoogle3DMapSource(), &Fact::rawValueChanged, this, &Viewer3DQmlBackend::_externalMapSettingsChanged);
        connect(_viewer3DSettings->useExternal3DMapSource(), &Fact::rawValueChanged, this, &Viewer3DQmlBackend::_externalMapSettingsChanged);
        connect(_viewer3DSettings->external3DMapOriginLatitude(), &Fact::rawValueChanged, this, &Viewer3DQmlBackend::_externalMapSettingsChanged);
        connect(_viewer3DSettings->external3DMapOriginLongitude(), &Fact::rawValueChanged, this, &Viewer3DQmlBackend::_externalMapSettingsChanged);
        connect(_viewer3DSettings->external3DMapOriginAltitude(), &Fact::rawValueChanged, this, &Viewer3DQmlBackend::_externalMapSettingsChanged);
    }


    if (_osmParserThread) {
        connect(_osmParserThread, &OsmParser::gpsRefChanged, this, &Viewer3DQmlBackend::_gpsRefChangedEvent);
    }

    connect(MultiVehicleManager::instance(), &MultiVehicleManager::activeVehicleChanged, this, &Viewer3DQmlBackend::_activeVehicleChangedEvent);
    _activeVehicleChangedEvent(MultiVehicleManager::instance()->activeVehicle());
    _restoreBestGpsRef();
}

bool Viewer3DQmlBackend::_externalMapSourceEnabled() const
{
    if (!_viewer3DSettings) {
        return false;
    }

    // Google 3D 使用 WebEngine 独立渲染，不走本地 Quick3D 坐标系；因此 Google 打开时不使用外部模型原点。

    return _viewer3DSettings->useExternal3DMapSource()->rawValue().toBool()
            && !_viewer3DSettings->useGoogle3DMapSource()->rawValue().toBool();
}

QGeoCoordinate Viewer3DQmlBackend::_externalMapOrigin() const
{
    if (!_viewer3DSettings) {
        return QGeoCoordinate();
    }

    const double latitude = _viewer3DSettings->external3DMapOriginLatitude()->rawValue().toDouble();
    const double longitude = _viewer3DSettings->external3DMapOriginLongitude()->rawValue().toDouble();
    const double altitude = _viewer3DSettings->external3DMapOriginAltitude()->rawValue().toDouble();
    return QGeoCoordinate(latitude, longitude, altitude);
}

bool Viewer3DQmlBackend::_trySetExternalMapGpsRef()
{
    if (!_externalMapSourceEnabled()) {
        return false;
    }

    const QGeoCoordinate origin = _externalMapOrigin();
    if (!origin.isValid()) {
        qWarning() << "3D viewer external map origin is invalid:" << origin.latitude() << origin.longitude() << origin.altitude();
        return false;
    }

    if (_gpsRefSet != GPS_REF_SET_BY_EXTERNAL_MAP || _gpsRef != origin) {
        _gpsRef = origin;
        _gpsRefSet = GPS_REF_SET_BY_EXTERNAL_MAP;
        emit gpsRefChanged();
        qDebug() << "3D viewer gps reference set by external model map:" << _gpsRef.latitude() << _gpsRef.longitude() << _gpsRef.altitude();
    }

    return true;
}

void Viewer3DQmlBackend::_restoreBestGpsRef()
{
    if (_trySetExternalMapGpsRef()) {
        return;
    }

    if (_gpsRefSet == GPS_REF_SET_BY_EXTERNAL_MAP) {
        _gpsRefSet = GPS_REF_NOT_SET;
    }

    // 外部模型模式关闭后，优先恢复 OSM 地图给出的参考点；没有 OSM 时再回退到车辆当前位置。

    if (_osmParserThread) {
        const QGeoCoordinate osmRef = _osmParserThread->getGpsRef();
        if (osmRef.isValid() && (osmRef.latitude() != 0.0 || osmRef.longitude() != 0.0)) {
            if (_gpsRefSet != GPS_REF_SET_BY_MAP || _gpsRef != osmRef) {
                _gpsRef = osmRef;
                _gpsRefSet = GPS_REF_SET_BY_MAP;
                emit gpsRefChanged();
                qDebug() << "3D viewer gps reference restored from osm map:" << _gpsRef.latitude() << _gpsRef.longitude() << _gpsRef.altitude();
            }
            return;
        }
    }

    if (_activeVehicle) {
        _activeVehicleCoordinateChanged(_activeVehicle->coordinate());
    }
}

void Viewer3DQmlBackend::_activeVehicleChangedEvent(Vehicle *vehicle)
{
    if(_activeVehicle){
        disconnect(_activeVehicle, &Vehicle::coordinateChanged, this, &Viewer3DQmlBackend::_activeVehicleCoordinateChanged);
    }

    _activeVehicle = vehicle;
    if(!_activeVehicle){ // means that all the vehicle have been disconnected
        if(_gpsRefSet == GPS_REF_SET_BY_VEHICLE){
            _gpsRefSet = GPS_REF_NOT_SET;
        }
    }else{
        _activeVehicleCoordinateChanged(_activeVehicle->coordinate());
        connect(_activeVehicle, &Vehicle::coordinateChanged, this, &Viewer3DQmlBackend::_activeVehicleCoordinateChanged);
    }
}

void Viewer3DQmlBackend::_activeVehicleCoordinateChanged(QGeoCoordinate newCoordinate)
{
    if (_externalMapSourceEnabled()) {
        // 外部模型地图必须固定使用用户配置的地图原点，不能被车辆第一次上报的位置覆盖。

        _trySetExternalMapGpsRef();
        return;
    }

    if(_gpsRefSet == GPS_REF_NOT_SET){
        if(newCoordinate.latitude() && newCoordinate.longitude()){
            _gpsRef = newCoordinate;
            _gpsRef.setAltitude(0);
            _gpsRefSet = GPS_REF_SET_BY_VEHICLE;
            emit gpsRefChanged();

            qDebug() << "3D viewer gps reference set by vehicles:" << _gpsRef.latitude() << _gpsRef.longitude() << _gpsRef.altitude();
        }
    }
}

void Viewer3DQmlBackend::_gpsRefChangedEvent(QGeoCoordinate newGpsRef, bool isRefSet)
{
    if (_externalMapSourceEnabled()) {
        // OSM 解析结果只服务本地 OSM 模式；外部模型模式下使用手动配置的模型原点。

        _trySetExternalMapGpsRef();
        return;
    }

    if(isRefSet){
        _gpsRef = newGpsRef;
        _gpsRefSet = GPS_REF_SET_BY_MAP;
        emit gpsRefChanged();
        qDebug() << "3D viewer gps reference set by osm map:" << _gpsRef.latitude() << _gpsRef.longitude() << _gpsRef.altitude();
    }else{
        _gpsRefSet = GPS_REF_NOT_SET;
    }
}

void Viewer3DQmlBackend::_externalMapSettingsChanged(QVariant value)
{
    Q_UNUSED(value)
    _restoreBestGpsRef();
}
