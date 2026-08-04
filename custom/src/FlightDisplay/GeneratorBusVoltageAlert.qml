/****************************************************************************
 *
 * Generator bus-voltage warning shown in Fly View.
 *
 ****************************************************************************/

import QtQuick

import QGroundControl.Controllers
import QGroundControl.Controls
import QGroundControl.FactSystem
import QGroundControl.Palette
import QGroundControl.ScreenTools

Rectangle {
    id: root

    required property var vehicle

    readonly property real lowVoltageThreshold:   _lowVoltageThresholdFact
                                                  ? Number(_lowVoltageThresholdFact.rawValue)
                                                  : NaN
    readonly property real lowVoltageConfirmationTime: _lowVoltageConfirmationFact
                                                       ? Number(_lowVoltageConfirmationFact.rawValue)
                                                       : NaN

    readonly property Fact _lowVoltageThresholdFact: controller.getParameterFact(-1, "COM_GEN_V_LOW")
    readonly property Fact _lowVoltageConfirmationFact: controller.getParameterFact(-1, "COM_GEN_LOW_T")

    readonly property var  _busVoltageFact: vehicle && vehicle.generator
                                             ? vehicle.generator.busVoltage
                                             : null
    readonly property real _busVoltage:     _busVoltageFact ? Number(_busVoltageFact.rawValue) : NaN
    readonly property bool _communicationLost: !vehicle ||
                                                !vehicle.vehicleLinkManager ||
                                                vehicle.vehicleLinkManager.communicationLost
    readonly property bool _telemetryValid:     !_communicationLost &&
                                                isFinite(_busVoltage) &&
                                                isFinite(lowVoltageThreshold) &&
                                                isFinite(lowVoltageConfirmationTime) &&
                                                lowVoltageConfirmationTime >= 0

    readonly property int _transitionNone:       0
    readonly property int _transitionToWarning:  1
    readonly property int _transitionToNormal:   2

    property bool _warningActive: false
    property int  _pendingTransition: _transitionNone

    FactPanelController {
        id: controller
    }

    Timer {
        id: transitionTimer
        interval: isFinite(root.lowVoltageConfirmationTime)
                  ? Math.max(1, root.lowVoltageConfirmationTime * 1000)
                  : 1
        repeat: false
        onTriggered: root._completePendingTransition()
    }

    function _resetPendingTransition() {
        transitionTimer.stop()
        _pendingTransition = _transitionNone
    }

    function _transitionConditionMet(transition) {
        return transition === _transitionToWarning
                ? _busVoltage < lowVoltageThreshold
                : _busVoltage > lowVoltageThreshold
    }

    function _completePendingTransition() {
        const completedTransition = _pendingTransition
        _pendingTransition = _transitionNone

        if (completedTransition === _transitionNone ||
                !_telemetryValid ||
                !_transitionConditionMet(completedTransition)) {
            return
        }

        _warningActive = completedTransition === _transitionToWarning
        _updateWarningState()
    }

    // Both activation and clearing require the voltage to remain continuously
    // on the corresponding side of the threshold for COM_GEN_LOW_T seconds.
    function _updateWarningState() {
        if (!_telemetryValid) {
            _resetPendingTransition()
            return
        }

        const requestedTransition = _warningActive
                ? (_busVoltage > lowVoltageThreshold ? _transitionToNormal : _transitionNone)
                : (_busVoltage < lowVoltageThreshold ? _transitionToWarning : _transitionNone)

        if (requestedTransition === _transitionNone) {
            _resetPendingTransition()
        } else if (lowVoltageConfirmationTime === 0) {
            _resetPendingTransition()
            _warningActive = requestedTransition === _transitionToWarning
        } else if (_pendingTransition !== requestedTransition) {
            _pendingTransition = requestedTransition
            transitionTimer.restart()
        }
    }

    on_BusVoltageChanged:             _updateWarningState()
    on_CommunicationLostChanged:      _updateWarningState()
    onLowVoltageThresholdChanged: {
        _resetPendingTransition()
        _updateWarningState()
    }
    onLowVoltageConfirmationTimeChanged: {
        _resetPendingTransition()
        _updateWarningState()
    }
    onVehicleChanged: {
        _warningActive = false
        _resetPendingTransition()
        _updateWarningState()
    }
    Component.onCompleted:            _updateWarningState()

    visible:        _warningActive && _telemetryValid
    implicitWidth:  warningLabel.implicitWidth + ScreenTools.defaultFontPixelWidth * 3
    implicitHeight: warningLabel.implicitHeight + ScreenTools.defaultFontPixelHeight
    radius:         ScreenTools.defaultFontPixelHeight * 0.5
    color:          qgcPal.alertBackground
    border.color:   qgcPal.alertBorder
    border.width:   2

    QGCPalette {
        id: qgcPal
        colorGroupEnabled: root.enabled
    }

    QGCLabel {
        id: warningLabel
        anchors.centerIn:   parent
        text:               qsTr("Generator Bus Voltage Low: %1 V").arg(root._busVoltage.toFixed(1))
        color:              qgcPal.alertText
        font.bold:          true
        font.pointSize:     ScreenTools.mediumFontPointSize
    }
}
