/****************************************************************************
 *
 * 合并相机控制栏中的思翼A8 Mini缩放子控件。
 * 短按按配置步长单步缩放；长按启动相机原生连续缩放，释放或取消时停止。
 *
 ****************************************************************************/

import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.ScreenTools

Item {
    id: root

    property var manager: QGroundControl.corePlugin ? QGroundControl.corePlugin.gimbalControlManager : null
    property real controlSize: Math.max(ScreenTools.defaultFontPixelHeight * 2.35,
                                        ScreenTools.isMobile ? ScreenTools.minTouchPixels : 0)
    property real controlSpacing: ScreenTools.defaultFontPixelWidth * 0.45

    readonly property bool online: Boolean(manager && manager.enabled && manager.sdkResponding)
    readonly property real zoomValue: manager ? Number(manager.currentZoom) : 1.0

    implicitWidth: zoomRow.implicitWidth
    implicitHeight: controlSize

    function cancelContinuousZoom() {
        if (manager && manager.continuousZoomActive) {
            manager.stopZoom()
        }
    }

    onOnlineChanged: {
        if (!online) {
            cancelContinuousZoom()
        }
    }
    onVisibleChanged: {
        if (!visible) {
            cancelContinuousZoom()
        }
    }
    Component.onDestruction: cancelContinuousZoom()

    Connections {
        target: Qt.application

        function onStateChanged() {
            if (Qt.application.state !== Qt.ApplicationActive) {
                root.cancelContinuousZoom()
            }
        }
    }

    RowLayout {
        id: zoomRow

        anchors.fill: parent
        spacing: root.controlSpacing

        Rectangle {
            id: zoomOutButton

            Layout.preferredWidth: root.controlSize
            Layout.preferredHeight: root.controlSize
            radius: width / 2
            color: zoomOutMouseArea.pressed ? "#f2ffffff" : (zoomOutMouseArea.containsMouse ? "#32ffffff" : "#1cffffff")
            border.color: zoomOutMouseArea.containsMouse ? "#d8ffffff" : "#78ffffff"
            border.width: 1
            enabled: root.online
                     && root.zoomValue > (root.manager.minimumZoom + 0.05)
                     && (!root.manager.continuousZoomActive || zoomOutMouseArea.pressed)
            opacity: enabled ? 1.0 : 0.38
            scale: zoomOutMouseArea.pressed ? 0.94 : 1.0

            Behavior on color { ColorAnimation { duration: 100 } }
            Behavior on scale { NumberAnimation { duration: 90 } }

            QGCLabel {
                anchors.centerIn: parent
                text: "\u2212"
                color: zoomOutMouseArea.pressed ? "#101820" : "white"
                font.bold: true
                font.pixelSize: parent.height * 0.48
            }

            MouseArea {
                id: zoomOutMouseArea

                anchors.fill: parent
                enabled: parent.enabled
                hoverEnabled: true
                preventStealing: true
                cursorShape: Qt.PointingHandCursor
                pressAndHoldInterval: 420

                property bool holdTriggered: false
                property bool continuousStarted: false

                onPressed: {
                    holdTriggered = false
                    continuousStarted = false
                }
                onPressAndHold: {
                    holdTriggered = true
                    continuousStarted = root.manager.startZoom(-1)
                }
                onReleased: {
                    if (holdTriggered) {
                        if (continuousStarted || root.manager.continuousZoomActive) {
                            root.manager.stopZoom()
                        }
                    } else if (containsMouse) {
                        root.manager.zoomOut()
                    }
                    holdTriggered = false
                    continuousStarted = false
                }
                onCanceled: {
                    if (continuousStarted || (root.manager && root.manager.continuousZoomActive)) {
                        root.manager.stopZoom()
                    }
                    holdTriggered = false
                    continuousStarted = false
                }
                onExited: {
                    if (pressed && continuousStarted) {
                        holdTriggered = true
                        root.manager.stopZoom()
                        continuousStarted = false
                    }
                }
            }
        }

        Rectangle {
            Layout.preferredWidth: ScreenTools.defaultFontPixelWidth * 5.2
            Layout.preferredHeight: root.controlSize * 0.78
            Layout.alignment: Qt.AlignVCenter
            radius: height / 2
            color: "#e8ffffff"
            border.color: "#80ffffff"
            border.width: 1

            QGCLabel {
                anchors.centerIn: parent
                text: root.manager ? root.zoomValue.toFixed(1) + "x" : "--"
                color: "#101820"
                font.bold: true
                font.pointSize: ScreenTools.smallFontPointSize
            }
        }

        Rectangle {
            id: zoomInButton

            Layout.preferredWidth: root.controlSize
            Layout.preferredHeight: root.controlSize
            radius: width / 2
            color: zoomInMouseArea.pressed ? "#f2ffffff" : (zoomInMouseArea.containsMouse ? "#32ffffff" : "#1cffffff")
            border.color: zoomInMouseArea.containsMouse ? "#d8ffffff" : "#78ffffff"
            border.width: 1
            enabled: root.online
                     && root.zoomValue < (root.manager.maximumZoom - 0.05)
                     && (!root.manager.continuousZoomActive || zoomInMouseArea.pressed)
            opacity: enabled ? 1.0 : 0.38
            scale: zoomInMouseArea.pressed ? 0.94 : 1.0

            Behavior on color { ColorAnimation { duration: 100 } }
            Behavior on scale { NumberAnimation { duration: 90 } }

            QGCLabel {
                anchors.centerIn: parent
                text: "+"
                color: zoomInMouseArea.pressed ? "#101820" : "white"
                font.bold: true
                font.pixelSize: parent.height * 0.48
            }

            MouseArea {
                id: zoomInMouseArea

                anchors.fill: parent
                enabled: parent.enabled
                hoverEnabled: true
                preventStealing: true
                cursorShape: Qt.PointingHandCursor
                pressAndHoldInterval: 420

                property bool holdTriggered: false
                property bool continuousStarted: false

                onPressed: {
                    holdTriggered = false
                    continuousStarted = false
                }
                onPressAndHold: {
                    holdTriggered = true
                    continuousStarted = root.manager.startZoom(1)
                }
                onReleased: {
                    if (holdTriggered) {
                        if (continuousStarted || root.manager.continuousZoomActive) {
                            root.manager.stopZoom()
                        }
                    } else if (containsMouse) {
                        root.manager.zoomIn()
                    }
                    holdTriggered = false
                    continuousStarted = false
                }
                onCanceled: {
                    if (continuousStarted || (root.manager && root.manager.continuousZoomActive)) {
                        root.manager.stopZoom()
                    }
                    holdTriggered = false
                    continuousStarted = false
                }
                onExited: {
                    if (pressed && continuousStarted) {
                        holdTriggered = true
                        root.manager.stopZoom()
                        continuousStarted = false
                    }
                }
            }
        }
    }
}
