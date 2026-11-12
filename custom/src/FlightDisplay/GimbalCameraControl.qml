/****************************************************************************
 *
 * Fly View思翼相机合并控制栏。
 * 缩放、拍照和录像使用双列触控布局；在线门禁独立于activeVehicle。
 *
 ****************************************************************************/

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QtControls
import QtQuick.Window

import QGroundControl
import QGroundControl.Controls
import QGroundControl.ScreenTools

Rectangle {
    id: root

    property var manager: QGroundControl.corePlugin ? QGroundControl.corePlugin.gimbalControlManager : null
    // Enabled by the MT11 wrapper. Keeping this off makes the A8 Mini control
    // layout and manager contract unchanged.
    property bool thermalControlsVisible: false
    // Optional diagnostic presentation. Product camera wrappers keep this
    // disabled so the normal control bar shows one target value consistently.
    property bool showActualZoom: false

    readonly property bool available: Boolean(manager && manager.enabled)
    readonly property bool online: Boolean(manager && manager.enabled && manager.sdkResponding)
    readonly property real actionSize: Math.max(ScreenTools.defaultFontPixelHeight * 2.35,
                                                ScreenTools.isMobile ? ScreenTools.minTouchPixels : 0)
    readonly property real cellWidth: Math.max(ScreenTools.defaultFontPixelHeight * 2.25,
                                                ScreenTools.isMobile ? ScreenTools.minTouchPixels : 0)
    readonly property real panelPadding: ScreenTools.defaultFontPixelHeight * 0.22
    readonly property real itemSpacing: panelPadding
    readonly property color accentColor: "#176b83"
    readonly property color panelColor: "#f5f7fa"
    readonly property color panelBorderColor: "#cbd5df"
    readonly property color innerBorderColor: "#ffffff"
    readonly property color controlBorderColor: "transparent"
    readonly property color popupColor: "#f5f7fa"
    readonly property color buttonColor: "transparent"
    readonly property color buttonHoverColor: "transparent"
    readonly property color buttonPressedColor: "#c5e1e9"
    readonly property color iconColor: "#152733"
    readonly property color hoveredIconColor: "#176b83"
    readonly property color pressedIconColor: "#152733"
    readonly property real actionIconSize: ScreenTools.defaultFontPixelHeight * 1.2
    readonly property real modeIconSize: actionIconSize
    readonly property bool recordingSessionActive: Boolean(manager && manager.recordingSessionActive)
    readonly property bool recordingSessionCapturing: Boolean(manager && manager.recordingSessionCapturing)
    readonly property bool recordingSessionPending: Boolean(manager
                                                              && (manager.cameraRecordingPending
                                                                  || manager.localRecordingPending))
    readonly property bool recordingSessionFailed: recordingSessionActive
                                                   && !recordingSessionCapturing
                                                   && !recordingSessionPending
    readonly property bool recordingSessionVisualActive: recordingSessionActive || recordingSessionCapturing
    readonly property bool videoModeKnown: Boolean(thermalControlsVisible
                                                     && manager
                                                     && manager.videoModeKnown)
    readonly property int videoMode: videoModeKnown ? Number(manager.videoMode) : -1
    readonly property bool videoModePending: Boolean(thermalControlsVisible
                                                      && manager
                                                      && manager.videoModePending)

    property int recordingSeconds: 0
    readonly property bool videoModeMenuOpen: videoModeMenu.visible

    implicitWidth: cellWidth * 2 + itemSpacing + panelPadding * 2
    implicitHeight: controlColumn.implicitHeight + panelPadding * 2
    width: implicitWidth
    height: implicitHeight
    radius: ScreenTools.defaultFontPixelHeight * 0.55
    color: panelColor
    border.color: panelBorderColor
    border.width: 1
    visible: available

    function videoModeCode(mode) {
        if (mode === 0) {
            return qsTr("ZOOM")
        }
        if (mode === 2) {
            return qsTr("IR")
        }
        if (mode === 3) {
            return qsTr("MIX")
        }
        return "?"
    }

    function videoModeName(mode) {
        if (mode === 0) {
            return qsTr("Zoom")
        }
        if (mode === 2) {
            return qsTr("Thermal")
        }
        if (mode === 3) {
            return qsTr("Zoom + Thermal")
        }
        return qsTr("Unknown mode")
    }

    function requestVideoMode(mode) {
        if (!manager || !online || videoModePending || mode === videoMode) {
            closeTransientUi()
            return
        }

        const accepted = manager.setVideoMode(mode)
        if (accepted !== false) {
            closeTransientUi()
        }
    }

    function openVideoModeMenu() {
        if (!thermalControlsVisible || !online || videoModePending) {
            return
        }

        videoModeMenu.reposition()
        videoModeMenu.open()
    }

    function closeTransientUi() {
        videoModeMenu.close()
    }

    function recordingTimeText() {
        const hours = Math.floor(recordingSeconds / 3600)
        const minutes = Math.floor((recordingSeconds % 3600) / 60)
        const seconds = recordingSeconds % 60
        const mm = (minutes < 10 ? "0" : "") + minutes
        const ss = (seconds < 10 ? "0" : "") + seconds
        return hours > 0 ? hours + ":" + mm + ":" + ss : mm + ":" + ss
    }

    onVisibleChanged: {
        if (!visible) {
            closeTransientUi()
        }
    }

    onOnlineChanged: {
        if (!online) {
            closeTransientUi()
        }
    }

    onManagerChanged: closeTransientUi()
    onThermalControlsVisibleChanged: {
        if (!thermalControlsVisible) {
            closeTransientUi()
        }
    }
    onVideoModePendingChanged: {
        if (videoModePending) {
            closeTransientUi()
        }
    }

    Component.onDestruction: closeTransientUi()

    // Block map clicks, but allow the host Flickable to take scrolling gestures.
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        preventStealing: false
        onWheel: function(wheel) { wheel.accepted = false }
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: 1
        radius: Math.max(0, root.radius - 1)
        color: "transparent"
        border.color: root.innerBorderColor
        border.width: 1
    }

    // 常驻控制栏的SDK状态提示：离线时状态点灰显，但控制栏不会从布局中消失。
    Rectangle {
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.topMargin: root.panelPadding * 0.35
        width: Math.max(5, root.actionSize * 0.12)
        height: width
        radius: width / 2
        color: root.online ? root.accentColor : "#8795a3"
        border.width: 0
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

    Connections {
        target: Qt.application

        function onStateChanged() {
            if (Qt.application.state !== Qt.ApplicationActive) {
                root.closeTransientUi()
            }
        }
    }

    Connections {
        id: windowVisibilityConnections

        target: root.Window.window
        ignoreUnknownSignals: true

        function onVisibleChanged() {
            if (!windowVisibilityConnections.target
                    || !windowVisibilityConnections.target.visible) {
                root.closeTransientUi()
            }
        }
    }

    QtControls.Popup {
        id: videoModeMenu

        readonly property real menuPadding: Math.max(5, root.panelPadding * 0.8)
        readonly property real edgeMargin: Math.max(8, root.panelPadding * 1.2)
        readonly property real popupGap: root.itemSpacing * 1.5
        property real lastClosedAtMs: -1000

        parent: QtControls.Overlay.overlay
        width: root.actionSize + menuPadding * 2
        height: root.actionSize * 3 + root.itemSpacing * 2 + menuPadding * 2
        padding: menuPadding
        z: 100
        modal: false
        focus: true
        dim: false
        closePolicy: QtControls.Popup.CloseOnEscape | QtControls.Popup.CloseOnPressOutside

        function bounded(value, minimum, maximum) {
            return Math.max(minimum, Math.min(maximum, value))
        }

        function reposition() {
            if (!parent || !modeButton) {
                return
            }

            const buttonPosition = modeButton.mapToItem(parent, 0, 0)
            const maximumX = Math.max(edgeMargin, parent.width - width - edgeMargin)
            const maximumY = Math.max(edgeMargin, parent.height - height - edgeMargin)
            const leftX = buttonPosition.x - width - popupGap

            if (leftX >= edgeMargin) {
                x = bounded(leftX, edgeMargin, maximumX)
                y = bounded(buttonPosition.y + modeButton.height / 2 - height / 2,
                            edgeMargin, maximumY)
                return
            }

            // Narrow portrait screens may not have enough room on the left.
            // Align the menu with the button and prefer below, then above.
            x = bounded(buttonPosition.x + modeButton.width - width,
                        edgeMargin, maximumX)
            const belowY = buttonPosition.y + modeButton.height + popupGap
            if (belowY + height <= parent.height - edgeMargin) {
                y = belowY
            } else {
                y = bounded(buttonPosition.y - height - popupGap,
                            edgeMargin, maximumY)
            }
        }

        onOpened: reposition()
        onClosed: lastClosedAtMs = Date.now()

        enter: Transition {
            NumberAnimation { property: "opacity"; from: 0.0; to: 1.0; duration: 120 }
            NumberAnimation { property: "scale"; from: 0.96; to: 1.0; duration: 120; easing.type: Easing.OutCubic }
        }

        exit: Transition {
            NumberAnimation { property: "opacity"; from: 1.0; to: 0.0; duration: 90 }
        }

        background: Rectangle {
            radius: Math.min(12, root.actionSize * 0.22)
            color: root.popupColor
            border.color: root.videoModePending ? "#96621d" : root.panelBorderColor
            border.width: 1

            Rectangle {
                anchors.fill: parent
                anchors.margins: 1
                radius: Math.max(0, parent.radius - 1)
                color: "transparent"
                border.color: root.innerBorderColor
                border.width: 1
            }
        }

        contentItem: Column {
            id: videoModeMenuColumn

            spacing: root.itemSpacing

            Repeater {
                model: [0, 2, 3]

                Rectangle {
                    id: modeOption

                    required property int modelData
                    readonly property int modeValue: modelData
                    readonly property bool selected: root.videoModeKnown
                                                     && root.videoMode === modeValue
                    readonly property bool optionEnabled: root.online
                                                          && !root.videoModePending

                    width: videoModeMenu.availableWidth
                    height: root.actionSize
                    radius: Math.min(9, height * 0.2)
                    color: modeOptionMouse.pressed ? root.buttonPressedColor : "transparent"
                    border.width: 0
                    opacity: optionEnabled ? 1.0 : 0.48

                    Behavior on color { ColorAnimation { duration: 100 } }

                    QGCColoredImage {
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.top: parent.top
                        anchors.topMargin: parent.height * 0.16
                        width: root.modeIconSize
                        height: width
                        source: modeOption.modeValue === 0
                                ? "qrc:/InstrumentValueIcons/camera.svg"
                                : (modeOption.modeValue === 2
                                   ? "qrc:/InstrumentValueIcons/thermometer.svg"
                                   : "qrc:/InstrumentValueIcons/layers.svg")
                        sourceSize.height: height
                        fillMode: Image.PreserveAspectFit
                        scale: modeOptionMouse.pressed ? 0.94 : 1.0
                        Behavior on scale { NumberAnimation { duration: 90 } }
                        color: modeOptionMouse.pressed ? root.pressedIconColor
                               : modeOptionMouse.containsMouse ? root.hoveredIconColor
                               : modeOption.selected ? root.accentColor : root.iconColor
                    }

                    QGCLabel {
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: parent.height * 0.08
                        text: root.videoModeCode(modeOption.modeValue)
                        color: modeOptionMouse.pressed ? root.pressedIconColor
                               : modeOptionMouse.containsMouse ? root.hoveredIconColor
                               : modeOption.selected ? root.accentColor : root.iconColor
                        font.bold: true
                        font.pointSize: ScreenTools.smallFontPointSize * 0.58
                    }

                    CameraControlIcon {
                        anchors.top: parent.top
                        anchors.right: parent.right
                        anchors.topMargin: parent.height * 0.04
                        anchors.rightMargin: parent.width * 0.08
                        visible: modeOption.selected
                        kind: "apply"
                        color: root.accentColor
                        width: root.actionSize * 0.25
                        height: width
                    }

                    CameraControlToolTip {
                        visible: modeOptionMouse.containsMouse
                        delay: 350
                        text: root.videoModeName(modeOption.modeValue)
                    }

                    MouseArea {
                        id: modeOptionMouse

                        anchors.fill: parent
                        enabled: modeOption.optionEnabled
                        hoverEnabled: true
                        preventStealing: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.requestVideoMode(modeOption.modeValue)
                    }
                }
            }
        }
    }

    Connections {
        target: videoModeMenu.parent
        enabled: videoModeMenu.visible
        ignoreUnknownSignals: true

        function onWidthChanged() { videoModeMenu.reposition() }
        function onHeightChanged() { videoModeMenu.reposition() }
    }

    GridLayout {
        id: controlColumn
        objectName: "cameraActionsGrid"

        anchors.fill: parent
        anchors.margins: root.panelPadding
        columns: 2
        columnSpacing: root.itemSpacing
        rowSpacing: root.itemSpacing

        GimbalZoomControl {
            id: zoomControl
            Layout.columnSpan: 2
            Layout.fillWidth: true

            manager: root.manager
            showActualZoom: root.showActualZoom
            controlSize: root.actionSize
            cellWidth: root.cellWidth
            iconSize: root.actionIconSize
            controlSpacing: root.itemSpacing
            accentColor: root.accentColor
            buttonColor: root.buttonColor
            buttonHoverColor: root.buttonHoverColor
            buttonPressedColor: root.buttonPressedColor
            buttonBorderColor: root.controlBorderColor
            buttonCornerRadius: ScreenTools.defaultFontPixelHeight * 0.4
            Layout.alignment: Qt.AlignHCenter
        }

        Rectangle {
            Layout.columnSpan: 2
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            Layout.leftMargin: root.itemSpacing
            Layout.rightMargin: root.itemSpacing
            Layout.alignment: Qt.AlignHCenter
            color: "#dce3ea"
        }

        Rectangle {
            id: modeButton
            Layout.columnSpan: 2

            readonly property string statusText: root.videoModePending
                                                     ? "..."
                                                     : root.videoModeCode(root.videoMode)

            visible: root.thermalControlsVisible
            Layout.preferredWidth: root.actionSize
            Layout.preferredHeight: root.actionSize
            Layout.alignment: Qt.AlignHCenter
            radius: Math.min(10, width * 0.22)
            color: modeMouseArea.pressed ? root.buttonPressedColor : "transparent"
            border.width: 0
            enabled: root.online && !root.videoModePending

            Behavior on color { ColorAnimation { duration: 100 } }

            CameraControlIcon {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.top
                anchors.topMargin: parent.height * 0.16
                width: root.modeIconSize
                height: width
                kind: "switch"
                color: !modeButton.enabled ? "#8795a3" : modeMouseArea.pressed ? root.pressedIconColor
                       : modeMouseArea.containsMouse ? root.hoveredIconColor
                       : root.videoModeMenuOpen ? root.accentColor : root.iconColor
                scale: modeMouseArea.pressed ? 0.94 : 1.0
                Behavior on scale { NumberAnimation { duration: 90 } }
            }

            QGCLabel {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: parent.height * 0.08
                text: modeButton.statusText
                color: !modeButton.enabled ? "#8795a3" : modeMouseArea.pressed ? root.pressedIconColor : root.accentColor
                font.bold: true
                font.pointSize: ScreenTools.smallFontPointSize * 0.58
            }

            CameraControlIcon {
                anchors.left: parent.left
                anchors.leftMargin: parent.width * 0.06
                anchors.verticalCenter: parent.verticalCenter
                kind: "chevronDown"
                rotation: 90
                color: !root.online ? "#8795a3" : root.videoModeMenuOpen ? root.accentColor : "#3d5262"
                width: root.actionSize * 0.2
                height: width
            }

            Rectangle {
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: Math.max(3, parent.width * 0.08)
                width: Math.max(5, parent.width * 0.12)
                height: width
                radius: width / 2
                color: !root.online ? "#8795a3" : root.videoModePending
                       ? "#96621d"
                       : (root.videoModeKnown ? root.accentColor : "#8795a3")
                border.color: "#ffffff"
                border.width: 1
            }

            MouseArea {
                id: modeMouseArea

                anchors.fill: parent
                enabled: parent.enabled
                hoverEnabled: true
                preventStealing: false
                cursorShape: Qt.PointingHandCursor
                property bool menuWasOpenOnPress: false

                onPressed: menuWasOpenOnPress = root.videoModeMenuOpen
                onClicked: {
                    if (menuWasOpenOnPress
                            || Date.now() - videoModeMenu.lastClosedAtMs < 160) {
                        root.closeTransientUi()
                    } else {
                        root.openVideoModeMenu()
                    }
                }
            }
        }

        Rectangle {
            id: photoButton
            objectName: "photoButton"

            Layout.preferredWidth: root.cellWidth
            Layout.preferredHeight: root.actionSize
            Layout.alignment: Qt.AlignHCenter | Qt.AlignTop
            radius: ScreenTools.defaultFontPixelHeight * 0.4
            color: photoMouseArea.pressed ? root.buttonPressedColor : "transparent"
            border.width: 0
            // Starting camera work requires a responding payload.
            enabled: root.online

            Behavior on color { ColorAnimation { duration: 100 } }

            CameraControlIcon {
                anchors.centerIn: parent
                width: root.actionIconSize
                height: width
                kind: "camera"
                color: !photoButton.enabled ? "#8795a3" : photoMouseArea.pressed ? root.pressedIconColor
                       : photoMouseArea.containsMouse ? root.hoveredIconColor
                       : photoSuccessFlash.running ? root.accentColor : root.iconColor
                scale: photoMouseArea.pressed ? 0.94 : 1.0
                Behavior on scale { NumberAnimation { duration: 90 } }
                Behavior on color { ColorAnimation { duration: 100 } }
            }

            // A confirmed capture briefly brightens the pictogram only.
            Timer {
                id: photoSuccessFlash
                interval: 420
                repeat: false
            }

            Accessible.role: Accessible.Button
            Accessible.name: qsTr("Take photo")
            CameraControlToolTip {
                visible: photoMouseArea.containsMouse && !photoMouseArea.pressed
                text: qsTr("Take photo")
            }

            MouseArea {
                id: photoMouseArea
                objectName: "photoMouseArea"

                anchors.fill: parent
                enabled: parent.enabled
                hoverEnabled: true
                preventStealing: false
                cursorShape: Qt.PointingHandCursor
                onClicked: root.manager.takePhoto()
            }
        }

        Rectangle {
            id: videoButton
            objectName: "videoButton"

            readonly property string statusText: root.recordingSessionPending && !root.recordingSessionCapturing
                                                     ? "..."
                                                     : (root.recordingSessionFailed
                                                        ? qsTr("FAILED")
                                                        : (root.recordingSessionVisualActive
                                                           ? root.recordingTimeText()
                                                           : ""))

            // Keep the rail narrow when recording. The elapsed time sits under
            // the icon, so a media state change does not move the whole rail.
            Layout.preferredWidth: root.cellWidth
            Layout.preferredHeight: root.actionSize + ScreenTools.defaultFontPixelHeight * 0.85
            Layout.alignment: Qt.AlignHCenter | Qt.AlignTop
            radius: ScreenTools.defaultFontPixelHeight * 0.4
            color: "transparent"
            border.width: 0
            // The manager coordinates the independent SD and local recording branches.
            enabled: Boolean(root.manager && root.manager.videoRecordingAvailable
                             && (root.online || root.recordingSessionVisualActive))

            Column {
                id: videoContent

                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.top
                spacing: ScreenTools.defaultFontPixelHeight * 0.1

                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: videoButton.width
                    height: root.actionSize
                    radius: ScreenTools.defaultFontPixelHeight * 0.4
                    color: videoMouseArea.pressed ? root.buttonPressedColor : "transparent"
                    border.width: 0
                    Behavior on color { ColorAnimation { duration: 100 } }
                    CameraControlIcon {
                        anchors.centerIn: parent
                        width: root.actionIconSize
                        height: width
                        kind: root.recordingSessionVisualActive ? "recordStop" : "record"
                        color: !videoButton.enabled ? "#8795a3" : videoMouseArea.pressed ? root.pressedIconColor
                               : root.recordingSessionCapturing ? "#c73d4d"
                               : root.recordingSessionFailed || root.recordingSessionPending ? "#96621d"
                               : videoMouseArea.containsMouse ? root.hoveredIconColor : root.iconColor
                        scale: videoMouseArea.pressed ? 0.94 : 1.0
                        Behavior on scale { NumberAnimation { duration: 90 } }
                    }
                }

                QGCLabel {
                    id: videoStatusLabel
                    objectName: "videoStatusLabel"
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: videoButton.width
                    text: videoButton.statusText
                    color: root.recordingSessionCapturing ? "#c73d4d"
                           : root.recordingSessionFailed || root.recordingSessionPending ? "#96621d" : "#3d5262"
                    font.bold: true
                    font.pointSize: ScreenTools.smallFontPointSize * 0.88
                    fontSizeMode: Text.HorizontalFit
                    minimumPixelSize: Math.max(8, ScreenTools.defaultFontPixelHeight * 0.55)
                    horizontalAlignment: Text.AlignHCenter
                }
            }

            Accessible.role: Accessible.Button
            Accessible.name: root.recordingSessionVisualActive ? qsTr("Stop recording") : qsTr("Start recording")
            CameraControlToolTip {
                visible: videoMouseArea.containsMouse && !videoMouseArea.pressed
                text: root.recordingSessionVisualActive ? qsTr("Stop recording") : qsTr("Start recording")
            }

            MouseArea {
                id: videoMouseArea
                objectName: "videoMouseArea"

                anchors.fill: parent
                enabled: parent.enabled
                hoverEnabled: true
                preventStealing: false
                cursorShape: Qt.PointingHandCursor
                onClicked: root.manager.toggleVideoRecording()
            }
        }

        CameraStorageStatus {
            objectName: "sdStatusRow"
            Layout.columnSpan: 2
            Layout.fillWidth: true
            known: Boolean(root.manager && root.manager.sdCardStatusKnown)
            inserted: Boolean(root.manager && root.manager.sdCardInserted)
        }
    }
}
