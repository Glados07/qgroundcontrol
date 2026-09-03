/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.MultiVehicleManager
import QGroundControl.ScreenTools
import QGroundControl.Palette
import QGroundControl.FactSystem
import QGroundControl.FactControls

Item {
    id:             control
    width:          gimbalIndicatorIcon.width * 1.1 + gimbalTelemetryLayout.childrenRect.width + margins
    anchors.top:    parent.top
    anchors.bottom: parent.bottom

    property var    activeVehicle:              QGroundControl.multiVehicleManager.activeVehicle
    property var    gimbalController:           activeVehicle ? activeVehicle.gimbalController : null
    property bool   showIndicator:              gimbalController && gimbalController.gimbals.count
    property var    activeGimbal:               gimbalController ? gimbalController.activeGimbal : null
    property bool   multiGimbalSetup:           gimbalController && gimbalController.gimbals.count > 1
    property bool   joystickButtonsAvailable:   activeVehicle && activeVehicle.joystickEnabled
    readonly property var _gimbalCenterCoordinator: QGroundControl.corePlugin
                                                     && QGroundControl.corePlugin.gimbalCenterCoordinator !== undefined
                                                     ? QGroundControl.corePlugin.gimbalCenterCoordinator
                                                     : null
    readonly property var _gimbalAzimuthProvider: QGroundControl.corePlugin
                                                  && QGroundControl.corePlugin.gimbalAzimuthProvider !== undefined
                                                  ? QGroundControl.corePlugin.gimbalAzimuthProvider
                                                  : null
    readonly property real _gimbalAbsoluteYaw: _gimbalAzimuthProvider && _gimbalAzimuthProvider.valid
                                                ? Number(_gimbalAzimuthProvider.absoluteYaw)
                                                : NaN

    property var    margins:                    ScreenTools.defaultFontPixelWidth
    property var    panelRadius:                ScreenTools.defaultFontPixelWidth * 0.5
    property var    buttonHeight:               height * 1.6
    property var    squareButtonPadding:        ScreenTools.defaultFontPixelWidth
    property var    separatorHeight:            buttonHeight * 0.9
    property var    settingsPanelVisible:       false

    // Keep only the latest toolbar posture action for one immutable context.
    property var    _pendingOwnershipAction:    null
    property var    _pendingVehicle:            null
    property var    _pendingController:         null
    property var    _pendingGimbal:             null
    property int    _pendingGeneration:         0
    property bool   _pendingCheckScheduled:     false
    property bool   _pendingAcquireSent:        false
    property bool   _centerPrimerAwaitingAck:   false
    property bool   _centerReplayScheduled:     false
    property int    _centerPrimerGeneration:    -1
    property int    _centerReplayGeneration:    -1
    property int    _centerPrimerVehicleId:     -1
    property int    _centerPrimerManagerCompid: -1
    property bool   _centerFinalAwaitingAck:    false
    property int    _centerFinalVehicleId:      -1
    property int    _centerFinalManagerCompid:  -1
    property string _centerFinalPrimerKey:      ""
    // Remember stale-target risk per Vehicle/manager/device instead of as one
    // global bit, so switching between native multi-gimbal entries cannot
    // accidentally clear another gimbal's required wake-up.
    property var    _centerPrimerRequiredKeys:  ({})
    property bool   _toolbarPostureDispatchInProgress: false

    // Covers the slow 0.2 Hz ownership-status fallback, one normal 3-second
    // COMMAND_ACK window for the Center primer, and its short settle delay.
    readonly property int _pendingOwnershipTimeoutMs: 10000
    readonly property int _centerFinalAckTimeoutMs:     4000
    // MAV_CMD_DO_GIMBAL_MANAGER_PITCHYAW and MAV_RESULT_ACCEPTED are not
    // exposed through the MAVLink QML enum wrapper in this QGC version.
    readonly property int _mavCmdGimbalManagerPitchYaw: 1000
    readonly property int _mavResultAccepted:             0
    readonly property int _mavCmdResultCommandOnly:       0
    // Center and Tilt 90 prove that this project gimbal accepts [-90, 0].
    // Keep the wake-up target inside that known-safe interval.
    readonly property real _centerPrimerPitchMin:          -90.0
    readonly property real _centerPrimerPitchMax:            0.0
    readonly property real _centerPrimerPitchStep:           1.0

    function _hasConfirmedOwnership(gimbal) {
        return !!gimbal
                && gimbal.gimbalHaveControl
                && !gimbal.gimbalOthersHaveControl
    }

    function _centerPrimerKey(vehicle, gimbal) {
        if (!vehicle || !gimbal) {
            return ""
        }

        var vehicleId = Number(vehicle.id)
        var managerCompid = Number(gimbal.managerCompid.rawValue)
        var deviceId = Number(gimbal.deviceId.rawValue)
        if (!isFinite(vehicleId)
                || !isFinite(managerCompid)
                || !isFinite(deviceId)) {
            return ""
        }

        return vehicleId + ":" + managerCompid + ":" + deviceId
    }

    function _centerPrimerIsRequired(vehicle, gimbal) {
        var key = _centerPrimerKey(vehicle, gimbal)
        return key !== "" && _centerPrimerRequiredKeys[key] === true
    }

    function _markCenterPrimerRequired(vehicle, gimbal) {
        var key = _centerPrimerKey(vehicle, gimbal)
        if (key !== "") {
            _centerPrimerRequiredKeys[key] = true
        }
    }

    function _clearCenterFinalAckWait() {
        _centerFinalAwaitingAck = false
        _centerFinalVehicleId = -1
        _centerFinalManagerCompid = -1
        _centerFinalPrimerKey = ""
        centerFinalAckTimeout.stop()
    }

    function _pendingContextIsCurrent() {
        return _pendingOwnershipAction !== null
                && activeVehicle === _pendingVehicle
                && gimbalController === _pendingController
                && activeGimbal === _pendingGimbal
    }

    function _clearPendingOwnershipAction() {
        ++_pendingGeneration
        _pendingOwnershipAction = null
        _pendingVehicle = null
        _pendingController = null
        _pendingGimbal = null
        _pendingCheckScheduled = false
        _pendingAcquireSent = false
        _centerPrimerAwaitingAck = false
        _centerReplayScheduled = false
        _centerPrimerGeneration = -1
        _centerReplayGeneration = -1
        _centerPrimerVehicleId = -1
        _centerPrimerManagerCompid = -1
        pendingOwnershipTimeout.stop()
        centerReplayDelay.stop()
    }

    function _invokeOwnershipAction(controller, gimbal, action) {
        _toolbarPostureDispatchInProgress = true
        try {
            switch (action.id) {
            case "yawLock":
                controller.toggleGimbalYawLock(action.value)
                break
            case "center":
                controller.centerGimbal()
                break
            case "tilt90":
                controller.sendPitchBodyYaw(-90, 0)
                break
            case "retract":
                controller.toggleGimbalRetracted(true)
                break
            }
        } finally {
            _toolbarPostureDispatchInProgress = false
        }
    }

    function _invokeCenterPrimer(controller, gimbal) {
        // Cache telemetry before calling into C++: sendPitchBodyYaw
        // immediately sets the local pitch Fact to zero during dispatch.
        var currentPitch = Number(gimbal.absolutePitch.rawValue)
        if (!isFinite(currentPitch)) {
            currentPitch = _centerPrimerPitchMax
        }

        // A same-pose sample is still a no-op in the affected RC/MAVLink
        // bridge. Clamp the telemetry to the two native toolbar endpoints,
        // then move exactly one degree. The split keeps the result non-zero,
        // in range, and different from the reported/clamped pitch even at
        // either endpoint.
        var boundedPitch = Math.max(_centerPrimerPitchMin,
                                    Math.min(_centerPrimerPitchMax, currentPitch))
        var pitch = boundedPitch <= _centerPrimerPitchMax - 2 * _centerPrimerPitchStep
                ? boundedPitch + _centerPrimerPitchStep
                : boundedPitch - _centerPrimerPitchStep

        _toolbarPostureDispatchInProgress = true
        try {
            // Use the exact body-yaw coordinate mode and yaw target of native
            // Center. Only pitch differs. This avoids an unnecessary
            // earth/body-frame transition and also stops the native 500 ms
            // rate timer before waiting for this command-1000 result.
            controller.sendPitchBodyYaw(pitch, 0, false)
        } finally {
            _toolbarPostureDispatchInProgress = false
        }
    }

    function _schedulePendingOwnershipCheck() {
        if (_pendingOwnershipAction === null || _pendingCheckScheduled) {
            return
        }

        var generation = _pendingGeneration
        _pendingCheckScheduled = true
        Qt.callLater(function() {
            if (control._pendingGeneration !== generation) {
                return
            }
            control._pendingCheckScheduled = false
            control._reviewPendingOwnership()
        })
    }

    function _reviewPendingOwnership() {
        if (_pendingOwnershipAction === null) {
            return
        }
        if (_centerPrimerAwaitingAck || _centerReplayScheduled) {
            return
        }
        if (!_pendingContextIsCurrent()) {
            _clearPendingOwnershipAction()
            return
        }
        if (!_hasConfirmedOwnership(_pendingGimbal)) {
            // Ownership may be lost between the click-time snapshot and this
            // deferred stable check. Send the one allowed CONFIGURE here too,
            // rather than waiting silently until the ten-second timeout.
            if (!_pendingAcquireSent) {
                _pendingAcquireSent = true
                _pendingController.acquireGimbalControl()
            }
            return
        }

        var generation = _pendingGeneration
        var action = _pendingOwnershipAction
        var controller = _pendingController
        var gimbal = _pendingGimbal

        if (action.id === "center") {
            // Some RC -> gimbal output chains keep the previous MAVLink 0/0
            // target cached while RC is active. Their first post-takeover 0/0
            // is then acknowledged but produces no update. Prime that chain
            // with a bounded, definitely changed pitch target, and replay
            // Center only after its ACK.
            _centerPrimerAwaitingAck = true
            _centerPrimerGeneration = generation
            _centerPrimerVehicleId = Number(_pendingVehicle.id)
            _centerPrimerManagerCompid = Number(gimbal.managerCompid.rawValue)
            _invokeCenterPrimer(controller, gimbal)
            if (_pendingGeneration !== generation) {
                return
            }
            return
        }

        _invokeOwnershipAction(controller, gimbal, action)

        // The popup handler synchronously invalidates this generation if RC
        // retakes control immediately before replay. Never auto-acquire again.
        if (_pendingGeneration !== generation) {
            return
        }
        _clearPendingOwnershipAction()
    }

    function _dispatchOwnershipAction(action) {
        if (_gimbalCenterCoordinator) {
            _gimbalCenterCoordinator.cancel()
        }
        var vehicle = activeVehicle
        var controller = gimbalController
        var gimbal = activeGimbal
        if (!vehicle || !controller || !gimbal) {
            return
        }

        if (_pendingOwnershipAction !== null) {
            if (_pendingContextIsCurrent()) {
                // Last gesture wins. Do not send another CONFIGURE and do not
                // reset the original ten-second deadline.
                _pendingOwnershipAction = action
                if (action.id === "center") {
                    _markCenterPrimerRequired(vehicle, gimbal)
                }
                _schedulePendingOwnershipCheck()
                return
            }
            _clearPendingOwnershipAction()
        }

        var hasOwnership = _hasConfirmedOwnership(gimbal)
        if (hasOwnership
                && (action.id !== "center"
                    || !_centerPrimerIsRequired(vehicle, gimbal))) {
            _invokeOwnershipAction(controller, gimbal, action)
            return
        }

        if (action.id === "center") {
            _markCenterPrimerRequired(vehicle, gimbal)
        }
        ++_pendingGeneration
        _pendingOwnershipAction = action
        _pendingVehicle = vehicle
        _pendingController = controller
        _pendingGimbal = gimbal
        _pendingCheckScheduled = false
        _pendingAcquireSent = false
        pendingOwnershipTimeout.restart()

        // One pending sequence sends exactly one automatic acquire request.
        // Never fight continuous RC ownership with repeated CONFIGURE commands.
        if (!hasOwnership) {
            _pendingAcquireSent = true
            controller.acquireGimbalControl()
        }
        _schedulePendingOwnershipCheck()
    }

    function _requestCenter() {
        if (_gimbalCenterCoordinator) {
            _gimbalCenterCoordinator.requestCenter()
        } else {
            _dispatchOwnershipAction({id: "center"})
        }
    }

    function _handleCenterPrimerResult(vehicleId, targetComponent, command, ackResult, failureCode) {
        if (!_centerPrimerAwaitingAck
                || _pendingOwnershipAction === null
                || !_pendingContextIsCurrent()
                || _centerPrimerGeneration !== _pendingGeneration
                || vehicleId !== _centerPrimerVehicleId
                || command !== _mavCmdGimbalManagerPitchYaw
                || targetComponent !== _centerPrimerManagerCompid) {
            return
        }

        _centerPrimerAwaitingAck = false
        if (ackResult !== _mavResultAccepted
                || failureCode !== _mavCmdResultCommandOnly) {
            _clearPendingOwnershipAction()
            return
        }

        // Vehicle removes the first command from its pending list before
        // emitting mavCommandResult. A short settle window also lets a
        // downstream MAVLink-v1/vendor bridge latch the priming setpoint.
        _centerReplayScheduled = true
        _centerReplayGeneration = _pendingGeneration
        centerReplayDelay.restart()
    }

    function _handleCenterFinalResult(vehicleId, targetComponent, command, ackResult, failureCode) {
        if (!_centerFinalAwaitingAck
                || vehicleId !== _centerFinalVehicleId
                || command !== _mavCmdGimbalManagerPitchYaw
                || targetComponent !== _centerFinalManagerCompid) {
            return
        }

        var primerKey = _centerFinalPrimerKey
        _clearCenterFinalAckWait()
        if (ackResult === _mavResultAccepted
                && failureCode === _mavCmdResultCommandOnly
                && primerKey !== "") {
            delete _centerPrimerRequiredKeys[primerKey]
        }
    }

    function _finishCenterReplay() {
        if (!_centerReplayScheduled) {
            return
        }
        if (_pendingOwnershipAction === null
                || _centerReplayGeneration !== _pendingGeneration) {
            _clearPendingOwnershipAction()
            return
        }
        _centerReplayScheduled = false

        if (!_pendingContextIsCurrent()
                || !_hasConfirmedOwnership(_pendingGimbal)) {
            _clearPendingOwnershipAction()
            return
        }

        var generation = _pendingGeneration
        var action = _pendingOwnershipAction
        var controller = _pendingController
        var gimbal = _pendingGimbal
        if (action.id === "center") {
            // Keep the stale-target marker until this final command's own ACK
            // is accepted. A synchronous Duplicate is therefore a safe
            // failure and the next Center will still run the primer.
            _clearCenterFinalAckWait()
            _centerFinalAwaitingAck = true
            _centerFinalVehicleId = Number(_pendingVehicle.id)
            _centerFinalManagerCompid = Number(gimbal.managerCompid.rawValue)
            _centerFinalPrimerKey = _centerPrimerKey(_pendingVehicle, gimbal)
        }
        _invokeOwnershipAction(controller, gimbal, action)
        if (_pendingGeneration !== generation) {
            if (action.id === "center") {
                _clearCenterFinalAckWait()
            }
            return
        }
        if (action.id === "center" && _centerFinalAwaitingAck) {
            centerFinalAckTimeout.restart()
        }
        _clearPendingOwnershipAction()
    }

    onActiveVehicleChanged: {
        if (_pendingOwnershipAction !== null) {
            _clearPendingOwnershipAction()
        }
        _clearCenterFinalAckWait()
    }
    onGimbalControllerChanged: {
        if (_pendingOwnershipAction !== null) {
            _clearPendingOwnershipAction()
        }
    }
    onActiveGimbalChanged: {
        if (_pendingOwnershipAction !== null) {
            _clearPendingOwnershipAction()
        }
        if (activeGimbal && !_hasConfirmedOwnership(activeGimbal)) {
            _markCenterPrimerRequired(activeVehicle, activeGimbal)
        }
    }

    Component.onDestruction: {
        _clearPendingOwnershipAction()
        _clearCenterFinalAckWait()
    }

    Timer {
        id:          pendingOwnershipTimeout
        interval:    control._pendingOwnershipTimeoutMs
        repeat:      false
        onTriggered: control._clearPendingOwnershipAction()
    }

    Timer {
        id:          centerReplayDelay
        // COMMAND_ACK only means that the manager accepted the primer. Give
        // the slower FC -> vendor-gimbal output bridge time to publish/latch
        // that changed target before sending 0/0.
        interval:    400
        repeat:      false
        onTriggered: control._finishCenterReplay()
    }

    Timer {
        id:          centerFinalAckTimeout
        interval:    control._centerFinalAckTimeoutMs
        repeat:      false
        onTriggered: control._clearCenterFinalAckWait()
    }

    // Popup panel, appears when clicking top toolbar gimbal indicator
    Component {
        id: gimbalControlsPage

        ToolIndicatorPage {
            contentComponent: GridLayout {
                // Label indicating the purpose of the panel and active gimbal instance
                QGCLabel {
                    text:                   qsTr("Gimbal ") +
                                                (multiGimbalSetup ? activeGimbal.deviceId.rawValue : "") +
                                                    qsTr("<br> Controls")

                    font.pointSize:         ScreenTools.smallFontPointSize
                    Layout.preferredWidth:  buttonHeight * 1.1
                    font.weight:            Font.DemiBold
                }

                // These are simple buttons that can be grouped on this Repeater
                Repeater {
                    id: simpleGimbalButtonsRepeater
                    property var hasControl:              gimbalController && gimbalController.activeGimbal && gimbalController.activeGimbal.gimbalHaveControl
                    property var acqControlButtonEnabled: QGroundControl.settingsManager.gimbalControllerSettings.toolbarIndicatorShowAcquireReleaseControl.rawValue

                    model: [
                        {id: "yawLock",   text: activeGimbal.yawLock ? qsTr("Yaw <br> Follow") : qsTr("Yaw <br> Lock")  , visible: true                    },
                        {id: "center",    text: qsTr("Center")                                                          , visible: true                    },
                        {id: "tilt90",    text: qsTr("Tilt 90")                                                         , visible: true                    },
                        {id: "pointHome", text: qsTr("Point <br> Home")                                                 , visible: true                    },
                        {id: "retract",   text: qsTr("Retract")                                                         , visible: true                    },
                        {id: "acqControl",text: hasControl ? qsTr("Release <br> Control") : qsTr("Acquire <br> Control"), visible: acqControlButtonEnabled }
                    ]

                    QGCButton {
                        property var callbackList: [
                           {"yawLock":      function(){ control._dispatchOwnershipAction({id: "yawLock", value: !activeGimbal.yawLock}) } },
                           {"center":       function(){ control._requestCenter() }                                                        },
                           {"tilt90":       function(){ control._dispatchOwnershipAction({id: "tilt90"}) }                                },
                           // PointHome is a Vehicle ROI command, not a direct
                           // gimbal posture command, so it bypasses ownership.
                           {"pointHome":    function(){
                                if (control._gimbalCenterCoordinator) {
                                    control._gimbalCenterCoordinator.cancel()
                                }
                                if (control._pendingOwnershipAction !== null) {
                                    control._clearPendingOwnershipAction()
                                }
                                activeVehicle.guidedModeROI(activeVehicle.homePosition)
                            } },
                           {"retract":      function(){ control._dispatchOwnershipAction({id: "retract"}) }                               },
                           // This button changes its action depending on gimbal being under control or not
                           {"acqControl":   function(){
                                if (control._gimbalCenterCoordinator) {
                                    control._gimbalCenterCoordinator.cancel()
                                }
                                if (control._pendingOwnershipAction !== null) {
                                    control._clearPendingOwnershipAction()
                                }
                                simpleGimbalButtonsRepeater.hasControl ?
                                    gimbalController.releaseGimbalControl() :
                                        gimbalController.acquireGimbalControl()
                            } }
                        ]

                        Layout.preferredWidth: Layout.preferredHeight
                        Layout.preferredHeight: buttonHeight
                        Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
                        text: modelData.text
                        fontWeight: Font.DemiBold
                        visible: modelData.visible
                        pointSize: ScreenTools.smallFontPointSize
                        backRadius: panelRadius * 0.5
                        leftPadding: squareButtonPadding
                        rightPadding: squareButtonPadding
                        onClicked: {
                            var callback = callbackList.find(function(item) {
                                return item.hasOwnProperty(modelData.id);
                            });
                            if (callback !== undefined) {
                                callback[modelData.id]();
                            }
                        }
                    }
                }

                // Separator
                Rectangle {
                    Layout.leftMargin:      margins
                    Layout.preferredWidth:  2
                    Layout.preferredHeight: separatorHeight
                    color:                  qgcPal.windowShade
                    visible:                multiGimbalSetup
                }

                // Active gimbal selector section
                QGCLabel {
                    text:                   qsTr("Active <br> Gimbal: ") + activeGimbal.deviceId.rawValue
                    font.pointSize:         ScreenTools.smallFontPointSize
                    Layout.preferredWidth:  buttonHeight * 1.1
                    Layout.leftMargin:      margins
                    font.weight:            Font.DemiBold
                    visible:                multiGimbalSetup
                }
                QGCButton {
                    id:                     gimbalSelectorButton
                    Layout.preferredWidth:  Layout.preferredHeight
                    Layout.preferredHeight: buttonHeight
                    Layout.alignment:       Qt.AlignHCenter | Qt.AlignBottom
                    text:                   qsTr("Select <br> Gimbal")
                    fontWeight:             Font.DemiBold
                    pointSize:              ScreenTools.smallFontPointSize
                    backRadius:             panelRadius * 0.5
                    visible:                multiGimbalSetup
                    checkable:              true

                    // This rectangle is to hide the "roundness" of panels when showing the dropdown, in the join between the 2 panels
                    Rectangle {
                        id:                         hideRoundCornersRectangle
                        anchors.verticalCenter:     gimbalSelectorPanel.top
                        anchors.horizontalCenter:   gimbalSelectorPanel.horizontalCenter
                        width:                      gimbalSelectorPanel.width
                        height:                     panelRadius * 2
                        color:                      qgcPal.window
                        visible:                    gimbalSelectorPanel.visible
                    }

                    Rectangle {
                        id:                         gimbalSelectorPanel
                        width:                      buttonHeight + margins * 2
                        height:                     gimbalSelectorContentGrid.childrenRect.height + margins * 2
                        visible:                    gimbalSelectorButton.checked
                        color:                      qgcPal.window
                        radius:                     panelRadius
                        // We only show border if the extended settings panel is visible
                        border.color:               settingsPanelVisible ? qgcPal.windowShade : qgcPal.window
                        border.width:               5

                        anchors.top:                parent.bottom
                        anchors.horizontalCenter:   parent.horizontalCenter
                        anchors.topMargin:          margins

                        property var buttonWidth:    width - margins * 2
                        property var panelHeight:    gimbalSelectorContentGrid.childrenRect.height + margins * 2
                        property var gridRowSpacing: margins
                        property var buttonFontSize: ScreenTools.smallFontPointSize * 0.9

                        GridLayout {
                            id:               gimbalSelectorContentGrid
                            width:            parent.width
                            rowSpacing:       gimbalSelectorPanel.gridRowSpacing
                            columns:          1

                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.top:              parent.top
                            anchors.topMargin:        margins

                            Repeater {
                                model: gimbalController && gimbalController.gimbals ? gimbalController.gimbals : undefined
                                delegate: QGCButton {
                                    Layout.preferredWidth:  Layout.preferredHeight
                                    Layout.preferredHeight: buttonHeight
                                    Layout.alignment:       Qt.AlignHCenter | Qt.AlignVCenter
                                    fontWeight:             Font.DemiBold
                                    pointSize:              ScreenTools.smallFontPointSize
                                    backRadius:             panelRadius * 0.5
                                    text:                   qsTr("Gimbal ") + object.deviceId.rawValue
                                    checked:                activeGimbal === object
                                    onClicked: {
                                        gimbalController.activeGimbal = object
                                        gimbalSelectorButton.checked = false
                                    }
                                }
                            }
                        }
                    }
                }

                // Separator
                Rectangle {
                    Layout.leftMargin:      margins
                    Layout.preferredWidth:  2
                    Layout.preferredHeight: separatorHeight
                    color:                  qgcPal.windowShade
                }

                // Show settings button. It is thought for persisting popup close actions, hence the visibility
                // based on a control.settingsPanelVisible It is interesting as users calibrating onscreen controls
                // will be testing and adjusting these frequently, so this way it is handier for them
                QGCButton {
                    id:                     extendedOptionsButton
                    Layout.leftMargin:      margins
                    Layout.preferredWidth:  Layout.preferredHeight
                    Layout.preferredHeight: buttonHeight
                    Layout.alignment:       Qt.AlignHCenter | Qt.AlignBottom
                    text:                   qsTr("Settings")
                    fontWeight:             Font.DemiBold
                    pointSize:              ScreenTools.smallFontPointSize
                    backRadius:             panelRadius * 0.5
                    checkable:              true
                    checked:                control.settingsPanelVisible
                    leftPadding:            squareButtonPadding
                    rightPadding:           squareButtonPadding
                    onCheckedChanged: {
                        if (checked !== control.settingsPanelVisible) {
                            control.settingsPanelVisible = checked
                        }
                    }
                }

                // Settings panel
                GridLayout {
                    Layout.row:         2
                    Layout.columnSpan:  8
                    Layout.fillWidth:   true
                    height:             buttonHeight * 1.5
                    visible:            settingsPanelVisible
                    columns:            2
                    rowSpacing:         margins

                    // Click on screen settings
                    FactCheckBox {
                        id:                 enableOnScreenControlCheckbox
                        text:               "  " + QGroundControl.settingsManager.gimbalControllerSettings.EnableOnScreenControl.shortDescription
                        fact:               QGroundControl.settingsManager.gimbalControllerSettings.EnableOnScreenControl
                        checkedValue:       1
                        uncheckedValue:     0
                        Layout.columnSpan:  2
                    }

                    QGCLabel {
                        id:                 controlTypeLabel
                        text:               qsTr("Control type: ")
                        visible:            enableOnScreenControlCheckbox.checked
                    }
                    FactComboBox {
                        id:                 controlTypeCombo
                        fact:               QGroundControl.settingsManager.gimbalControllerSettings.ControlType
                        visible:            enableOnScreenControlCheckbox.checked
                    }

                    QGCLabel {
                        text:               qsTr("Horizontal FOV")
                        visible:            enableOnScreenControlCheckbox.checked && QGroundControl.settingsManager.gimbalControllerSettings.ControlType.rawValue === 0
                    }
                    FactTextField {
                        fact:               QGroundControl.settingsManager.gimbalControllerSettings.CameraHFov
                        visible:            enableOnScreenControlCheckbox.checked && QGroundControl.settingsManager.gimbalControllerSettings.ControlType.rawValue === 0
                    }

                    QGCLabel {
                        text:               qsTr("Vertical FOV")
                        visible:            enableOnScreenControlCheckbox.checked && QGroundControl.settingsManager.gimbalControllerSettings.ControlType.rawValue === 0
                    }
                    FactTextField {
                        fact:               QGroundControl.settingsManager.gimbalControllerSettings.CameraVFov
                        visible:            enableOnScreenControlCheckbox.checked && QGroundControl.settingsManager.gimbalControllerSettings.ControlType.rawValue === 0
                    }

                    QGCLabel {
                        text:               qsTr("Max speed:")
                        visible:            enableOnScreenControlCheckbox.checked && QGroundControl.settingsManager.gimbalControllerSettings.ControlType.rawValue === 1
                    }
                    FactTextField {
                        fact:               QGroundControl.settingsManager.gimbalControllerSettings.CameraSlideSpeed
                        visible:            enableOnScreenControlCheckbox.checked && QGroundControl.settingsManager.gimbalControllerSettings.ControlType.rawValue === 1
                    }

                    // Separator
                    Rectangle {
                        Layout.columnSpan:       2
                        Layout.preferredHeight:  2
                        Layout.preferredWidth:   gimbalAzimuthMapCheckbox.width
                        Layout.margins:          margins
                        color:                   qgcPal.windowShade
                    }

                    QGCLabel {
                        text:               qsTr("Joystick buttons speed:")
                        visible:            joystickButtonsAvailable && QGroundControl.settingsManager.gimbalControllerSettings.visible
                    }
                    FactTextField {
                        fact:               QGroundControl.settingsManager.gimbalControllerSettings.joystickButtonsSpeed
                        visible:            joystickButtonsAvailable && QGroundControl.settingsManager.gimbalControllerSettings.visible
                        showHelp:           true
                    }

                    // Separator
                    Rectangle {
                        Layout.columnSpan:       2
                        Layout.preferredHeight:  2
                        Layout.preferredWidth:   gimbalAzimuthMapCheckbox.width
                        Layout.margins:          margins
                        color:                   qgcPal.windowShade
                        visible:                 joystickButtonsAvailable && QGroundControl.settingsManager.gimbalControllerSettings.visible
                    }

                    FactCheckBox {
                        id:                 gimbalAzimuthMapCheckbox
                        text:               "  " + qsTr("Show gimbal Azimuth indicator in map")
                        fact:               QGroundControl.settingsManager.gimbalControllerSettings.showAzimuthIndicatorOnMap
                        Layout.columnSpan:  2
                        checkedValue:       1
                        uncheckedValue:     0
                    }

                    FactCheckBox {
                        id:                 gimbalAzimutIndicatorCheckbox
                        text:               "  " + qsTr("Use Azimuth instead of local yaw on top toolbar indicator")
                        fact:               QGroundControl.settingsManager.gimbalControllerSettings.toolbarIndicatorShowAzimuth
                        Layout.columnSpan:  2
                        checkedValue:       1
                        uncheckedValue:     0
                    }

                    FactCheckBox {
                        id:                 showAcquireControlCheckbox
                        text:               "  " + qsTr("Show Acquire/Release control button")
                        fact:               QGroundControl.settingsManager.gimbalControllerSettings.toolbarIndicatorShowAcquireReleaseControl
                        Layout.columnSpan:  2
                        checkedValue:       1
                        uncheckedValue:     0
                    }
                }
            }
        }
    }

    // Icon plus active instance indicator
    QGCColoredImage {
        id:                      gimbalIndicatorIcon
        width:                   height
        anchors.top:             parent.top
        anchors.bottom:          parent.bottom
        source:                  "/gimbal/payload.svg"
        fillMode:                Image.PreserveAspectFit
        sourceSize.height:       height
        color:                   qgcPal.buttonText

        // Current active gimbal indicator
        QGCLabel {
            id:                  gimbalInstanceIndicatorLabel
            text:                activeGimbal ? activeGimbal.deviceId.rawValue : ""
            visible:             multiGimbalSetup
            anchors.top:         parent.top
            anchors.topMargin:   -margins * 0.5
            anchors.right:       parent.right
            anchors.rightMargin: -margins * 0.5
        }
    }

    // Telemetry and status indicator
    GridLayout {
        id:                        gimbalTelemetryLayout
        anchors.left:              gimbalIndicatorIcon.right
        anchors.leftMargin:        margins
        anchors.verticalCenter:    parent.verticalCenter
        columns:                   2
        rows:                      2
        rowSpacing:                0
        columnSpacing:             margins
        property bool showAzimuth: QGroundControl.settingsManager.gimbalControllerSettings.toolbarIndicatorShowAzimuth.rawValue

        QGCLabel {
            id:                     statusLabel
            text:                   activeGimbal && activeGimbal.retracted ?
                                        qsTr("Retracted") :
                                        (activeGimbal && activeGimbal.yawLock ? qsTr("Yaw locked") : qsTr("Yaw follow"))
            Layout.columnSpan:      2
            Layout.alignment:       Qt.AlignHCenter
        }
        QGCLabel {
            id:                     pitchLabel
            text:                   activeGimbal ? qsTr("P: ") + activeGimbal.absolutePitch.rawValue.toFixed(1) : ""
        }
        QGCLabel {
            id:                     panLabel
            text:                   activeGimbal ?
                                        gimbalTelemetryLayout.showAzimuth ?
                                            (isFinite(control._gimbalAbsoluteYaw)
                                                ? (qsTr("Az: ") + control._gimbalAbsoluteYaw.toFixed(1))
                                                : (qsTr("Az: ") + "--")) :
                                            (qsTr("Y: ") + activeGimbal.bodyYaw.rawValue.toFixed(1)) :
                                                ""
        }
    }

    MouseArea {
        anchors.fill:   parent
        onClicked:      mainWindow.showIndicatorDrawer(gimbalControlsPage, control)
    }

    Connections {
        target: _gimbalCenterCoordinator
        function onCenterRequestStarted() {
            if (control._pendingOwnershipAction !== null) {
                control._clearPendingOwnershipAction()
            }
            control._clearCenterFinalAckWait()
        }
    }

    Connections {
        target: activeGimbal

        // GimbalController updates these two properties sequentially. Defer
        // both notifications and then require the complete stable predicate.
        function onGimbalHaveControlChanged() {
            if (!control._hasConfirmedOwnership(activeGimbal)) {
                control._markCenterPrimerRequired(activeVehicle, activeGimbal)
            }
            control._schedulePendingOwnershipCheck()
        }
        function onGimbalOthersHaveControlChanged() {
            if (!control._hasConfirmedOwnership(activeGimbal)) {
                control._markCenterPrimerRequired(activeVehicle, activeGimbal)
            }
            control._schedulePendingOwnershipCheck()
        }
        function onDestroyed() {
            if (control._pendingOwnershipAction !== null) {
                control._clearPendingOwnershipAction()
            }
        }
    }

    Connections {
        target: activeVehicle
        function onMavCommandResult(vehicleId, targetComponent, command, ackResult, failureCode) {
            // A previous gimbal's final ACK and the active gimbal's primer ACK
            // can coexist because Vehicle keys commands by component. Let both
            // phase handlers apply their own identity filters.
            control._handleCenterPrimerResult(vehicleId, targetComponent, command, ackResult, failureCode)
            control._handleCenterFinalResult(vehicleId, targetComponent, command, ackResult, failureCode)
        }
        function onDestroyed() {
            if (control._pendingOwnershipAction !== null) {
                control._clearPendingOwnershipAction()
            }
            control._clearCenterFinalAckWait()
        }
    }

    Connections {
        id:                         acquirePopupConnection
        property bool isPopupOpen:  false
        target:                     gimbalController
        function onShowAcquireGimbalControlPopup() {
            var coordinatedDispatch = control._gimbalCenterCoordinator
                    && control._gimbalCenterCoordinator.dispatchInProgress
            if (control._pendingOwnershipAction !== null
                    || control._toolbarPostureDispatchInProgress
                    || coordinatedDispatch) {
                // The command lost ownership between the stable check and
                // synchronous dispatch, or a toolbar acquisition is pending.
                // Suppress this toolbar-only prompt.
                if (coordinatedDispatch) {
                    control._gimbalCenterCoordinator.cancel()
                }
                if (control._pendingOwnershipAction !== null) {
                    control._clearPendingOwnershipAction()
                }
                return
            }

            if (!acquirePopupConnection.isPopupOpen) {
                acquirePopupConnection.isPopupOpen = true
                mainWindow.showMessageDialog(
                    "Request Gimbal Control?",
                    "Command not sent. Another user has control of the gimbal.",
                    Dialog.Yes | Dialog.No,
                    gimbalController.acquireGimbalControl,
                    function() { acquirePopupConnection.isPopupOpen = false }
                )
            }
        }
        function onDestroyed() {
            if (control._pendingOwnershipAction !== null) {
                control._clearPendingOwnershipAction()
            }
        }
    }
}
