/****************************************************************************
 *
 * Fly View custom overlay for the fuel-cell bus-voltage warning.
 *
 ****************************************************************************/

import QtQuick

import QGroundControl
import QGroundControl.Controls
import QGroundControl.ScreenTools

import Custom.Widgets

Item {
    property var parentToolInsets
    property var totalToolInsets: toolInsets
    property var mapControl

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

    GeneratorBusVoltageAlert {
        anchors.top:              parent.top
        anchors.topMargin:        parentToolInsets.topEdgeCenterInset + ScreenTools.defaultFontPixelHeight * 5
        anchors.horizontalCenter: parent.horizontalCenter
        vehicle:                  QGroundControl.multiVehicleManager.activeVehicle
    }
}
