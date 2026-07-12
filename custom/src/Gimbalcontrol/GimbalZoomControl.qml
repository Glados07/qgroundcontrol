/****************************************************************************
 *
 * 思翼 A8 Mini 私有 SDK 缩放控件。
 * QML 仅负责界面、轮询和点击事件；倍率限制、分度值和 UDP 协议由 C++ 处理。
 *
 ****************************************************************************/

import QtQuick
import QtQuick.Controls

import QGroundControl
import QGroundControl.Controls
import QGroundControl.ScreenTools

Rectangle {
    id: root

    property var manager:       QGroundControl.corePlugin ? QGroundControl.corePlugin.gimbalControlManager : null
    property var settings:      QGroundControl.corePlugin ? QGroundControl.corePlugin.gimbalControlSettings : null
    property var activeVehicle: QGroundControl.multiVehicleManager.activeVehicle

    width:  ScreenTools.defaultFontPixelWidth * 10
    height: controlColumn.implicitHeight + panelMargin * 2
    radius: ScreenTools.defaultFontPixelHeight * 0.45
    color:  Qt.rgba(0.05, 0.07, 0.09, 0.58)

    property real panelMargin: ScreenTools.defaultFontPixelHeight * 0.55

    visible: Boolean(manager &&
                     settings &&
                     settings.enabled &&
                     settings.enabled.rawValue &&
                     activeVehicle &&
                     !QGroundControl.videoManager.fullScreen)

    Timer {
        interval: 2000
        repeat: true
        running: root.visible && root.manager
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

        ZoomRoundButton {
            buttonText: "+"
            accessibleName: qsTr("Zoom in")
            onClicked: root.manager.zoomIn()
        }

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width:  ScreenTools.defaultFontPixelWidth * 6
            height: ScreenTools.defaultFontPixelHeight * 1.65
            radius: height / 2
            color:  "#ffffff"
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

        ZoomRoundButton {
            buttonText: "-"
            accessibleName: qsTr("Zoom out")
            onClicked: root.manager.zoomOut()
        }

        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: ScreenTools.defaultFontPixelWidth * 0.55

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width:  ScreenTools.defaultFontPixelWidth * 0.8
                height: width
                radius: width / 2
                color: root.manager && root.manager.sdkResponding ? "#22c55e" : "#f59e0b"
            }

            QGCLabel {
                text: "SDK"
                color: "#ffffff"
                font.pointSize: ScreenTools.smallFontPointSize
            }
        }
    }

    component ZoomRoundButton: Rectangle {
        id: zoomButton

        signal clicked
        property string buttonText: "+"
        property string accessibleName: ""

        anchors.horizontalCenter: parent.horizontalCenter
        width:  ScreenTools.defaultFontPixelHeight * 3.35
        height: width
        radius: width / 2
        color: mouseArea.pressed ? "#e7edf4" : "#ffffff"
        border.color: mouseArea.containsMouse ? "#8b98a8" : "#d5dbe3"
        border.width: Math.max(2, ScreenTools.defaultFontPixelWidth * 0.2)
        enabled: Boolean(root.manager && root.manager.enabled)
        opacity: enabled ? 1.0 : 0.45

        Accessible.name: accessibleName
        Accessible.role: Accessible.Button

        QGCLabel {
            anchors.centerIn: parent
            text: zoomButton.buttonText
            color: "#17202a"
            font.bold: true
            font.pixelSize: parent.height * 0.58
        }

        MouseArea {
            id: mouseArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: zoomButton.clicked()
        }

        ToolTip.visible: mouseArea.containsMouse
        ToolTip.text: accessibleName
    }
}
