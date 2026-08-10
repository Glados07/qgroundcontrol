/****************************************************************************
 *
 * Fly View custom overlay for the heading compass bar and fuel-cell warning.
 *
 ****************************************************************************/

import QtQuick

import QGroundControl
import QGroundControl.Controls
import QGroundControl.ScreenTools

import Custom.Widgets

Item {
    id: root

    property var parentToolInsets
    property var totalToolInsets: toolInsets
    property var mapControl

    property var _activeVehicle: QGroundControl.multiVehicleManager.activeVehicle
    property var _flyViewCustomSettings: QGroundControl.corePlugin ? QGroundControl.corePlugin.flyViewCustomSettings : null
    property bool _showHeadingCompassBar: Boolean(_flyViewCustomSettings &&
                                                  _flyViewCustomSettings.showHeadingCompassBar &&
                                                  _flyViewCustomSettings.showHeadingCompassBar.rawValue)
    property real _heading: _activeVehicle ? Number(_activeVehicle.heading.rawValue) : NaN
    property bool _headingValid: isFinite(_heading)
    property real _toolsMargin: ScreenTools.defaultFontPixelWidth * 0.75

    // 罗盘条只占用底部中央区域；关闭时完整透传 QGC 原生 inset。
    QGCToolInsets {
        id: toolInsets
        leftEdgeTopInset:       parentToolInsets.leftEdgeTopInset
        leftEdgeCenterInset:    parentToolInsets.leftEdgeCenterInset
        leftEdgeBottomInset:    parentToolInsets.leftEdgeBottomInset
        rightEdgeTopInset:      parentToolInsets.rightEdgeTopInset
        rightEdgeCenterInset:   parentToolInsets.rightEdgeCenterInset
        rightEdgeBottomInset:   parentToolInsets.rightEdgeBottomInset
        topEdgeLeftInset:       parentToolInsets.topEdgeLeftInset
        topEdgeCenterInset:     parentToolInsets.topEdgeCenterInset
        topEdgeRightInset:      parentToolInsets.topEdgeRightInset
        bottomEdgeLeftInset:    parentToolInsets.bottomEdgeLeftInset
        bottomEdgeCenterInset:  Math.max(parentToolInsets.bottomEdgeCenterInset,
                                         compassBarLoader.visible ? parent.height - compassBarLoader.y : 0)
        bottomEdgeRightInset:   parentToolInsets.bottomEdgeRightInset
    }

    // 新增 QML 未注册到原生 FlightDisplay 模块，使用明确的 custom QRC 地址加载。
    Loader {
        id: compassBarLoader
        active: root.visible && root._showHeadingCompassBar && root._activeVehicle && root._headingValid
        visible: active && status === Loader.Ready
        source: "qrc:/Custom/qml/QGroundControl/FlightDisplay/FlyViewCompassBar.qml"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        // 保持 custom-example 的固定可读宽度。底部角落 inset 是地图重居中的占位提示，
        // 不能取单侧最大值后从左右各扣一次，否则放大 PIP/仪表区时会把罗盘条压到 0。
        // 这里只在整个 Fly View 本身小于首选宽度时收窄，角落控件尺寸不再改变罗盘条宽度。
        anchors.bottomMargin: root._toolsMargin
        width: Math.min(item ? item.implicitWidth : ScreenTools.defaultFontPixelWidth * 50,
                        Math.max(0, parent.width - (root._toolsMargin * 2)))
        height: item ? item.implicitHeight : 0

        onStatusChanged: {
            if (status === Loader.Error) {
                console.warn("Fly View compass bar failed to load:", source)
            }
        }
    }

    Loader {
        id: generatorBusVoltageAlertLoader

        property bool _reloadEnabled: true
        readonly property bool _parametersReady: Boolean(root._activeVehicle &&
                                                          root._activeVehicle.parameterManager &&
                                                          root._activeVehicle.parameterManager.parametersReady)

        function reloadForActiveVehicle() {
            _reloadEnabled = false
            Qt.callLater(() => _reloadEnabled = true)
        }

        active:                   _reloadEnabled && _parametersReady
        anchors.top:              parent.top
        anchors.topMargin:        parentToolInsets.topEdgeCenterInset + ScreenTools.defaultFontPixelHeight * 5
        anchors.horizontalCenter: parent.horizontalCenter
        sourceComponent:          generatorBusVoltageAlertComponent
    }

    Component {
        id: generatorBusVoltageAlertComponent

        GeneratorBusVoltageAlert {
            vehicle: root._activeVehicle
        }
    }

    on_ActiveVehicleChanged: generatorBusVoltageAlertLoader.reloadForActiveVehicle()
}
