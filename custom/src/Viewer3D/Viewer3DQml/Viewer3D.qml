import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.Controllers
import QGroundControl.FactSystem
import QGroundControl.FlightDisplay
import QGroundControl.FlightMap
import QGroundControl.Palette
import QGroundControl.ScreenTools
import QGroundControl.Vehicle

import QGroundControl.Viewer3D
import Viewer3D.Models3D


///     @author Omid Esrafilian <esrafilian.omid@gmail.com>

Item{
    id: viewer3DBody

    property bool isOpen: false
    property var    _viewer3DSettings:              QGroundControl.corePlugin.viewer3DSettings
    property bool   _viewer3DEnabled:               _viewer3DSettings.enabled.rawValue
    property bool   _useGoogle3DMapSource:          _viewer3DSettings.useGoogle3DMapSource.rawValue
    property bool   _google3DMapsAvailable:         QGroundControl.corePlugin.google3DMapsAvailable

    function _viewer3DSource() {
        // Google 3D 在线地图使用独立 WebEngine 视图；关闭开关时保持原来的本地 OSM/Quick3D 渲染链路。
        if (_useGoogle3DMapSource) {
            return _google3DMapsAvailable ? "Google3DMapView.qml" : "Google3DMapUnavailable.qml"
        }
        return "Models3D/Viewer3DModel.qml"
    }

    function _bindLoadedView() {
        if (view3DLoader.status === Loader.Ready && view3DManagerLoader.status === Loader.Ready && view3DLoader.item) {
            view3DLoader.item.viewer3DManager = view3DManagerLoader.item
        }
    }

    function open(){
        if(_viewer3DEnabled === true){
            view3DManagerLoader.active = true;
            isOpen = true;
        }
    }

    function close(){
        isOpen = false;
    }

    visible: isOpen
    enabled: isOpen

    on_Viewer3DEnabledChanged: {
        if(_viewer3DEnabled === false){
            viewer3DBody.close();
            view3DManagerLoader.active = false;
        }
    }

    Component{
        id: viewer3DManagerComponent

        Viewer3DManager{
            id: _viewer3DManager
        }
    }

    Loader{
        id: view3DManagerLoader
        active: false
        sourceComponent: viewer3DManagerComponent

        onLoaded: {
            if(view3DLoader.active === true){
                viewer3DBody._bindLoadedView()
            }else{
                view3DLoader.active = true
            }
        }
    }

    Loader{
        id: view3DLoader
        anchors.fill: parent
        active: false
        source: viewer3DBody._viewer3DSource()

        onLoaded: viewer3DBody._bindLoadedView()
    }

    Binding{
        target: view3DLoader.item
        property: "isViewer3DOpen"
        value: isOpen
        when: view3DLoader.status == Loader.Ready
    }
}