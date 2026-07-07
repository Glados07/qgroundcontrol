/****************************************************************************
 *
 * Fly View 左侧思翼云台相机缩放控件。
 * 控件只负责 UI 和点击事件，缩放范围、分度值和 UDP 发送由 C++ Manager 统一处理。
 *
 ****************************************************************************/

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.Palette
import QGroundControl.ScreenTools

Item {
    id: root

    width:  ScreenTools.defaultFontPixelWidth * 7
    height: buttonColumn.implicitHeight

    property var manager:  QGroundControl.corePlugin.gimbalControlManager
    property var settings: QGroundControl.corePlugin.gimbalControlSettings

    visible: manager && settings &&
             settings.enabled.rawValue &&
             QGroundControl.multiVehicleManager.activeVehicle &&
             !QGroundControl.videoManager.fullScreen

    QGCPalette { id: qgcPal }

    Timer {
        interval: 2000
        repeat: true
        running: root.visible && root.manager && root.settings.enabled.rawValue
        triggeredOnStart: true
        onTriggered: root.manager.requestCurrentZoom()
    }

    Column {
        id: buttonColumn
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: ScreenTools.defaultFontPixelHeight * 0.55

        ZoomRoundButton {
            buttonText: "+"
            onClicked: root.manager ? root.manager.zoomIn() : undefined
        }

        ZoomRoundButton {
            buttonText: "-"
            onClicked: root.manager ? root.manager.zoomOut() : undefined
        }

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width:  ScreenTools.defaultFontPixelWidth * 5.2
            height: ScreenTools.defaultFontPixelHeight * 1.55
            radius: height / 2
            color:  "#f7f9fc"
            border.color: "#d8dde6"
            border.width: 1
            opacity: 0.96

            QGCLabel {
                anchors.centerIn: parent
                text: root.manager ? Number(root.manager.currentZoom).toFixed(1) + "x" : "--"
                color: "#1f2937"
                font.bold: true
                font.pointSize: ScreenTools.smallFontPointSize
            }
        }
    }

    component ZoomRoundButton: Rectangle {
        id: zoomButton

        signal clicked
        property string buttonText: "+"

        anchors.horizontalCenter: parent.horizontalCenter
        width:  ScreenTools.defaultFontPixelHeight * 3.3
        height: width
        radius: width / 2
        color: mouseArea.pressed ? "#e7edf6" : "#ffffff"
        border.color: "#d8dde6"
        border.width: Math.max(2, ScreenTools.defaultFontPixelWidth * 0.22)
        opacity: enabled ? 0.98 : 0.45
        enabled: root.manager && root.settings && root.settings.enabled.rawValue

        Text {
            anchors.centerIn: parent
            text: zoomButton.buttonText
            color: "#18212c"
            font.bold: true
            font.pixelSize: parent.height * 0.62
        }

        MouseArea {
            id: mouseArea
            anchors.fill: parent
            hoverEnabled: true
            onClicked: zoomButton.clicked()
        }
    }
}
