/****************************************************************************
 *
 * 思翼 A8 Mini 私有 SDK 缩放控件。
 * 采用半透明浮层和稳定尺寸，只展示缩放操作与当前倍率。
 *
 ****************************************************************************/

import QtQuick

import QGroundControl
import QGroundControl.Controls
import QGroundControl.ScreenTools

Rectangle {
    id: root

    property var manager: QGroundControl.corePlugin ? QGroundControl.corePlugin.gimbalControlManager : null

    implicitWidth:  ScreenTools.defaultFontPixelWidth * 9.5
    implicitHeight: controlColumn.implicitHeight + panelMargin * 2
    width: implicitWidth
    height: implicitHeight
    radius: Math.min(8, ScreenTools.defaultFontPixelHeight * 0.42)
    color: "#99101822"
    border.color: "#70ffffff"
    border.width: 1

    property real panelMargin: ScreenTools.defaultFontPixelHeight * 0.62

    // 内层高光边缘增加视频画面上的轮廓感，不影响鼠标事件。
    Rectangle {
        anchors.fill: parent
        anchors.margins: 1
        radius: Math.max(0, root.radius - 1)
        color: "transparent"
        border.color: "#24ffffff"
        border.width: 1
    }

    Timer {
        interval: 2000
        repeat: true
        running: root.visible && root.manager && root.manager.enabled
        triggeredOnStart: true
        onTriggered: root.manager.requestCurrentZoom()
    }

    Column {
        id: controlColumn

        anchors.top: parent.top
        anchors.topMargin: root.panelMargin
        anchors.horizontalCenter: parent.horizontalCenter
        width: parent.width - root.panelMargin * 2
        spacing: ScreenTools.defaultFontPixelHeight * 0.58

        Rectangle {
            id: zoomInButton

            anchors.horizontalCenter: parent.horizontalCenter
            width: ScreenTools.defaultFontPixelHeight * 3.45
            height: width
            radius: width / 2
            color: zoomInMouseArea.pressed ? "#dce4ec" : (zoomInMouseArea.containsMouse ? "#ffffff" : "#f2ffffff")
            border.color: zoomInMouseArea.containsMouse ? "#ffffff" : "#a8ffffff"
            border.width: 1
            enabled: Boolean(root.manager && root.manager.enabled)
            opacity: enabled ? 1.0 : 0.55
            scale: zoomInMouseArea.pressed ? 0.95 : 1.0

            Behavior on scale {
                NumberAnimation { duration: 90 }
            }

            Behavior on color {
                ColorAnimation { duration: 100 }
            }

            QGCLabel {
                anchors.centerIn: parent
                text: "+"
                color: "#101820"
                font.bold: true
                font.pixelSize: parent.height * 0.56
            }

            MouseArea {
                id: zoomInMouseArea
                anchors.fill: parent
                enabled: parent.enabled
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.manager.zoomIn()
            }
        }

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: ScreenTools.defaultFontPixelWidth * 5.8
            height: ScreenTools.defaultFontPixelHeight * 1.58
            radius: height / 2
            color: "#e8ffffff"
            border.color: "#80ffffff"
            border.width: 1

            QGCLabel {
                anchors.centerIn: parent
                text: root.manager ? Number(root.manager.currentZoom).toFixed(1) + "x" : "--"
                color: "#101820"
                font.bold: true
                font.pointSize: ScreenTools.smallFontPointSize
            }
        }

        Rectangle {
            id: zoomOutButton

            anchors.horizontalCenter: parent.horizontalCenter
            width: ScreenTools.defaultFontPixelHeight * 3.45
            height: width
            radius: width / 2
            color: zoomOutMouseArea.pressed ? "#dce4ec" : (zoomOutMouseArea.containsMouse ? "#ffffff" : "#f2ffffff")
            border.color: zoomOutMouseArea.containsMouse ? "#ffffff" : "#a8ffffff"
            border.width: 1
            enabled: Boolean(root.manager && root.manager.enabled)
            opacity: enabled ? 1.0 : 0.55
            scale: zoomOutMouseArea.pressed ? 0.95 : 1.0

            Behavior on scale {
                NumberAnimation { duration: 90 }
            }

            Behavior on color {
                ColorAnimation { duration: 100 }
            }

            QGCLabel {
                anchors.centerIn: parent
                text: "-"
                color: "#101820"
                font.bold: true
                font.pixelSize: parent.height * 0.56
            }

            MouseArea {
                id: zoomOutMouseArea
                anchors.fill: parent
                enabled: parent.enabled
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.manager.zoomOut()
            }
        }

    }
}
