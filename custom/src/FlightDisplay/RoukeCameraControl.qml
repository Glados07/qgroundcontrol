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
    // The flight-view host supplies the visible primary rail, including its selector.
    property Item settingsAnchorItem: root
    readonly property bool available: Boolean(manager && manager.enabled)
    readonly property bool canControl: Boolean(online && manager.controlAvailable && !manager.pending)
    readonly property bool canSaveLocally: Boolean(online && manager.localMediaStorageEnabled)
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
    readonly property real panelPadding: ScreenTools.defaultFontPixelHeight * 0.22
    readonly property real actionHeight: recordingActionSize
    readonly property real cellWidth: Math.max(ScreenTools.defaultFontPixelHeight * 2.25,
                                                ScreenTools.isMobile ? ScreenTools.minTouchPixels : 0)
    readonly property real itemSpacing: panelPadding
    readonly property bool settingsOpen: settingsPopup.visible
    property double settingsClosedAt: 0
    property double viewClosedAt: 0
    implicitWidth: cellWidth * 2 + itemSpacing + panelPadding * 2
    implicitHeight: controls.implicitHeight + panelPadding * 2
    width: implicitWidth
    height: implicitHeight
    radius: ScreenTools.defaultFontPixelHeight * 0.55
    color: "#f5f7fa"
    border.color: "#cbd5df"
    visible: available
    function closeTransientUi() {
        if (settingsPopup) settingsPopup.close()
        if (viewMenu) viewMenu.close()
    }
    onVisibleChanged: if (!visible) closeTransientUi()
    onEnabledChanged: if (!enabled) closeTransientUi()
    onManagerChanged: { closeTransientUi(); recordingSeconds = 0 }
    onAvailableChanged: if (!available) closeTransientUi()
    // Settings remain available for inspection and draft editing while offline.
    onOnlineChanged: if (!online && viewMenu) viewMenu.close()
    Component.onDestruction: closeTransientUi()
    Connections {
        target: root.Window.window
        ignoreUnknownSignals: true
        function onVisibleChanged() { if (!root.Window.window || !root.Window.window.visible) root.closeTransientUi() }
    }
    Connections {
        target: Qt.application
        function onStateChanged() { if (Qt.application.state !== Qt.ApplicationActive) root.closeTransientUi() }
    }
    // Block map clicks, but allow the host Flickable to take scrolling gestures.
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        preventStealing: false
        onWheel: function(wheel) { wheel.accepted = false }
    }
    RoukeCameraSettings { id: settingsPopup; manager: root.manager; anchorItem: root.settingsAnchorItem }
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
        background: Rectangle { color: "#f5f7fa"; radius: 16; border.color: "#cbd5df" }
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
    // The flight-view host constrains and scrolls this complete two-column panel.
    GridLayout {
        id: controls
        objectName: "cameraActionsGrid"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: root.panelPadding
        columns: 2
        columnSpacing: root.itemSpacing
        rowSpacing: root.itemSpacing
        Item {
            Layout.row: 0
            Layout.column: 0
            Layout.columnSpan: 2
            Layout.fillWidth: true
            implicitHeight: ScreenTools.defaultFontPixelHeight * 0.6
            Rectangle {
                anchors.centerIn: parent
                width: 6; height: 6; radius: 3
                color: !root.online ? "#8795a3" : root.manager.pending ? "#96621d" : "#176b83"
            }
            CameraControlToolTip {
                visible: stateMouse.containsMouse
                text: root.online && root.manager.pending ? qsTr("Waiting for payload")
                                     : root.online ? qsTr("Rouke connected") : qsTr("Awaiting payload response")
            }
            MouseArea { id: stateMouse; anchors.fill: parent; hoverEnabled: true }
        }
        CameraControlButton {
            id: viewButton
            objectName: "viewButton"
            Layout.row: 1
            Layout.column: 0
            Layout.preferredWidth: root.cellWidth
            Layout.preferredHeight: root.actionHeight * 1.3
            Layout.alignment: Qt.AlignHCenter | Qt.AlignTop
            iconName: "switch"
            text: qsTr("Switch camera")
            caption: root.manager && root.manager.viewKnown ? (root.manager.viewType === 4 ? "LR1" : "OV2735") : "—"
            menuIndicator: true
            checked: viewMenu.visible
            enabled: root.canControl
            onClicked: {
                settingsPopup.close()
                if (viewMenu.visible || Date.now() - root.viewClosedAt < 160) viewMenu.close()
                else viewMenu.open()
            }
        }
        CameraControlButton {
            objectName: "centerButton"
            Layout.row: 1
            Layout.column: 1
            Layout.preferredWidth: root.cellWidth
            Layout.preferredHeight: root.actionHeight
            Layout.alignment: Qt.AlignHCenter | Qt.AlignTop
            iconName: "center"
            text: qsTr("Center")
            enabled: Boolean(root.online && root.manager.controlAvailable && !root.manager.gimbalCenterPending)
            onClicked: root.manager.centerGimbal()
        }
        CameraControlButton {
            objectName: "focusButton"
            Layout.row: 2
            Layout.column: 0
            Layout.preferredWidth: root.cellWidth
            Layout.preferredHeight: root.actionHeight
            Layout.alignment: Qt.AlignHCenter | Qt.AlignTop
            iconName: "focus"
            text: qsTr("Focus")
            enabled: root.canControl
            onClicked: root.manager.autoFocus()
        }
        CameraControlButton {
            id: settingsButton
            objectName: "settingsButton"
            Layout.row: 2
            Layout.column: 1
            Layout.preferredWidth: root.cellWidth
            Layout.preferredHeight: root.actionHeight
            Layout.alignment: Qt.AlignHCenter | Qt.AlignTop
            iconName: "settings"
            text: qsTr("Camera settings")
            checked: settingsPopup.visible
            enabled: root.available
            muted: !root.online
            onClicked: {
                viewMenu.close()
                if (settingsPopup.visible || Date.now() - root.settingsClosedAt < 160) settingsPopup.close()
                else settingsPopup.showSettings()
            }
        }
        CameraControlButton {
            objectName: "photoButton"
            Layout.row: 3
            Layout.column: 0
            Layout.preferredWidth: root.cellWidth
            Layout.preferredHeight: root.actionHeight
            Layout.alignment: Qt.AlignHCenter | Qt.AlignTop
            iconName: "camera"
            text: qsTr("Take Photo")
            enabled: root.canControl || root.canSaveLocally
            onClicked: root.manager.takePhoto()
        }
        Rectangle {
            id: recordButton
            Layout.row: 3
            Layout.column: 1
            objectName: "recordButton"
            readonly property bool recording: root.recordingSessionCapturing
            readonly property string iconName: root.recordingSessionVisualActive ? "recordStop" : "record"
            readonly property color iconColor: !enabled ? "#8795a3" : videoMouseArea.pressed ? "#152733"
                                               : root.recordingSessionCapturing ? "#c73d4d"
                                               : root.recordingSessionFailed || root.recordingSessionPending ? "#96621d"
                                               : videoMouseArea.containsMouse ? "#176b83" : "#152733"
            readonly property string text: root.recordingSessionVisualActive ? qsTr("Stop recording") : qsTr("Start recording")
            readonly property string statusText: root.recordingSessionPending && !root.recordingSessionCapturing
                                                 ? "..."
                                                 : root.recordingSessionFailed ? qsTr("FAILED")
                                                 : root.recordingSessionVisualActive ? root.recordingTimeText() : ""

            // Match the A8 recording control, including its fixed footprint
            // and elapsed-time label below the single start/stop pictogram.
            Layout.preferredWidth: root.cellWidth
            Layout.preferredHeight: root.recordingActionSize + ScreenTools.defaultFontPixelHeight * 0.85
            Layout.alignment: Qt.AlignHCenter | Qt.AlignTop
            radius: ScreenTools.defaultFontPixelHeight * 0.4
            color: "transparent"
            border.width: 0
            enabled: Boolean(root.manager && root.manager.videoRecordingAvailable
                             && (root.online || root.recordingSessionVisualActive))

            Column {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.top
                spacing: ScreenTools.defaultFontPixelHeight * 0.1
                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: recordButton.width
                    height: root.recordingActionSize
                    radius: ScreenTools.defaultFontPixelHeight * 0.4
                    color: videoMouseArea.pressed ? "#c5e1e9" : "transparent"
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
            Accessible.name: text
            CameraControlToolTip {
                visible: videoMouseArea.containsMouse && !videoMouseArea.pressed
                text: recordButton.text
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
            Layout.row: 4
            Layout.column: 0
            Layout.columnSpan: 2
            Layout.fillWidth: true
            known: Boolean(root.manager && root.manager.sdCardStatusKnown)
            inserted: Boolean(root.manager && root.manager.sdCardInserted)
        }
    }
}
