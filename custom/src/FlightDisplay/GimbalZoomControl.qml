/****************************************************************************
 *
 * Shared A8 Mini and MT11 zoom control. A short release asks the selected
 * manager for one exact step. Holding for 420 ms starts native continuous
 * zoom; release, cancellation, hiding and application suspension all ask the
 * manager to stop. Device-specific zoom ranges remain in their managers.
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

    readonly property int gestureIdle: 0
    readonly property int gesturePressed: 1
    readonly property int gestureHolding: 2
    readonly property int gestureConsumed: 3
    readonly property bool online: Boolean(manager && manager.zoomControlsUnlocked)
    readonly property bool zoomKnown: Boolean(manager && manager.zoomStatusKnown)
    readonly property bool canSend: online
    readonly property bool canZoomIn: Boolean(manager && manager.zoomInAvailable)
    readonly property bool canZoomOut: Boolean(manager && manager.zoomOutAvailable)
    readonly property real zoomValue: manager ? Number(manager.currentZoom) : 1.0

    implicitWidth: zoomColumn.implicitWidth
    implicitHeight: zoomColumn.implicitHeight

    function cancelZoomGesture() {
        var shouldStop = Boolean(zoomOutMouseArea && zoomOutMouseArea.gestureState === gestureHolding)
                         || Boolean(zoomInMouseArea && zoomInMouseArea.gestureState === gestureHolding)
                         || Boolean(manager && manager.continuousZoomActive)
        if (zoomOutMouseArea) {
            zoomOutMouseArea.consumeGesture()
        }
        if (zoomInMouseArea) {
            zoomInMouseArea.consumeGesture()
        }
        if (shouldStop && manager) {
            manager.cancelZoom()
        }
    }

    onOnlineChanged: {
        if (!online) {
            cancelZoomGesture()
        }
    }
    onVisibleChanged: {
        if (!visible) {
            cancelZoomGesture()
        } else {
            if (zoomOutMouseArea) {
                zoomOutMouseArea.resetGestureIfReleased()
            }
            if (zoomInMouseArea) {
                zoomInMouseArea.resetGestureIfReleased()
            }
        }
    }
    Component.onDestruction: cancelZoomGesture()

    Connections {
        target: Qt.application

        function onStateChanged() {
            if (Qt.application.state !== Qt.ApplicationActive) {
                root.cancelZoomGesture()
            } else {
                if (zoomOutMouseArea) {
                    zoomOutMouseArea.resetGestureIfReleased()
                }
                if (zoomInMouseArea) {
                    zoomInMouseArea.resetGestureIfReleased()
                }
            }
        }
    }

    Connections {
        target: root.manager
        ignoreUnknownSignals: true

        function onContinuousZoomActiveChanged() {
            if (!root.manager || !root.manager.continuousZoomActive) {
                if (zoomOutMouseArea) {
                    zoomOutMouseArea.consumeExternalStop()
                }
                if (zoomInMouseArea) {
                    zoomInMouseArea.consumeExternalStop()
                }
            }
        }
    }

    GridLayout {
        id: zoomColumn

        anchors.fill: parent
        columns: 1
        columnSpacing: 0
        rowSpacing: root.controlSpacing

        Rectangle {
            id: zoomOutButton

            Layout.row: 2
            Layout.column: 0
            Layout.preferredWidth: root.controlSize
            Layout.preferredHeight: root.controlSize
            Layout.alignment: Qt.AlignHCenter
            radius: width / 2
            color: zoomOutMouseArea.pressed ? "#f2ffffff" : (zoomOutMouseArea.containsMouse ? "#32ffffff" : "#1cffffff")
            border.color: zoomOutMouseArea.containsMouse ? "#d8ffffff" : "#78ffffff"
            border.width: 1
            enabled: root.canSend
                     && (zoomOutMouseArea.gestureState !== root.gestureIdle
                         || root.canZoomOut)
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

                property int gestureState: root.gestureIdle
                property real pressStartedAtMs: 0

                function consumeGesture() {
                    gestureState = pressed ? root.gestureConsumed : root.gestureIdle
                }

                function consumeExternalStop() {
                    if (gestureState === root.gestureHolding) {
                        consumeGesture()
                    }
                }

                function resetGestureIfReleased() {
                    if (!pressed) {
                        gestureState = root.gestureIdle
                    }
                }

                onPressed: {
                    pressStartedAtMs = Date.now()
                    gestureState = root.gesturePressed
                }
                onPressAndHold: {
                    if (gestureState !== root.gesturePressed) {
                        return
                    }

                    // Consume before calling the manager so a synchronous
                    // availability change cannot turn this hold into a tap.
                    gestureState = root.gestureConsumed
                    const totalPressDurationMs = Math.max(
                        420, Math.round(Date.now() - pressStartedAtMs))
                    if (root.manager
                            && root.manager.startZoomWithPressDuration(
                                -1, totalPressDurationMs)) {
                        gestureState = root.gestureHolding
                    }
                }
                onReleased: function(mouse) {
                    const completedState = gestureState
                    const releasedInside = mouse.x >= 0 && mouse.x <= width
                                           && mouse.y >= 0 && mouse.y <= height
                    gestureState = root.gestureIdle
                    if (completedState === root.gestureHolding && root.manager) {
                        root.manager.stopZoom()
                    } else if (completedState === root.gesturePressed
                               && releasedInside
                               && root.manager) {
                        root.manager.zoomOut()
                    }
                }
                onCanceled: {
                    const completedState = gestureState
                    gestureState = root.gestureIdle
                    if (completedState === root.gestureHolding && root.manager) {
                        root.manager.cancelZoom()
                    }
                }
                onExited: {
                    if (!pressed) {
                        return
                    }

                    const completedState = gestureState
                    gestureState = root.gestureConsumed
                    if (completedState === root.gestureHolding && root.manager) {
                        root.manager.cancelZoom()
                    }
                }
            }
        }

        Rectangle {
            Layout.row: 1
            Layout.column: 0
            Layout.preferredWidth: Math.max(root.controlSize,
                                            ScreenTools.defaultFontPixelWidth * 5.2)
            Layout.preferredHeight: root.controlSize * 0.78
            Layout.alignment: Qt.AlignHCenter
            radius: height / 2
            color: "#e8ffffff"
            border.color: "#80ffffff"
            border.width: 1

            QGCLabel {
                anchors.centerIn: parent
                // Each manager owns whether this is a target or feedback value.
                text: root.online && root.zoomKnown
                      ? root.zoomValue.toFixed(1) + "x"
                      : "--"
                color: "#101820"
                font.bold: true
                font.pointSize: ScreenTools.smallFontPointSize
            }
        }

        Rectangle {
            id: zoomInButton

            Layout.row: 0
            Layout.column: 0
            Layout.preferredWidth: root.controlSize
            Layout.preferredHeight: root.controlSize
            Layout.alignment: Qt.AlignHCenter
            radius: width / 2
            color: zoomInMouseArea.pressed ? "#f2ffffff" : (zoomInMouseArea.containsMouse ? "#32ffffff" : "#1cffffff")
            border.color: zoomInMouseArea.containsMouse ? "#d8ffffff" : "#78ffffff"
            border.width: 1
            enabled: root.canSend
                     && (zoomInMouseArea.gestureState !== root.gestureIdle
                         || root.canZoomIn)
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

                property int gestureState: root.gestureIdle
                property real pressStartedAtMs: 0

                function consumeGesture() {
                    gestureState = pressed ? root.gestureConsumed : root.gestureIdle
                }

                function consumeExternalStop() {
                    if (gestureState === root.gestureHolding) {
                        consumeGesture()
                    }
                }

                function resetGestureIfReleased() {
                    if (!pressed) {
                        gestureState = root.gestureIdle
                    }
                }

                onPressed: {
                    pressStartedAtMs = Date.now()
                    gestureState = root.gesturePressed
                }
                onPressAndHold: {
                    if (gestureState !== root.gesturePressed) {
                        return
                    }

                    // Consume before calling the manager so release cannot
                    // append a short-step command to continuous zoom.
                    gestureState = root.gestureConsumed
                    const totalPressDurationMs = Math.max(
                        420, Math.round(Date.now() - pressStartedAtMs))
                    if (root.manager
                            && root.manager.startZoomWithPressDuration(
                                1, totalPressDurationMs)) {
                        gestureState = root.gestureHolding
                    }
                }
                onReleased: function(mouse) {
                    const completedState = gestureState
                    const releasedInside = mouse.x >= 0 && mouse.x <= width
                                           && mouse.y >= 0 && mouse.y <= height
                    gestureState = root.gestureIdle
                    if (completedState === root.gestureHolding && root.manager) {
                        root.manager.stopZoom()
                    } else if (completedState === root.gesturePressed
                               && releasedInside
                               && root.manager) {
                        root.manager.zoomIn()
                    }
                }
                onCanceled: {
                    const completedState = gestureState
                    gestureState = root.gestureIdle
                    if (completedState === root.gestureHolding && root.manager) {
                        root.manager.cancelZoom()
                    }
                }
                onExited: {
                    if (!pressed) {
                        return
                    }

                    const completedState = gestureState
                    gestureState = root.gestureConsumed
                    if (completedState === root.gestureHolding && root.manager) {
                        root.manager.cancelZoom()
                    }
                }
            }
        }
    }
}
