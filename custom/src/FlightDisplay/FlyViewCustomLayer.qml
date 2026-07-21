/****************************************************************************
 *
 * Fly View custom overlay for the heading compass bar and fuel-cell warning.
 *
 ****************************************************************************/

import QtQuick

import QGroundControl
import QGroundControl.Controls
import QGroundControl.Palette
import QGroundControl.ScreenTools

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
    property real _busVoltage: _activeVehicle ? _activeVehicle.generator.busVoltage.rawValue : NaN
    property bool _busVoltageLow: false
    property real _toolsMargin: ScreenTools.defaultFontPixelWidth * 0.75
    property real _horizontalSafetyInset: Math.max(parentToolInsets.leftEdgeBottomInset,
                                                   parentToolInsets.rightEdgeBottomInset)

    QGCPalette {
        id: qgcPal
    }

    // 使用回差避免母线电压在阈值附近波动时告警反复闪烁。
    QtObject {
        property real voltage: root._busVoltage

        onVoltageChanged: {
            if (isNaN(voltage)) {
                root._busVoltageLow = false
            } else if (voltage < 20.0) {
                root._busVoltageLow = true
            } else if (voltage > 20.4) {
                root._busVoltageLow = false
            }
        }
    }

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
        // 与 custom-example 一致固定在飞行界面底边；左右 inset 负责避让 PIP、摇杆和仪表区。
        anchors.bottomMargin: root._toolsMargin
        width: Math.min(ScreenTools.defaultFontPixelWidth * 50,
                        Math.max(0, parent.width - ((root._horizontalSafetyInset + root._toolsMargin) * 2)))
        height: item ? item.implicitHeight : 0

        onStatusChanged: {
            if (status === Loader.Error) {
                console.warn("Fly View compass bar failed to load:", source)
            }
        }
    }

    Rectangle {
        id: busVoltageAlert
        visible: root._busVoltageLow
        anchors.top: parent.top
        anchors.topMargin: parentToolInsets.topEdgeCenterInset + ScreenTools.defaultFontPixelHeight * 5
        anchors.horizontalCenter: parent.horizontalCenter
        width: warningLabel.implicitWidth + ScreenTools.defaultFontPixelWidth * 3
        height: warningLabel.implicitHeight + ScreenTools.defaultFontPixelHeight
        radius: ScreenTools.defaultFontPixelHeight * 0.5
        color: qgcPal.alertBackground
        border.color: qgcPal.alertBorder
        border.width: 2

        QGCLabel {
            id: warningLabel
            anchors.centerIn: parent
            text: qsTr("Generator Bus Voltage Low: %1 V").arg(root._busVoltage.toFixed(1))
            color: qgcPal.alertText
            font.bold: true
            font.pointSize: ScreenTools.mediumFontPointSize
        }
    }
}
