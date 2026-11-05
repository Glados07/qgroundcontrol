/****************************************************************************
 * Compact Rouke flight-view rail; parameter state remains ACK-confirmed.
 ****************************************************************************/
import QtQuick
import QtQuick.Layouts
import QtQuick.Window
import QtQuick.Controls as QtControls
import QGroundControl
import QGroundControl.Controls
import QGroundControl.ScreenTools

Rectangle {
    id: root
    property var manager: QGroundControl.corePlugin ? QGroundControl.corePlugin.roukeControlManager : null
    readonly property bool available: Boolean(manager && manager.enabled)
    readonly property bool canControl: Boolean(available && manager.controlAvailable && !manager.pending)
    readonly property bool canSaveLocally: Boolean(available && manager.localMediaStorageEnabled)
    readonly property bool online: Boolean(available && manager.sdkResponding)
    readonly property real recordingActionSize: Math.max(ScreenTools.defaultFontPixelHeight * 2.35,
                                                        ScreenTools.isMobile ? ScreenTools.minTouchPixels : 0)
    readonly property bool recordingSessionActive: Boolean(manager && manager.recordingSessionActive)
    readonly property bool recordingSessionCapturing: Boolean(manager && manager.recordingSessionCapturing)
    readonly property bool recordingSessionPending: Boolean(manager
                                                              && (manager.cameraRecordingPending
                                                                  || manager.localRecordingPending))
    readonly property bool recordingSessionFailed: recordingSessionActive
                                                   && !recordingSessionCapturing
                                                   && !recordingSessionPending
    // The manager retains the obligation to stop across pending/uncertain ACKs.
    // The next click and the displayed action must use that same decision.
    readonly property bool recordingSessionVisualActive: Boolean(manager && manager.recordingStopRequired)
    property int recordingSeconds: 0
    function recordingTimeText() {
        const hours = Math.floor(recordingSeconds / 3600)
        const minutes = Math.floor((recordingSeconds % 3600) / 60)
        const seconds = recordingSeconds % 60
        const mm = (minutes < 10 ? "0" : "") + minutes
        const ss = (seconds < 10 ? "0" : "") + seconds
        return hours > 0 ? hours + ":" + mm + ":" + ss : mm + ":" + ss
    }
    function resetRecordingTimeIfStopped() {
        if (!recordingSessionVisualActive && !recordingSessionCapturing) recordingSeconds = 0
    }
    onRecordingSessionVisualActiveChanged: resetRecordingTimeIfStopped()
    onRecordingSessionCapturingChanged: resetRecordingTimeIfStopped()
    Timer {
        interval: 1000
        repeat: true
        running: root.recordingSessionCapturing
        onTriggered: ++root.recordingSeconds
    }
    readonly property real panelPadding: ScreenTools.defaultFontPixelHeight * 0.45
    readonly property real actionHeight: Math.max(ScreenTools.defaultFontPixelHeight * 2.25,
                                                 ScreenTools.isMobile ? ScreenTools.minTouchPixels : 0)
    readonly property bool settingsOpen: settingsPopup.visible
    property double settingsClosedAt: 0
    property double viewClosedAt: 0
    readonly property string errorText: manager ? [manager.lastError, manager.localMediaError]
                                                    .filter(function(value) { return value.length > 0 }).join("\n") : ""
    property real remainingHeight: 10000
    function updateAvailableHeight() {
        remainingHeight = root.Window.window
                ? Math.max(actionHeight * 2.2, root.Window.window.height - root.mapToItem(null, 0, 0).y - panelPadding)
                : 10000
    }
    onYChanged: Qt.callLater(updateAvailableHeight)
    onActionHeightChanged: Qt.callLater(updateAvailableHeight)
    Component.onCompleted: Qt.callLater(updateAvailableHeight)
    Connections { target: root.parent; function onYChanged() { Qt.callLater(root.updateAvailableHeight) } }
    implicitWidth: Math.max(actionHeight, ScreenTools.defaultFontPixelHeight * 2.9) + panelPadding * 2
    implicitHeight: Math.min(controls.implicitHeight + settingsButton.implicitHeight + panelPadding * 3, remainingHeight)
    width: implicitWidth
    height: implicitHeight
    radius: ScreenTools.defaultFontPixelHeight * 0.55
    color: "#f2172833"
    border.color: "#355262"
    visible: available
    function closeTransientUi() {
        if (settingsPopup) settingsPopup.close()
        if (viewMenu) viewMenu.close()
    }
    onVisibleChanged: if (!visible) closeTransientUi()
    onEnabledChanged: if (!enabled) closeTransientUi()
    onManagerChanged: { closeTransientUi(); recordingSeconds = 0 }
    onAvailableChanged: if (!available) closeTransientUi()
    Component.onDestruction: closeTransientUi()
    Connections {
        target: root.Window.window
        ignoreUnknownSignals: true
        function onHeightChanged() { Qt.callLater(root.updateAvailableHeight) }
        function onVisibleChanged() { if (!root.Window.window || !root.Window.window.visible) root.closeTransientUi() }
    }
    Connections {
        target: Qt.application
        function onStateChanged() { if (Qt.application.state !== Qt.ApplicationActive) root.closeTransientUi() }
    }
    DeadMouseArea { anchors.fill: parent }
    RoukeCameraSettings { id: settingsPopup; manager: root.manager; anchorItem: root }
    Connections {
        target: settingsPopup
        function onClosed() { root.settingsClosedAt = Date.now() }
    }
    QtControls.Popup {
        id: viewMenu
        objectName: "viewMenu"
        parent: QtControls.Overlay.overlay
        width: Math.min(ScreenTools.defaultFontPixelWidth * 23, parent ? parent.width - 24 : 220)
        height: viewOptions.implicitHeight + padding * 2
        padding: root.panelPadding
        focus: true
        closePolicy: QtControls.Popup.CloseOnEscape | QtControls.Popup.CloseOnPressOutside
        function reposition() {
            if (!parent) return
            const p = viewButton.mapToItem(parent, 0, 0)
            x = Math.max(12, Math.min(parent.width - width - 12, p.x - width - 12))
            y = Math.max(12, Math.min(parent.height - height - 12, p.y))
        }
        onOpened: reposition()
        onClosed: root.viewClosedAt = Date.now()
        Connections {
            target: viewMenu.parent
            function onWidthChanged() { if (viewMenu.visible) viewMenu.reposition() }
            function onHeightChanged() { if (viewMenu.visible) viewMenu.reposition() }
        }
        background: Rectangle { color: "#172833"; radius: 16; border.color: "#476878" }
        contentItem: RowLayout {
            id: viewOptions
            spacing: root.panelPadding
            Repeater {
                model: [4, 5]
                CameraControlButton {
                    required property int modelData
                    objectName: "view_" + modelData
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    implicitHeight: root.actionHeight * 1.45
                    iconName: modelData === 4 ? "payloadRouke" : "camera"
                    caption: text
                    text: modelData === 4 ? "LR1" : "OV2735"
                    checked: Boolean(root.manager && root.manager.viewKnown && root.manager.viewType === modelData)
                    enabled: root.canControl
                    onClicked: { root.manager.setView(modelData); viewMenu.close() }
                }
            }
        }
    }
    Flickable {
        id: cameraControlScroll
        objectName: "cameraControlScroll"
        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
        anchors.bottom: settingsButton.top
        anchors.margins: root.panelPadding
        clip: true
        contentWidth: width
        contentHeight: controls.implicitHeight
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.VerticalFlick
        QtControls.ScrollIndicator.vertical: QtControls.ScrollIndicator {}
        WheelHandler {
            target: null
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            onWheel: function(event) {
                const delta = event.pixelDelta.y || event.angleDelta.y / 120 * ScreenTools.defaultFontPixelHeight * 3
                cameraControlScroll.contentY = Math.max(0, Math.min(cameraControlScroll.contentHeight - cameraControlScroll.height,
                                                                    cameraControlScroll.contentY - delta))
                event.accepted = true
            }
        }
        ColumnLayout {
            id: controls
            width: cameraControlScroll.width
            spacing: ScreenTools.defaultFontPixelHeight * 0.24
            Item {
                Layout.fillWidth: true
                implicitHeight: ScreenTools.defaultFontPixelHeight * 0.9
                Rectangle {
                    anchors.centerIn: parent
                    width: 6; height: 6; radius: 3
                    color: root.manager && root.manager.pending ? "#ffcc80" : root.online ? "#65d9f4" : "#6d8592"
                }
                QtControls.ToolTip.visible: stateMouse.containsMouse
                QtControls.ToolTip.text: root.manager && root.manager.pending ? qsTr("Waiting for payload")
                                         : root.online ? qsTr("Rouke connected") : qsTr("Awaiting payload response")
                MouseArea { id: stateMouse; anchors.fill: parent; hoverEnabled: true }
            }
            CameraControlButton {
                id: viewButton
                objectName: "viewButton"
                Layout.alignment: Qt.AlignHCenter
                iconName: "switch"
                text: qsTr("Switch camera")
                caption: root.manager && root.manager.viewKnown ? (root.manager.viewType === 4 ? "LR1" : "OV2735") : "—"
                menuIndicator: true
                checked: viewMenu.visible
                enabled: root.canControl
                implicitHeight: root.actionHeight * 1.3
                onClicked: {
                    settingsPopup.close()
                    if (viewMenu.visible || Date.now() - root.viewClosedAt < 160) viewMenu.close()
                    else viewMenu.open()
                }
            }
            Rectangle { Layout.fillWidth: true; Layout.leftMargin: 10; Layout.rightMargin: 10; implicitHeight: 1; color: "#304956" }
            CameraControlButton {
                objectName: "centerButton"
                Layout.alignment: Qt.AlignHCenter
                iconName: "center"
                text: qsTr("Center")
                enabled: Boolean(root.available && root.manager.controlAvailable && !root.manager.gimbalCenterPending)
                onClicked: root.manager.centerGimbal()
            }
            CameraControlButton {
                objectName: "focusButton"
                Layout.alignment: Qt.AlignHCenter
                iconName: "focus"
                text: qsTr("Focus")
                enabled: root.canControl
                onClicked: root.manager.autoFocus()
            }
            CameraControlButton {
                objectName: "photoButton"
                Layout.alignment: Qt.AlignHCenter
                iconName: "camera"
                text: qsTr("Take Photo")
                enabled: root.canControl || root.canSaveLocally
                onClicked: root.manager.takePhoto()
            }
            Rectangle {
                id: recordButton
                objectName: "recordButton"
                readonly property bool recording: root.recordingSessionCapturing
                readonly property string iconName: root.recordingSessionVisualActive ? "recordStop" : "record"
                readonly property color iconColor: videoMouseArea.pressed ? "#163744"
                                                   : root.recordingSessionCapturing ? "#ff6c80"
                                                   : root.recordingSessionFailed || root.recordingSessionPending ? "#ffc857"
                                                   : videoMouseArea.containsMouse ? "#a8eaf7" : "#dcecf4"
                readonly property string text: root.recordingSessionVisualActive ? qsTr("Stop recording") : qsTr("Start recording")
                readonly property string statusText: root.recordingSessionPending && !root.recordingSessionCapturing
                                                     ? "..."
                                                     : root.recordingSessionFailed ? qsTr("FAILED")
                                                     : root.recordingSessionVisualActive ? root.recordingTimeText() : ""

                // Match the A8 recording control, including its fixed footprint
                // and elapsed-time label below the single start/stop pictogram.
                Layout.preferredWidth: root.recordingActionSize * 1.16
                Layout.preferredHeight: root.recordingActionSize + ScreenTools.defaultFontPixelHeight * 0.85
                Layout.alignment: Qt.AlignHCenter
                radius: ScreenTools.defaultFontPixelHeight * 0.4
                color: "transparent"
                border.width: 0
                enabled: Boolean(root.manager && root.manager.videoRecordingAvailable)
                opacity: enabled ? 1.0 : 0.45

                Column {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: parent.top
                    spacing: ScreenTools.defaultFontPixelHeight * 0.1
                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: root.recordingActionSize
                        height: width
                        radius: ScreenTools.defaultFontPixelHeight * 0.4
                        color: videoMouseArea.pressed ? "#b8e7f2" : "transparent"
                        border.width: 0
                        Behavior on color { ColorAnimation { duration: 100 } }
                        CameraControlIcon {
                            anchors.centerIn: parent
                            width: ScreenTools.defaultFontPixelHeight * 1.2
                            height: width
                            kind: recordButton.iconName
                            color: recordButton.iconColor
                            scale: videoMouseArea.pressed ? 0.94 : 1.0
                            Behavior on scale { NumberAnimation { duration: 90 } }
                        }
                    }
                    QGCLabel {
                        id: videoStatusLabel
                        objectName: "videoStatusLabel"
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: recordButton.width
                        text: recordButton.statusText
                        color: root.recordingSessionCapturing ? "#ff9cab"
                               : root.recordingSessionFailed || root.recordingSessionPending ? "#ffc857" : "#b4c9d4"
                        font.bold: true
                        font.pointSize: ScreenTools.smallFontPointSize * 0.88
                        fontSizeMode: Text.HorizontalFit
                        minimumPixelSize: Math.max(8, ScreenTools.defaultFontPixelHeight * 0.55)
                        horizontalAlignment: Text.AlignHCenter
                    }
                }
                Accessible.role: Accessible.Button
                Accessible.name: text
                QtControls.ToolTip.visible: videoMouseArea.containsMouse && !videoMouseArea.pressed
                QtControls.ToolTip.delay: 450
                QtControls.ToolTip.text: text
                MouseArea {
                    id: videoMouseArea
                    objectName: "videoMouseArea"
                    anchors.fill: parent
                    enabled: parent.enabled
                    hoverEnabled: true
                    preventStealing: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.manager.toggleVideoRecording()
                }
            }
            CameraStorageStatus {
                objectName: "sdStatusRow"
                Layout.alignment: Qt.AlignHCenter
                known: Boolean(root.manager && root.manager.sdCardStatusKnown)
                inserted: Boolean(root.manager && root.manager.sdCardInserted)
            }
            CameraControlButton {
                objectName: "errorButton"
                Layout.alignment: Qt.AlignHCenter
                visible: root.errorText.length > 0
                iconName: "warning"
                iconColor: "#ffcc80"
                text: root.errorText
            }
        }
    }
    CameraControlButton {
        id: settingsButton
        objectName: "settingsButton"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: root.panelPadding
        iconName: "settings"
        text: qsTr("Camera settings")
        checked: settingsPopup.visible
        onClicked: {
            viewMenu.close()
            if (settingsPopup.visible || Date.now() - root.settingsClosedAt < 160) settingsPopup.close()
            else settingsPopup.showSettings()
        }
    }
}
