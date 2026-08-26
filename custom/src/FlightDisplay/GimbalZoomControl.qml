/****************************************************************************
 *
 * Shared A8 Mini and MT11 zoom control. A short release asks the selected
 * manager for one exact step. An explicit 420 ms timer starts the manager-
 * owned hold sequence in both shared and hold-only ranges; a shorter release
 * in a hold-only range intentionally sends no zoom command. A held press can
 * retry a transiently unavailable start, but release, cancellation, manager
 * switching, hiding and application suspension cancel it. Device-specific
 * protocols, bounds and pacing remain in their managers.
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
    property bool showActualZoom: false
    property real controlSize: Math.max(ScreenTools.defaultFontPixelHeight * 2.35,
                                        ScreenTools.isMobile ? ScreenTools.minTouchPixels : 0)
    property real controlSpacing: ScreenTools.defaultFontPixelWidth * 0.45
    property color accentColor: "#65d9f4"
    property color buttonColor: "#321f2b36"
    property color buttonHoverColor: "#4a334653"
    property color buttonPressedColor: "#e8f2f7fa"
    property color buttonBorderColor: "#8065d9f4"
    property real buttonCornerRadius: Math.min(10, controlSize * 0.22)
    property var _previousManager: null

    readonly property int gestureIdle: 0
    readonly property int gesturePressed: 1
    readonly property int gestureHolding: 2
    readonly property int gestureConsumed: 3
    readonly property int holdThresholdMs: 420
    readonly property int holdStartRetryMs: 100
    readonly property bool online: Boolean(manager && manager.zoomControlsUnlocked)
    readonly property bool endpointReachable: Boolean(manager
                                                       && manager.enabled
                                                       && (manager.sdkResponding
                                                           || manager.zoomControlsUnlocked
                                                           || manager.continuousZoomActive))
    readonly property bool zoomKnown: Boolean(manager && manager.zoomStatusKnown)
    readonly property bool canSend: online
    readonly property bool canTapZoomIn: Boolean(manager && manager.zoomInTapAvailable)
    readonly property bool canTapZoomOut: Boolean(manager && manager.zoomOutTapAvailable)
    readonly property bool canHoldZoomIn: Boolean(manager && manager.zoomInHoldAvailable)
    readonly property bool canHoldZoomOut: Boolean(manager && manager.zoomOutHoldAvailable)
    readonly property bool canZoomIn: canTapZoomIn || canHoldZoomIn
    readonly property bool canZoomOut: canTapZoomOut || canHoldZoomOut
    readonly property real zoomValue: manager ? Number(manager.currentZoom) : 1.0
    readonly property bool actualZoomKnown: Boolean(showActualZoom
                                                     && manager
                                                     && manager.actualZoomKnown)
    readonly property real actualZoomValue: showActualZoom && manager
                                             ? Number(manager.actualZoom)
                                             : zoomValue

    implicitWidth: zoomColumn.implicitWidth
    implicitHeight: zoomColumn.implicitHeight

    function cancelZoomGesture(includeCurrentManager) {
        const cancelCurrentManager = includeCurrentManager === undefined
                                     ? true : Boolean(includeCurrentManager)
        const outManager = zoomOutMouseArea ? zoomOutMouseArea.managerAtPress : null
        const inManager = zoomInMouseArea ? zoomInMouseArea.managerAtPress : null
        const outShouldStop = Boolean(outManager
                                      && (zoomOutMouseArea.gestureState !== gestureIdle
                                          || outManager.continuousZoomActive))
        const inShouldStop = Boolean(inManager
                                     && (zoomInMouseArea.gestureState !== gestureIdle
                                         || inManager.continuousZoomActive))
        if (zoomOutMouseArea) {
            zoomOutMouseArea.consumeGesture()
            zoomOutMouseArea.clearPressContext()
        }
        if (zoomInMouseArea) {
            zoomInMouseArea.consumeGesture()
            zoomInMouseArea.clearPressContext()
        }
        if (outShouldStop && outManager) {
            outManager.cancelZoom()
        }
        if (inShouldStop && inManager && inManager !== outManager) {
            inManager.cancelZoom()
        }
        if (cancelCurrentManager && manager
                && manager !== outManager && manager !== inManager) {
            manager.cancelZoom()
        }
    }

    function beginHeldZoom(mouseArea, direction, pressDurationMs) {
        // Consume before calling the manager so a synchronous availability
        // change cannot turn this gesture into a tap on release.
        mouseArea.gestureState = gestureConsumed
        const gestureManager = mouseArea.managerAtPress
        if (gestureManager && gestureManager === manager
                && gestureManager.startZoomWithPressDuration(
                    direction, Math.max(0, pressDurationMs))) {
            mouseArea.gestureState = gestureHolding
            return true
        }
        return false
    }

    onEndpointReachableChanged: {
        if (!endpointReachable) {
            cancelZoomGesture()
        }
    }
    onManagerChanged: {
        const oldManager = _previousManager
        cancelZoomGesture(false)
        if (oldManager && oldManager !== manager) {
            // A reliable MT11 endpoint intentionally leaves its direction
            // latched for the next reverse press. Neutralize that latent state
            // when the camera selector moves away after the original press.
            oldManager.cancelZoom()
        }
        _previousManager = manager
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
    Component.onCompleted: _previousManager = manager
    Component.onDestruction: {
        cancelZoomGesture()
        _previousManager = null
    }

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
            radius: root.buttonCornerRadius
            color: zoomOutMouseArea.pressed
                   ? root.buttonPressedColor
                   : (zoomOutMouseArea.containsMouse ? root.buttonHoverColor : root.buttonColor)
            border.color: zoomOutMouseArea.containsMouse
                          ? root.accentColor : root.buttonBorderColor
            border.width: zoomOutMouseArea.containsMouse ? 2 : 1
            enabled: zoomOutMouseArea.gestureState !== root.gestureIdle
                     || (root.canSend && root.canZoomOut)
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
                property int gestureState: root.gestureIdle
                property real pressStartedAtMs: 0
                property bool tapAvailableAtPress: false
                property bool holdAvailableAtPress: false
                property bool holdStartPending: false
                property var managerAtPress: null

                function clearPressContext() {
                    zoomOutHoldThresholdTimer.stop()
                    holdStartPending = false
                    tapAvailableAtPress = false
                    holdAvailableAtPress = false
                    managerAtPress = null
                }

                function consumeGesture() {
                    holdStartPending = false
                    gestureState = pressed ? root.gestureConsumed : root.gestureIdle
                }

                function tryStartHeldZoom() {
                    if (!holdStartPending || !pressed) {
                        holdStartPending = false
                        return
                    }
                    if (!holdAvailableAtPress
                            || !managerAtPress
                            || managerAtPress !== root.manager
                            || !root.endpointReachable) {
                        holdStartPending = false
                        return
                    }
                    // Availability and the first UDP write can change at the
                    // one-shot pressAndHold boundary. Keep this same physical
                    // press eligible and retry only while the same manager is
                    // online; release/cancel/switch clears the pending start.
                    if (!root.online
                            || !managerAtPress.zoomOutHoldAvailable) {
                        return
                    }

                    const totalPressDurationMs = Math.max(
                        root.holdThresholdMs,
                        Math.round(Date.now() - pressStartedAtMs))
                    if (root.beginHeldZoom(zoomOutMouseArea,
                                           -1,
                                           totalPressDurationMs)) {
                        holdStartPending = false
                    } else if (!pressed || managerAtPress !== root.manager
                               || !root.endpointReachable) {
                        holdStartPending = false
                    }
                }

                function beginHoldAcquisition() {
                    if (gestureState !== root.gesturePressed || !pressed) {
                        return
                    }
                    // Qt's pressAndHold signal is one-shot and can disappear
                    // after a small Android pointer drift. Own the threshold
                    // explicitly, then retry this same captured press if the
                    // manager has a transient availability/write race.
                    gestureState = root.gestureConsumed
                    if (!holdAvailableAtPress || !managerAtPress
                            || managerAtPress !== root.manager
                            || !root.endpointReachable) {
                        return
                    }
                    holdStartPending = true
                    tryStartHeldZoom()
                }

                Timer {
                    id: zoomOutHoldThresholdTimer

                    interval: root.holdThresholdMs
                    repeat: false
                    onTriggered: zoomOutMouseArea.beginHoldAcquisition()
                }

                Timer {
                    interval: root.holdStartRetryMs
                    repeat: true
                    running: zoomOutMouseArea.holdStartPending
                    onTriggered: zoomOutMouseArea.tryStartHeldZoom()
                }

                function consumeExternalStop() {
                    if (gestureState === root.gestureHolding) {
                        consumeGesture()
                    }
                }

                function resetGestureIfReleased() {
                    if (!pressed) {
                        gestureState = root.gestureIdle
                        clearPressContext()
                    }
                }

                onPressed: {
                    pressStartedAtMs = Date.now()
                    gestureState = root.gesturePressed
                    managerAtPress = root.manager
                    // Availability can change while feedback settles. Freeze
                    // which gestures this physical press was allowed to own.
                    tapAvailableAtPress = root.canTapZoomOut
                    holdAvailableAtPress = root.canHoldZoomOut
                    zoomOutHoldThresholdTimer.restart()
                }
                onReleased: function(mouse) {
                    const completedState = gestureState
                    const tapWasAvailable = tapAvailableAtPress
                    const gestureManager = managerAtPress
                    const heldLongEnough = Date.now() - pressStartedAtMs
                                                   >= root.holdThresholdMs
                    const tapStillAvailable = Boolean(
                        gestureManager && gestureManager === root.manager
                        && gestureManager.zoomOutTapAvailable)
                    const releasedInside = mouse.x >= 0 && mouse.x <= width
                                           && mouse.y >= 0 && mouse.y <= height
                    gestureState = root.gestureIdle
                    clearPressContext()
                    if (completedState === root.gestureHolding && gestureManager) {
                        gestureManager.stopZoom()
                    } else if (completedState === root.gesturePressed
                               && releasedInside
                               && !heldLongEnough
                               && tapWasAvailable
                               && tapStillAvailable) {
                        gestureManager.zoomOut()
                    }
                }
                onCanceled: {
                    const completedState = gestureState
                    const gestureManager = managerAtPress
                    gestureState = root.gestureIdle
                    clearPressContext()
                    if (completedState === root.gestureHolding && gestureManager) {
                        gestureManager.cancelZoom()
                    }
                }
            }
        }

        Rectangle {
            Layout.row: 1
            Layout.column: 0
            Layout.preferredWidth: Math.max(
                                       root.controlSize,
                                       root.showActualZoom
                                           ? Math.max(targetZoomLabel.implicitWidth,
                                                      actualZoomLabel.implicitWidth)
                                             + root.controlSpacing * 2.0
                                           : ScreenTools.defaultFontPixelWidth
                                             * 5.2)
            Layout.preferredHeight: root.controlSize
                                    * (root.showActualZoom ? 1.08 : 0.78)
            Layout.alignment: Qt.AlignHCenter
            radius: height / 2
            color: "#66192630"
            border.color: root.online && root.zoomKnown
                          ? root.buttonBorderColor : "#5065d9f4"
            border.width: 1

            QGCLabel {
                anchors.centerIn: parent
                // Each manager owns whether this is a target or feedback value.
                visible: !root.showActualZoom
                text: root.online && root.zoomKnown
                      ? root.zoomValue.toFixed(1) + "x"
                      : "--"
                color: root.online && root.zoomKnown ? root.accentColor : "#9dabb3b9"
                font.bold: true
                font.pointSize: ScreenTools.smallFontPointSize
            }

            Column {
                id: zoomValueColumn

                anchors.centerIn: parent
                spacing: 0
                visible: root.showActualZoom

                QGCLabel {
                    id: targetZoomLabel

                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("Target") + " "
                          + (root.online && root.zoomKnown
                             ? root.zoomValue.toFixed(1) + "x"
                             : "--")
                    color: root.online && root.zoomKnown ? root.accentColor : "#9dabb3b9"
                    font.bold: true
                    font.pointSize: ScreenTools.smallFontPointSize * 0.82
                }

                QGCLabel {
                    id: actualZoomLabel

                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("Actual") + " "
                          + (root.online && root.actualZoomKnown
                             ? root.actualZoomValue.toFixed(1) + "x"
                             : "--")
                    color: "#ced9dfe3"
                    font.pointSize: ScreenTools.smallFontPointSize * 0.74
                }
            }
        }

        Rectangle {
            id: zoomInButton

            Layout.row: 0
            Layout.column: 0
            Layout.preferredWidth: root.controlSize
            Layout.preferredHeight: root.controlSize
            Layout.alignment: Qt.AlignHCenter
            radius: root.buttonCornerRadius
            color: zoomInMouseArea.pressed
                   ? root.buttonPressedColor
                   : (zoomInMouseArea.containsMouse ? root.buttonHoverColor : root.buttonColor)
            border.color: zoomInMouseArea.containsMouse
                          ? root.accentColor : root.buttonBorderColor
            border.width: zoomInMouseArea.containsMouse ? 2 : 1
            enabled: zoomInMouseArea.gestureState !== root.gestureIdle
                     || (root.canSend && root.canZoomIn)
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
                property int gestureState: root.gestureIdle
                property real pressStartedAtMs: 0
                property bool tapAvailableAtPress: false
                property bool holdAvailableAtPress: false
                property bool holdStartPending: false
                property var managerAtPress: null

                function clearPressContext() {
                    zoomInHoldThresholdTimer.stop()
                    holdStartPending = false
                    tapAvailableAtPress = false
                    holdAvailableAtPress = false
                    managerAtPress = null
                }

                function consumeGesture() {
                    holdStartPending = false
                    gestureState = pressed ? root.gestureConsumed : root.gestureIdle
                }

                function tryStartHeldZoom() {
                    if (!holdStartPending || !pressed) {
                        holdStartPending = false
                        return
                    }
                    if (!holdAvailableAtPress
                            || !managerAtPress
                            || managerAtPress !== root.manager
                            || !root.endpointReachable) {
                        holdStartPending = false
                        return
                    }
                    if (!root.online
                            || !managerAtPress.zoomInHoldAvailable) {
                        return
                    }

                    const totalPressDurationMs = Math.max(
                        root.holdThresholdMs,
                        Math.round(Date.now() - pressStartedAtMs))
                    if (root.beginHeldZoom(zoomInMouseArea,
                                           1,
                                           totalPressDurationMs)) {
                        holdStartPending = false
                    } else if (!pressed || managerAtPress !== root.manager
                               || !root.endpointReachable) {
                        holdStartPending = false
                    }
                }

                function beginHoldAcquisition() {
                    if (gestureState !== root.gesturePressed || !pressed) {
                        return
                    }
                    gestureState = root.gestureConsumed
                    if (!holdAvailableAtPress || !managerAtPress
                            || managerAtPress !== root.manager
                            || !root.endpointReachable) {
                        return
                    }
                    holdStartPending = true
                    tryStartHeldZoom()
                }

                Timer {
                    id: zoomInHoldThresholdTimer

                    interval: root.holdThresholdMs
                    repeat: false
                    onTriggered: zoomInMouseArea.beginHoldAcquisition()
                }

                Timer {
                    interval: root.holdStartRetryMs
                    repeat: true
                    running: zoomInMouseArea.holdStartPending
                    onTriggered: zoomInMouseArea.tryStartHeldZoom()
                }

                function consumeExternalStop() {
                    if (gestureState === root.gestureHolding) {
                        consumeGesture()
                    }
                }

                function resetGestureIfReleased() {
                    if (!pressed) {
                        gestureState = root.gestureIdle
                        clearPressContext()
                    }
                }

                onPressed: {
                    pressStartedAtMs = Date.now()
                    gestureState = root.gesturePressed
                    managerAtPress = root.manager
                    tapAvailableAtPress = root.canTapZoomIn
                    holdAvailableAtPress = root.canHoldZoomIn
                    zoomInHoldThresholdTimer.restart()
                }
                onReleased: function(mouse) {
                    const completedState = gestureState
                    const tapWasAvailable = tapAvailableAtPress
                    const gestureManager = managerAtPress
                    const heldLongEnough = Date.now() - pressStartedAtMs
                                                   >= root.holdThresholdMs
                    const tapStillAvailable = Boolean(
                        gestureManager && gestureManager === root.manager
                        && gestureManager.zoomInTapAvailable)
                    const releasedInside = mouse.x >= 0 && mouse.x <= width
                                           && mouse.y >= 0 && mouse.y <= height
                    gestureState = root.gestureIdle
                    clearPressContext()
                    if (completedState === root.gestureHolding && gestureManager) {
                        gestureManager.stopZoom()
                    } else if (completedState === root.gesturePressed
                               && releasedInside
                               && !heldLongEnough
                               && tapWasAvailable
                               && tapStillAvailable) {
                        gestureManager.zoomIn()
                    }
                }
                onCanceled: {
                    const completedState = gestureState
                    const gestureManager = managerAtPress
                    gestureState = root.gestureIdle
                    clearPressContext()
                    if (completedState === root.gestureHolding && gestureManager) {
                        gestureManager.cancelZoom()
                    }
                }
            }
        }
    }
}
