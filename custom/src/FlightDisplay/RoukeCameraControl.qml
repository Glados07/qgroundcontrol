/****************************************************************************
 * Rouke camera controls. Device state is confirmed by payload acknowledgements.
 ****************************************************************************/

import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.ScreenTools

Rectangle {
    id: root

    property var manager: QGroundControl.corePlugin ? QGroundControl.corePlugin.roukeControlManager : null
    readonly property bool available: Boolean(manager && manager.enabled)
    readonly property bool canControl: Boolean(available && manager.controlAvailable && !manager.pending)
    readonly property bool canSaveLocally: Boolean(available && manager.localMediaStorageEnabled)
    readonly property real panelPadding: ScreenTools.defaultFontPixelHeight * 0.48
    readonly property real actionHeight: Math.max(ScreenTools.defaultFontPixelHeight * 2.35,
                                                  ScreenTools.isMobile ? ScreenTools.minTouchPixels : 0)

    implicitWidth: ScreenTools.defaultFontPixelWidth * 24 + panelPadding * 2
    implicitHeight: controls.implicitHeight + panelPadding * 2
    width: implicitWidth
    height: implicitHeight
    radius: ScreenTools.defaultFontPixelHeight * 0.7
    color: "#783b4b58"
    border.color: "#a065d9f4"
    visible: available

    // Same lifecycle contract as the other camera panel; no continuous gesture.
    function closeTransientUi() {}

    DeadMouseArea { anchors.fill: parent }

    ColumnLayout {
        id: controls
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: root.panelPadding
        spacing: ScreenTools.defaultFontPixelHeight * 0.35

        QGCLabel {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: root.manager && root.manager.pending ? qsTr("Waiting for payload")
                  : root.manager && root.manager.sdkResponding ? qsTr("Rouke connected")
                  : root.canControl ? qsTr("Awaiting payload response") : qsTr("Payload control unavailable")
            color: "white"
            font.pointSize: ScreenTools.smallFontPointSize
            wrapMode: Text.Wrap
        }

        RowLayout {
            Layout.fillWidth: true
            Repeater {
                model: [4, 5]
                QGCButton {
                    required property int modelData
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    Layout.preferredHeight: root.actionHeight
                    text: modelData === 4 ? "LR1" : "OV2735"
                    enabled: root.canControl
                    checked: Boolean(root.manager && root.manager.viewKnown && root.manager.viewType === modelData)
                    onClicked: root.manager.setView(modelData)
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            QGCButton {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                Layout.preferredHeight: root.actionHeight
                text: qsTr("Focus")
                enabled: root.canControl
                onClicked: root.manager.autoFocus()
            }
            QGCButton {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                Layout.preferredHeight: root.actionHeight
                text: qsTr("Center")
                enabled: Boolean(root.available && root.manager.controlAvailable
                                 && !root.manager.gimbalCenterPending)
                onClicked: root.manager.centerGimbal()
            }
        }

        QGCButton {
            Layout.fillWidth: true
            Layout.preferredHeight: root.actionHeight
            text: qsTr("Take Photo")
            enabled: root.canControl || root.canSaveLocally
            onClicked: root.manager.takePhoto()
        }

        RowLayout {
            Layout.fillWidth: true
            QGCButton {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                Layout.preferredHeight: root.actionHeight
                text: qsTr("Record")
                enabled: (root.canControl || root.canSaveLocally)
                         && !Boolean(root.manager && root.manager.recordingSessionActive)
                onClicked: root.manager.startRecording()
            }
            QGCButton {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                Layout.preferredHeight: root.actionHeight
                text: qsTr("Stop")
                // Explicit stop remains available when remote recording state
                // is unknown (for example after reconnecting).
                enabled: Boolean(root.manager && root.manager.enabled
                                 && (root.manager.controlAvailable || root.manager.localRecording
                                     || root.manager.localRecordingPending))
                onClicked: root.manager.stopRecording()
            }
        }

        QGCLabel {
            Layout.fillWidth: true
            color: "white"
            font.pointSize: ScreenTools.smallFontPointSize
            wrapMode: Text.Wrap
            text: qsTr("SD: %1 · LOCAL: %2").arg(
                      !root.manager || !root.manager.recordingKnown ? qsTr("Unknown")
                      : root.manager.recording ? qsTr("Recording") : qsTr("Stopped")).arg(
                      root.manager && root.manager.localRecordingPending ? qsTr("Pending")
                      : root.manager && root.manager.localRecording ? qsTr("Recording") : qsTr("Stopped"))
        }

        QGCLabel {
            Layout.fillWidth: true
            color: "white"
            font.pointSize: ScreenTools.smallFontPointSize
            text: qsTr("Photos: SD %1 / LOCAL %2")
                .arg(root.manager ? root.manager.photoCount : 0)
                .arg(root.manager ? root.manager.localPhotoCount : 0)
        }

        QGCLabel {
            Layout.fillWidth: true
            visible: text.length > 0
            color: "#ffd38a"
            font.pointSize: ScreenTools.smallFontPointSize
            wrapMode: Text.Wrap
            text: root.manager ? [root.manager.lastError, root.manager.localMediaError].filter(function(value) { return value.length > 0 }).join("\n") : ""
        }
    }
}
