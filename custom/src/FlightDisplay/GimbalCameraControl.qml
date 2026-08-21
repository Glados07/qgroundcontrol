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
    // Enabled by the MT11 wrapper. Keeping this off makes the A8 Mini control
    // layout and manager contract unchanged.
    property bool thermalControlsVisible: false
    // MT11 exposes both the legal step target and authoritative 0x18
    // feedback. A8 keeps its existing single-value presentation.
    property bool showActualZoom: false

    readonly property bool available: Boolean(manager && manager.enabled)
    readonly property bool online: Boolean(manager && manager.enabled && manager.sdkResponding)
    readonly property real actionSize: Math.max(ScreenTools.defaultFontPixelHeight * 2.35,
                                                ScreenTools.isMobile ? ScreenTools.minTouchPixels : 0)
    readonly property real panelPadding: ScreenTools.defaultFontPixelHeight * 0.48
    readonly property real itemSpacing: ScreenTools.defaultFontPixelWidth * 0.45
    readonly property bool recordingSessionActive: Boolean(manager && manager.recordingSessionActive)
    readonly property bool recordingSessionCapturing: Boolean(manager && manager.recordingSessionCapturing)
    readonly property bool recordingSessionPending: Boolean(manager
                                                              && (manager.cameraRecordingPending
                                                                  || manager.localRecordingPending))
    readonly property bool recordingSessionFailed: recordingSessionActive
                                                   && !recordingSessionCapturing
                                                   && !recordingSessionPending
    readonly property bool recordingSessionVisualActive: recordingSessionActive || recordingSessionCapturing
    readonly property bool mediaErrorVisible: Boolean(manager
                                                       && (recordingSessionFailed
                                                           || manager.localMediaError.length > 0
                                                           || (manager.lastError.length > 0
                                                               && !recordingSessionCapturing)))
    readonly property bool thermalModeKnown: Boolean(thermalControlsVisible
                                                       && manager
                                                       && manager.thermalModeKnown)
    readonly property bool thermalModeEnabled: Boolean(thermalModeKnown
                                                         && manager.thermalModeEnabled)
    readonly property bool thermalCommandPending: Boolean(thermalControlsVisible
                                                            && manager
                                                            && manager.thermalCommandPending)

    property int recordingSeconds: 0

    implicitWidth: controlColumn.implicitWidth + panelPadding * 2
    implicitHeight: controlColumn.implicitHeight + panelPadding * 2
    width: implicitWidth
    height: implicitHeight
    radius: Math.min(10, ScreenTools.defaultFontPixelHeight * 0.55)
    color: online ? "#b0101822" : "#a018202a"
    border.color: mediaErrorVisible
                  ? qgcPal.colorRed
                  : (online ? "#78ffffff" : "#708493a3")
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
        running: root.recordingSessionCapturing
        onTriggered: ++root.recordingSeconds
    }

    Connections {
        target: root.manager

        function onRecordingSessionStateChanged() {
            if (!root.manager || !root.manager.recordingSessionActive) {
                root.recordingSeconds = 0
            }
        }

        function onPhotoCountChanged() {
            photoSuccessFlash.restart()
        }

        function onLocalPhotoCountChanged() {
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
            showActualZoom: root.showActualZoom
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
            id: thermalButton

            readonly property string statusText: root.thermalCommandPending
                                                     ? qsTr("...")
                                                     : (!root.thermalModeKnown
                                                        ? qsTr("?")
                                                        : (root.thermalModeEnabled
                                                           ? qsTr("IR")
                                                           : qsTr("RGB")))

            visible: root.thermalControlsVisible
            Layout.preferredWidth: root.actionSize
            Layout.preferredHeight: root.actionSize
            Layout.alignment: Qt.AlignHCenter
            radius: width / 2
            color: root.thermalModeEnabled
                   ? (thermalMouseArea.pressed ? "#e0a34d1f" : "#c8783014")
                   : (thermalMouseArea.pressed
                      ? "#f0ffffff"
                      : (thermalMouseArea.containsMouse ? "#32ffffff" : "#1cffffff"))
            border.color: root.thermalCommandPending
                          ? "#ffffc857"
                          : (root.thermalModeEnabled
                             ? "#ffffa45b"
                             : (thermalMouseArea.containsMouse ? "#f0ffffff" : "#a8ffffff"))
            border.width: 2
            // The displayed mode changes only when the manager receives and
            // publishes the matching SDK acknowledgement.
            enabled: root.available && root.thermalModeKnown && !root.thermalCommandPending
            opacity: enabled ? 1.0 : 0.55
            scale: thermalMouseArea.pressed ? 0.94 : 1.0

            Behavior on color { ColorAnimation { duration: 120 } }
            Behavior on scale { NumberAnimation { duration: 90 } }

            QGCLabel {
                anchors.centerIn: parent
                text: thermalButton.statusText
                color: thermalMouseArea.pressed && !root.thermalModeEnabled ? "#101820" : "white"
                font.bold: true
                font.pointSize: ScreenTools.smallFontPointSize
            }

            MouseArea {
                id: thermalMouseArea

                anchors.fill: parent
                enabled: parent.enabled
                hoverEnabled: true
                preventStealing: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.manager.toggleThermalMode()
            }
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

            readonly property string statusText: root.recordingSessionPending && !root.recordingSessionCapturing
                                                     ? "..."
                                                     : (root.recordingSessionFailed
                                                        ? qsTr("FAILED")
                                                        : (root.recordingSessionVisualActive
                                                           ? root.recordingTimeText()
                                                           : ""))

            Layout.preferredWidth: statusText.length > 0
                                   ? Math.max(root.actionSize * 1.5,
                                              videoContent.implicitWidth + root.itemSpacing * 2)
                                   : root.actionSize
            Layout.preferredHeight: root.actionSize
            Layout.alignment: Qt.AlignHCenter
            radius: height / 2
            color: root.recordingSessionFailed
                   ? (videoMouseArea.pressed ? "#e06d3514" : "#c85b2d12")
                   : (root.recordingSessionVisualActive
                      ? (videoMouseArea.pressed ? "#e0a31f34" : "#c8a31f34")
                      : (videoMouseArea.pressed ? "#f0ffffff" : (videoMouseArea.containsMouse ? "#32ffffff" : "#1cffffff")))
            border.color: root.recordingSessionFailed
                          ? "#ffffc857"
                          : (root.recordingSessionVisualActive
                             ? "#ffff6b78"
                             : (videoMouseArea.containsMouse ? "#f0ffffff" : "#a8ffffff"))
            border.width: 2
            // The manager coordinates the independent SD and local recording branches.
            enabled: Boolean(root.manager && root.manager.videoRecordingAvailable)
            opacity: enabled ? 1.0 : 0.45
            scale: videoMouseArea.pressed ? 0.94 : 1.0

            Behavior on color { ColorAnimation { duration: 120 } }
            Behavior on scale { NumberAnimation { duration: 90 } }

            Row {
                id: videoContent

                anchors.centerIn: parent
                spacing: videoButton.statusText.length > 0
                         ? ScreenTools.defaultFontPixelWidth * 0.35
                         : 0

                QGCColoredImage {
                    anchors.verticalCenter: parent.verticalCenter
                    width: root.actionSize * 0.48
                    height: width
                    source: "/qmlimages/camera_video.svg"
                    sourceSize.height: height
                    fillMode: Image.PreserveAspectFit
                    color: videoMouseArea.pressed && !root.recordingSessionVisualActive ? "#101820" : "white"
                }

                QGCLabel {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: videoButton.statusText.length > 0
                    text: videoButton.statusText
                    color: videoMouseArea.pressed && !root.recordingSessionVisualActive ? "#101820" : "white"
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
                radius: root.recordingSessionVisualActive && !root.recordingSessionFailed ? 1 : width / 2
                color: root.recordingSessionFailed
                       ? "#ffc857"
                       : (root.recordingSessionVisualActive ? "white" : qgcPal.colorRed)
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

        RowLayout {
            id: recordingStatusRow

            Layout.alignment: Qt.AlignHCenter
            spacing: root.itemSpacing * 0.6

            Rectangle {
                id: sdRecordingBadge

                readonly property bool known: Boolean(root.manager && root.manager.cameraStatusKnown)
                readonly property bool pending: Boolean(root.manager && root.manager.cameraRecordingPending)
                readonly property bool capturing: Boolean(root.manager
                                                           && known
                                                           && !pending
                                                           && root.manager.recording)
                readonly property bool failed: Boolean(root.manager
                                                        && root.recordingSessionActive
                                                        && known
                                                        && !pending
                                                        && !root.manager.recording)
                readonly property color statusColor: capturing
                                                     ? qgcPal.colorGreen
                                                     : (pending
                                                        ? "#ffc857"
                                                        : (failed
                                                           ? qgcPal.colorRed
                                                           : (known ? "#87929d" : "#66727e")))

                Layout.preferredWidth: Math.max(root.actionSize * 0.58,
                                                sdBadgeContent.implicitWidth + root.itemSpacing * 1.2)
                Layout.preferredHeight: Math.max(ScreenTools.defaultFontPixelHeight * 1.05,
                                                 root.actionSize * 0.3)
                radius: height / 2
                color: capturing
                       ? "#3430a060"
                       : (pending ? "#344b3c12" : (failed ? "#344f1721" : "#2818202a"))
                border.color: statusColor
                border.width: 1
                opacity: known || pending ? 1.0 : 0.62

                Row {
                    id: sdBadgeContent

                    anchors.centerIn: parent
                    spacing: root.itemSpacing * 0.35

                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.max(4, root.actionSize * 0.09)
                        height: width
                        radius: width / 2
                        color: sdRecordingBadge.statusColor
                    }

                    QGCLabel {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("SD")
                        color: "white"
                        font.bold: true
                        font.pointSize: ScreenTools.smallFontPointSize
                    }
                }
            }

            Rectangle {
                id: localRecordingBadge

                readonly property bool storageEnabled: Boolean(root.manager && root.manager.localMediaStorageEnabled)
                readonly property bool pending: Boolean(root.manager && root.manager.localRecordingPending)
                readonly property bool capturing: Boolean(root.manager && root.manager.localRecording)
                readonly property bool failed: Boolean(storageEnabled
                                                       && root.manager
                                                       && root.manager.localMediaError.length > 0
                                                       && !capturing
                                                       && !pending)
                readonly property color statusColor: capturing
                                                     ? qgcPal.colorGreen
                                                     : (pending
                                                        ? "#ffc857"
                                                        : (!storageEnabled
                                                           ? "#66727e"
                                                           : (failed ? qgcPal.colorRed : "#87929d")))

                Layout.preferredWidth: Math.max(root.actionSize * 0.86,
                                                localBadgeContent.implicitWidth + root.itemSpacing * 1.2)
                Layout.preferredHeight: Math.max(ScreenTools.defaultFontPixelHeight * 1.05,
                                                 root.actionSize * 0.3)
                radius: height / 2
                color: capturing
                       ? "#3430a060"
                       : (pending
                          ? "#344b3c12"
                          : (!storageEnabled ? "#2018202a" : (failed ? "#344f1721" : "#2818202a")))
                border.color: statusColor
                border.width: 1
                opacity: storageEnabled || capturing || pending ? 1.0 : 0.62

                Row {
                    id: localBadgeContent

                    anchors.centerIn: parent
                    spacing: root.itemSpacing * 0.35

                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.max(4, root.actionSize * 0.09)
                        height: width
                        radius: width / 2
                        color: localRecordingBadge.statusColor
                    }

                    QGCLabel {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("LOCAL")
                        color: "white"
                        font.bold: true
                        font.pointSize: ScreenTools.smallFontPointSize
                    }
                }
            }
        }
    }
}
