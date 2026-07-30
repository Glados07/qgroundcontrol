/****************************************************************************
 *
 * Fly View思翼相机合并控制栏。
 * 缩放、拍照和录像纵向排列并复用QGC图标；全部命令均不依赖activeVehicle。
 *
 ****************************************************************************/

import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.Palette
import QGroundControl.ScreenTools

Rectangle {
    id: root

    property var manager: QGroundControl.corePlugin ? QGroundControl.corePlugin.gimbalControlManager : null

    readonly property bool available: Boolean(manager && manager.enabled)
    readonly property bool online: Boolean(manager && manager.enabled && manager.sdkResponding)
    readonly property real actionSize: Math.max(ScreenTools.defaultFontPixelHeight * 2.35,
                                                ScreenTools.isMobile ? ScreenTools.minTouchPixels : 0)
    readonly property real panelPadding: ScreenTools.defaultFontPixelHeight * 0.48
    readonly property real itemSpacing: ScreenTools.defaultFontPixelWidth * 0.45

    property int recordingSeconds: 0

    implicitWidth: controlColumn.implicitWidth + panelPadding * 2
    implicitHeight: controlColumn.implicitHeight + panelPadding * 2
    width: implicitWidth
    height: implicitHeight
    radius: Math.min(10, ScreenTools.defaultFontPixelHeight * 0.55)
    color: online ? "#b0101822" : "#a018202a"
    border.color: online
                  ? (manager && manager.lastError.length > 0 ? qgcPal.colorRed : "#78ffffff")
                  : "#708493a3"
    border.width: 1
    visible: available

    function recordingTimeText() {
        const hours = Math.floor(recordingSeconds / 3600)
        const minutes = Math.floor((recordingSeconds % 3600) / 60)
        const seconds = recordingSeconds % 60
        const mm = (minutes < 10 ? "0" : "") + minutes
        const ss = (seconds < 10 ? "0" : "") + seconds
        return hours > 0 ? hours + ":" + mm + ":" + ss : mm + ":" + ss
    }

    QGCPalette {
        id: qgcPal
        colorGroupEnabled: root.enabled
    }

    DeadMouseArea { anchors.fill: parent }

    Rectangle {
        anchors.fill: parent
        anchors.margins: 1
        radius: Math.max(0, root.radius - 1)
        color: "transparent"
        border.color: "#20ffffff"
        border.width: 1
    }

    // 常驻控制栏的SDK状态提示：离线时状态点灰显，但控制栏不会从布局中消失。
    Rectangle {
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.topMargin: root.panelPadding * 0.35
        anchors.rightMargin: root.panelPadding * 0.35
        width: Math.max(5, root.actionSize * 0.12)
        height: width
        radius: width / 2
        color: root.online ? qgcPal.colorGreen : "#87929d"
        border.color: "#b8ffffff"
        border.width: 1
        z: 2
    }

    Timer {
        interval: 1000
        repeat: true
        running: Boolean(root.manager
                         && root.manager.cameraStatusKnown
                         && !root.manager.recordingCommandPending
                         && root.manager.recording)
        onTriggered: ++root.recordingSeconds
    }

    Connections {
        target: root.manager

        function onRecordingChanged() {
            root.recordingSeconds = 0
        }

        function onPhotoCountChanged() {
            photoSuccessFlash.restart()
        }
    }

    ColumnLayout {
        id: controlColumn

        anchors.fill: parent
        anchors.margins: root.panelPadding
        spacing: root.itemSpacing

        GimbalZoomControl {
            id: zoomControl

            manager: root.manager
            controlSize: root.actionSize
            controlSpacing: root.itemSpacing
            Layout.alignment: Qt.AlignHCenter
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            Layout.leftMargin: root.itemSpacing
            Layout.rightMargin: root.itemSpacing
            Layout.alignment: Qt.AlignHCenter
            color: "#56ffffff"
        }

        Rectangle {
            id: photoButton

            Layout.preferredWidth: root.actionSize
            Layout.preferredHeight: root.actionSize
            Layout.alignment: Qt.AlignHCenter
            radius: width / 2
            color: photoMouseArea.pressed ? "#f0ffffff" : (photoMouseArea.containsMouse ? "#32ffffff" : "#1cffffff")
            border.color: photoMouseArea.containsMouse ? "#f0ffffff" : "#a8ffffff"
            border.width: 2
            // UDP 命令发送能力不依赖最近一次状态探测，避免偶发回包超时锁死拍照。
            enabled: root.available
            opacity: enabled ? 1.0 : 0.45
            scale: photoMouseArea.pressed ? 0.94 : 1.0

            Behavior on color { ColorAnimation { duration: 100 } }
            Behavior on scale { NumberAnimation { duration: 90 } }

            QGCColoredImage {
                anchors.centerIn: parent
                width: parent.width * 0.48
                height: width
                source: "/qmlimages/camera_photo.svg"
                sourceSize.height: height
                fillMode: Image.PreserveAspectFit
                color: photoMouseArea.pressed ? "#101820" : "white"
            }

            Rectangle {
                anchors.fill: parent
                anchors.margins: -2
                radius: width / 2
                color: "transparent"
                border.color: qgcPal.colorGreen
                border.width: 3
                opacity: 0

                SequentialAnimation on opacity {
                    id: photoSuccessFlash
                    running: false
                    NumberAnimation { to: 1; duration: 90 }
                    PauseAnimation { duration: 180 }
                    NumberAnimation { to: 0; duration: 240 }
                }
            }

            MouseArea {
                id: photoMouseArea

                anchors.fill: parent
                enabled: parent.enabled
                hoverEnabled: true
                preventStealing: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.manager.takePhoto()
            }
        }

        Rectangle {
            id: videoButton

            Layout.preferredWidth: Math.max(root.actionSize * 1.5,
                                            videoContent.implicitWidth + root.itemSpacing * 2)
            Layout.preferredHeight: root.actionSize
            Layout.alignment: Qt.AlignHCenter
            radius: height / 2
            color: root.manager && root.manager.recording
                   ? (videoMouseArea.pressed ? "#e0a31f34" : "#c8a31f34")
                   : (videoMouseArea.pressed ? "#f0ffffff" : (videoMouseArea.containsMouse ? "#32ffffff" : "#1cffffff"))
            border.color: root.manager && root.manager.recording
                          ? "#ffff6b78"
                          : (videoMouseArea.containsMouse ? "#f0ffffff" : "#a8ffffff")
            border.width: 2
            // 录像是 toggle 命令，仍需先同步当前录像状态以避免反向操作。
            enabled: Boolean(root.available
                             && root.manager
                             && root.manager.cameraStatusKnown
                             && !root.manager.recordingCommandPending)
            opacity: enabled ? 1.0 : 0.45
            scale: videoMouseArea.pressed ? 0.96 : 1.0

            Behavior on color { ColorAnimation { duration: 120 } }
            Behavior on scale { NumberAnimation { duration: 90 } }

            Row {
                id: videoContent

                anchors.centerIn: parent
                spacing: ScreenTools.defaultFontPixelWidth * 0.35

                QGCColoredImage {
                    anchors.verticalCenter: parent.verticalCenter
                    width: root.actionSize * 0.4
                    height: width
                    source: "/qmlimages/camera_video.svg"
                    sourceSize.height: height
                    fillMode: Image.PreserveAspectFit
                    color: videoMouseArea.pressed && !(root.manager && root.manager.recording) ? "#101820" : "white"
                }

                QGCLabel {
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.manager
                          && (!root.manager.cameraStatusKnown || root.manager.recordingCommandPending)
                          ? "..."
                          : (root.manager && root.manager.recording ? root.recordingTimeText() : qsTr("REC"))
                    color: videoMouseArea.pressed && !(root.manager && root.manager.recording) ? "#101820" : "white"
                    font.bold: true
                    font.pointSize: ScreenTools.smallFontPointSize
                }
            }

            Rectangle {
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.rightMargin: parent.width * 0.13
                anchors.bottomMargin: parent.height * 0.12
                width: Math.max(5, parent.height * 0.14)
                height: width
                radius: root.manager && root.manager.recording ? 1 : width / 2
                color: root.manager && root.manager.recording ? "white" : qgcPal.colorRed
            }

            MouseArea {
                id: videoMouseArea

                anchors.fill: parent
                enabled: parent.enabled
                hoverEnabled: true
                preventStealing: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.manager.toggleVideoRecording()
            }
        }
    }
}
