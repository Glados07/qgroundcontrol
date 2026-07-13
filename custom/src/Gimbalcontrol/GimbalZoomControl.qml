/****************************************************************************
 *
 * 思翼 A8 Mini 私有 SDK 缩放控件。
 * 控件始终提供稳定尺寸；SDK 未响应时只禁用操作，不隐藏整个界面。
 *
 ****************************************************************************/

import QtQuick

import QGroundControl
import QGroundControl.Controls
import QGroundControl.ScreenTools

Rectangle {
    id: root

    property var manager: QGroundControl.corePlugin ? QGroundControl.corePlugin.gimbalControlManager : null

    implicitWidth:  ScreenTools.defaultFontPixelWidth * 10
    implicitHeight: controlColumn.implicitHeight + panelMargin * 2
    width: implicitWidth
    height: implicitHeight
    radius: ScreenTools.defaultFontPixelHeight * 0.45
    color: "#b8141820"
    border.color: "#80ffffff"
    border.width: 1

    property real panelMargin: ScreenTools.defaultFontPixelHeight * 0.55

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
        spacing: ScreenTools.defaultFontPixelHeight * 0.5

        Rectangle {
            id: zoomInButton

            anchors.horizontalCenter: parent.horizontalCenter
            width: ScreenTools.defaultFontPixelHeight * 3.35
            height: width
            radius: width / 2
            color: zoomInMouseArea.pressed ? "#e7edf4" : "#ffffff"
            border.color: zoomInMouseArea.containsMouse ? "#738195" : "#d5dbe3"
            border.width: Math.max(2, ScreenTools.defaultFontPixelWidth * 0.2)
            enabled: Boolean(root.manager && root.manager.enabled)
            opacity: enabled ? 1.0 : 0.55

            QGCLabel {
                anchors.centerIn: parent
                text: "+"
                color: "#17202a"
                font.bold: true
                font.pixelSize: parent.height * 0.58
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
            width: ScreenTools.defaultFontPixelWidth * 6
            height: ScreenTools.defaultFontPixelHeight * 1.65
            radius: height / 2
            color: "#ffffff"
            border.color: "#d5dbe3"
            border.width: 1

            QGCLabel {
                anchors.centerIn: parent
                text: root.manager ? Number(root.manager.currentZoom).toFixed(1) + "x" : "--"
                color: "#17202a"
                font.bold: true
                font.pointSize: ScreenTools.smallFontPointSize
            }
        }

        Rectangle {
            id: zoomOutButton

            anchors.horizontalCenter: parent.horizontalCenter
            width: ScreenTools.defaultFontPixelHeight * 3.35
            height: width
            radius: width / 2
            color: zoomOutMouseArea.pressed ? "#e7edf4" : "#ffffff"
            border.color: zoomOutMouseArea.containsMouse ? "#738195" : "#d5dbe3"
            border.width: Math.max(2, ScreenTools.defaultFontPixelWidth * 0.2)
            enabled: Boolean(root.manager && root.manager.enabled)
            opacity: enabled ? 1.0 : 0.55

            QGCLabel {
                anchors.centerIn: parent
                text: "-"
                color: "#17202a"
                font.bold: true
                font.pixelSize: parent.height * 0.58
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

        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: ScreenTools.defaultFontPixelWidth * 0.55

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: ScreenTools.defaultFontPixelWidth * 0.8
                height: width
                radius: width / 2
                color: root.manager && root.manager.sdkResponding ? "#22c55e" : "#f59e0b"
            }

            QGCLabel {
                text: root.manager && root.manager.sdkResponding ? qsTr("SDK Ready") : qsTr("SDK Waiting")
                color: "#ffffff"
                font.pointSize: ScreenTools.smallFontPointSize
            }
        }
    }
}
