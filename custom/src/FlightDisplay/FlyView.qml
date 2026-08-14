/****************************************************************************
 *
 * (c) 2009-2020 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

import QtLocation
import QtPositioning
import QtQuick.Window
import QtQml.Models

import QGroundControl
import QGroundControl.Controllers
import QGroundControl.Controls
import QGroundControl.FactSystem
import QGroundControl.FlightDisplay
import QGroundControl.FlightMap
import QGroundControl.Palette
import QGroundControl.ScreenTools
import QGroundControl.Vehicle

import Custom.FlightDisplay as CustomFlightDisplay
import Viewer3D

Item {
    id: _root

    property var planController: _planController
    property var guidedController: _guidedController
    property bool utmspSendActTrigger: false

    PlanMasterController {
        id: _planController
        flyView: true
        Component.onCompleted: start()
    }

    property var _dualVideoManager: QGroundControl.corePlugin
                                    ? QGroundControl.corePlugin.dualVideoManager
                                    : null
    property bool _mainWindowIsMap: mapControl.pipState.state
                                    === mapControl.pipState.fullState
    property bool _isFullWindowItemDark: !_mainWindowIsMap
                                         || mapControl.isSatelliteMap
    property var _activeVehicle: QGroundControl.multiVehicleManager.activeVehicle
    property var _missionController: _planController.missionController
    property var _geoFenceController: _planController.geoFenceController
    property var _rallyPointController: _planController.rallyPointController
    property real _margins: ScreenTools.defaultFontPixelWidth / 2
    property var _guidedController: guidedActionsController
    property var _guidedValueSlider: guidedValueSlider
    property var _widgetLayer: widgetLayer
    property real _toolsMargin: ScreenTools.defaultFontPixelWidth * 0.75
    property rect _centerViewport: Qt.rect(0, 0, width, height)
    property real _rightPanelWidth: ScreenTools.defaultFontPixelWidth * 30
    property var _mapControl: mapControl
    property bool _anyVideoFullScreen: QGroundControl.videoManager.fullScreen
                                       || (_dualVideoManager
                                           && _dualVideoManager.fullScreen)

    property real _fullItemZorder: 0
    property real _pipItemZorder: QGroundControl.zOrderWidgets

    function _calcCenterViewPort() {
        var newToolInset = Qt.rect(0, 0, width, height)
        toolstrip.adjustToolInset(newToolInset)
    }

    function dropMainStatusIndicatorTool() {
        toolbar.dropMainStatusIndicatorTool()
    }

    Component.onCompleted: {
        if (_dualVideoManager && _root.Window.window) {
            _dualVideoManager.init(_root.Window.window)
        }
    }

    on_ActiveVehicleChanged: {
        if (!_activeVehicle && _dualVideoManager) {
            _dualVideoManager.fullScreen = false
        }
    }

    Connections {
        target: QGroundControl.videoManager

        function onHasVideoChanged() {
            if (!QGroundControl.videoManager.hasVideo) {
                QGroundControl.videoManager.fullScreen = false
                if (videoControl.pipState.state
                        === videoControl.pipState.fullState) {
                    _pipView._setFullItem(mapControl, true)
                }
            }
        }
    }

    Connections {
        target: _activeVehicle
        ignoreUnknownSignals: true

        function onCommunicationLostChanged() {
            if (_activeVehicle && _activeVehicle.communicationLost
                    && _dualVideoManager) {
                _dualVideoManager.fullScreen = false
            }
        }
    }

    Connections {
        target: _dualVideoManager
        ignoreUnknownSignals: true

        function onEnabledChanged() {
            if (_dualVideoManager.enabled && _root.Window.window) {
                _dualVideoManager.init(_root.Window.window)
            }
        }

        function onHasVideoChanged() {
            if (!_dualVideoManager.hasVideo
                    && mt11VideoControl.pipState.state
                    === mt11VideoControl.pipState.fullState) {
                _pipView._setFullItem(mapControl, true)
            }
        }
    }

    QGCToolInsets {
        id: _toolInsets
        leftEdgeBottomInset: _pipView.leftEdgeBottomInset
        bottomEdgeLeftInset: _pipView.bottomEdgeLeftInset
    }

    FlyViewToolBar {
        id: toolbar
        visible: !_anyVideoFullScreen
    }

    Item {
        id: mapHolder
        anchors.top: toolbar.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right

        FlyViewMap {
            id: mapControl
            planMasterController: _planController
            rightPanelWidth: ScreenTools.defaultFontPixelHeight * 9
            pipView: _pipView.item1PipView
            pipMode: !_mainWindowIsMap
            toolInsets: customOverlay.totalToolInsets
            mapName: "FlightDisplayView"
            enabled: !viewer3DWindow.isOpen
        }

        FlyViewVideo {
            id: videoControl
            pipView: _pipView.item2PipView
        }

        CustomFlightDisplay.MT11Video {
            id: mt11VideoControl
            pipView: _pipView.item3PipView
            visible: _dualVideoManager
                     && (_dualVideoManager.hasVideo
                         || _dualVideoManager.initialized)
        }

        CustomFlightDisplay.DualPipView {
            id: _pipView
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            anchors.margins: _toolsMargin
            currentItemSettingsKey: "MainFlyWindowView"
            item1: mapControl
            item2: QGroundControl.videoManager.hasVideo ? videoControl : null
            item3: _dualVideoManager && _dualVideoManager.hasVideo
                   ? mt11VideoControl : null
            show: !_anyVideoFullScreen
            z: QGroundControl.zOrderWidgets

            property real leftEdgeBottomInset: visible
                                                   ? width + anchors.margins : 0
            property real bottomEdgeLeftInset: visible
                                                   ? height + anchors.margins : 0
        }

        FlyViewWidgetLayer {
            id: widgetLayer
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: guidedValueSlider.visible
                           ? guidedValueSlider.left : parent.right
            z: _fullItemZorder + 2
            parentToolInsets: _toolInsets
            mapControl: _mapControl
            visible: !_anyVideoFullScreen
            utmspActTrigger: utmspSendActTrigger
            isViewer3DOpen: viewer3DWindow.isOpen
        }

        FlyViewCustomLayer {
            id: customOverlay
            anchors.fill: widgetLayer
            z: _fullItemZorder + 2
            parentToolInsets: widgetLayer.totalToolInsets
            mapControl: _mapControl
            visible: !_anyVideoFullScreen
        }

        FlyViewInsetViewer {
            id: widgetLayerInsetViewer
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: guidedValueSlider.visible
                           ? guidedValueSlider.left : parent.right
            z: widgetLayer.z + 1
            insetsToView: widgetLayer.totalToolInsets
            visible: false
        }

        GuidedActionsController {
            id: guidedActionsController
            missionController: _missionController
            guidedValueSlider: _guidedValueSlider
        }

        GuidedValueSlider {
            id: guidedValueSlider
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            z: QGroundControl.zOrderTopMost
            visible: false
        }

        Viewer3D {
            id: viewer3DWindow
            anchors.fill: parent
        }
    }
}
