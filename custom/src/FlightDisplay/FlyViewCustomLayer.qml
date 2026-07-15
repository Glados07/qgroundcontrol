/****************************************************************************
 *
 * Fly View custom overlay for the fuel-cell bus-voltage warning.
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
    property real _busVoltage: _activeVehicle ? _activeVehicle.generator.busVoltage.rawValue : NaN
    property bool _busVoltageLow: false

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

    // 该扩展不占用固定工具栏区域，完整透传 QGC 原生 inset。
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
        bottomEdgeCenterInset:  parentToolInsets.bottomEdgeCenterInset
        bottomEdgeRightInset:   parentToolInsets.bottomEdgeRightInset
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
