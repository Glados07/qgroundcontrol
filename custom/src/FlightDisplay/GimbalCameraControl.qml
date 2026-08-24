/****************************************************************************
 *
 * Fly View思翼相机合并控制栏。
 * 缩放、拍照和录像纵向排列并复用QGC图标；全部命令均不依赖activeVehicle。
 *
 ****************************************************************************/

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QtControls
import QtQuick.Window

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
    // Optional diagnostic presentation. Product camera wrappers keep this
    // disabled so the normal control bar shows one target value consistently.
    property bool showActualZoom: false

    readonly property bool available: Boolean(manager && manager.enabled)
    readonly property bool online: Boolean(manager && manager.enabled && manager.sdkResponding)
    readonly property real actionSize: Math.max(ScreenTools.defaultFontPixelHeight * 2.35,
                                                ScreenTools.isMobile ? ScreenTools.minTouchPixels : 0)
    readonly property real panelPadding: ScreenTools.defaultFontPixelHeight * 0.48
    readonly property real itemSpacing: ScreenTools.defaultFontPixelWidth * 0.45
    readonly property color accentColor: "#65d9f4"
    readonly property color panelColor: online ? "#783b4b58" : "#66303c47"
    readonly property color panelBorderColor: "#a065d9f4"
    readonly property color innerBorderColor: "#2865d9f4"
    readonly property color controlBorderColor: "#8065d9f4"
    readonly property color popupColor: "#b02c3945"
    readonly property color buttonColor: "#321f2b36"
    readonly property color buttonHoverColor: "#4a334653"
    readonly property color buttonPressedColor: "#e8f2f7fa"
    readonly property real modeIconSize: actionSize * 0.40
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

    implicitWidth: controlColumn.implicitWidth + panelPadding * 2
    implicitHeight: controlColumn.implicitHeight + panelPadding * 2
    width: implicitWidth
    height: implicitHeight
    radius: Math.min(14, ScreenTools.defaultFontPixelHeight * 0.7)
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
        border.color: root.innerBorderColor
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
            border.color: root.videoModePending ? "#d8ffc857" : root.panelBorderColor
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
                    color: selected
                           ? "#3e2a7182"
                           : (modeOptionMouse.pressed
                              ? root.buttonPressedColor
                              : (modeOptionMouse.containsMouse
                                 ? root.buttonHoverColor
                                 : root.buttonColor))
                    border.color: selected
                                  ? root.accentColor
                                  : (modeOptionMouse.containsMouse
                                     ? root.accentColor
                                     : root.controlBorderColor)
                    border.width: selected ? 2 : 1
                    opacity: optionEnabled ? 1.0 : 0.48
                    scale: modeOptionMouse.pressed ? 0.98 : 1.0

                    Behavior on color { ColorAnimation { duration: 100 } }
                    Behavior on scale { NumberAnimation { duration: 80 } }

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
                        color: modeOptionMouse.pressed && !modeOption.selected
                               ? "#16212a"
                               : (modeOption.selected ? root.accentColor : "white")
                    }

                    QGCLabel {
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: parent.height * 0.08
                        text: root.videoModeCode(modeOption.modeValue)
                        color: modeOptionMouse.pressed && !modeOption.selected
                               ? "#16212a"
                               : (modeOption.selected ? root.accentColor : "white")
                        font.bold: true
                        font.pointSize: ScreenTools.smallFontPointSize * 0.58
                    }

                    QGCLabel {
                        anchors.top: parent.top
                        anchors.right: parent.right
                        anchors.topMargin: parent.height * 0.04
                        anchors.rightMargin: parent.width * 0.08
                        visible: modeOption.selected
                        text: "\u2713"
                        color: root.accentColor
                        font.bold: true
                        font.pixelSize: root.actionSize * 0.22
                    }

                    QtControls.ToolTip.visible: modeOptionMouse.containsMouse
                    QtControls.ToolTip.delay: 350
                    QtControls.ToolTip.text: root.videoModeName(modeOption.modeValue)

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
            accentColor: root.accentColor
            buttonColor: root.buttonColor
            buttonHoverColor: root.buttonHoverColor
            buttonPressedColor: root.buttonPressedColor
            buttonBorderColor: root.controlBorderColor
            buttonCornerRadius: Math.min(10, root.actionSize * 0.22)
            Layout.alignment: Qt.AlignHCenter
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            Layout.leftMargin: root.itemSpacing
            Layout.rightMargin: root.itemSpacing
            Layout.alignment: Qt.AlignHCenter
            color: "#5265d9f4"
        }

        Rectangle {
            id: modeButton

            readonly property string statusText: root.videoModePending
                                                     ? "..."
                                                     : root.videoModeCode(root.videoMode)

            visible: root.thermalControlsVisible
            Layout.preferredWidth: root.actionSize
            Layout.preferredHeight: root.actionSize
            Layout.alignment: Qt.AlignHCenter
            radius: Math.min(10, width * 0.22)
            color: modeMouseArea.pressed
                   ? root.buttonPressedColor
                   : (root.videoModeMenuOpen
                      ? "#442a7182"
                      : (modeMouseArea.containsMouse
                         ? root.buttonHoverColor
                         : root.buttonColor))
            border.color: root.videoModePending
                          ? "#ffffc857"
                          : (root.videoModeMenuOpen
                             ? root.accentColor
                             : (modeMouseArea.containsMouse
                                ? root.accentColor
                                : root.controlBorderColor))
            border.width: root.videoModeMenuOpen ? 2 : 1
            enabled: root.online && !root.videoModePending
            opacity: enabled ? 1.0 : 0.5
            scale: modeMouseArea.pressed ? 0.94 : 1.0

            Behavior on color { ColorAnimation { duration: 110 } }
            Behavior on scale { NumberAnimation { duration: 85 } }

            QGCColoredImage {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.top
                anchors.topMargin: parent.height * 0.16
                width: root.modeIconSize
                height: width
                source: "qrc:/InstrumentValueIcons/view-carousel.svg"
                sourceSize.height: height
                fillMode: Image.PreserveAspectFit
                color: modeMouseArea.pressed ? "#15212a" : "white"
            }

            QGCLabel {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: parent.height * 0.08
                text: modeButton.statusText
                color: modeMouseArea.pressed ? "#15212a" : root.accentColor
                font.bold: true
                font.pointSize: ScreenTools.smallFontPointSize * 0.58
            }

            QGCLabel {
                anchors.left: parent.left
                anchors.leftMargin: parent.width * 0.06
                anchors.verticalCenter: parent.verticalCenter
                text: "\u25c0"
                color: modeMouseArea.pressed
                       ? "#15212a"
                       : (root.videoModeMenuOpen ? root.accentColor : "#b8ffffff")
                font.pixelSize: root.actionSize * 0.14
            }

            Rectangle {
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: Math.max(3, parent.width * 0.08)
                width: Math.max(5, parent.width * 0.12)
                height: width
                radius: width / 2
                color: root.videoModePending
                       ? "#ffc857"
                       : (root.videoModeKnown ? root.accentColor : "#8e9aa3")
                border.color: "#b8ffffff"
                border.width: 1
            }

            MouseArea {
                id: modeMouseArea

                anchors.fill: parent
                enabled: parent.enabled
                hoverEnabled: true
                preventStealing: true
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

            Layout.preferredWidth: root.actionSize
            Layout.preferredHeight: root.actionSize
            Layout.alignment: Qt.AlignHCenter
            radius: Math.min(10, width * 0.22)
            color: photoMouseArea.pressed
                   ? root.buttonPressedColor
                   : (photoMouseArea.containsMouse ? root.buttonHoverColor : root.buttonColor)
            border.color: photoMouseArea.containsMouse
                          ? root.accentColor : root.controlBorderColor
            border.width: photoMouseArea.containsMouse ? 2 : 1
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
                radius: Math.min(12, width * 0.24)
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
            radius: statusText.length > 0 ? height / 2 : Math.min(10, height * 0.22)
            color: root.recordingSessionFailed
                   ? (videoMouseArea.pressed ? "#e06d3514" : "#c85b2d12")
                   : (root.recordingSessionVisualActive
                      ? (videoMouseArea.pressed ? "#e0a31f34" : "#c8a31f34")
                      : (videoMouseArea.pressed
                         ? root.buttonPressedColor
                         : (videoMouseArea.containsMouse ? root.buttonHoverColor : root.buttonColor)))
            border.color: root.recordingSessionFailed
                          ? "#ffffc857"
                          : (root.recordingSessionVisualActive
                             ? "#ffff6b78"
                             : (videoMouseArea.containsMouse
                                ? root.accentColor
                                : root.controlBorderColor))
            border.width: videoMouseArea.containsMouse || root.recordingSessionVisualActive ? 2 : 1
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
