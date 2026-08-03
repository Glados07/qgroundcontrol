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

    readonly property real _configuredLowVoltageThreshold: _lowVoltageThresholdFact
                                                            ? Number(_lowVoltageThresholdFact.rawValue)
                                                            : NaN
    readonly property real lowVoltageThreshold:   isFinite(_configuredLowVoltageThreshold)
                                                  ? _configuredLowVoltageThreshold
                                                  : 20.0
    readonly property real clearVoltageThreshold: lowVoltageThreshold + 0.4

    readonly property Fact _lowVoltageThresholdFact: controller.getParameterFact(-1, "COM_GEN_V_LOW", false)

    readonly property var  _busVoltageFact: vehicle && vehicle.generator
                                             ? vehicle.generator.busVoltage
                                             : null
    readonly property real _busVoltage:     _busVoltageFact ? Number(_busVoltageFact.rawValue) : NaN
    readonly property bool _communicationLost: !vehicle ||
                                                !vehicle.vehicleLinkManager ||
                                                vehicle.vehicleLinkManager.communicationLost

    property bool _warningActive: false

    FactPanelController {
        id: controller
    }

    // Hysteresis prevents the warning from flickering near the low-voltage threshold.
    function _updateWarningState() {
        if (_communicationLost || !isFinite(_busVoltage)) {
            _warningActive = false
        } else if (_busVoltage < lowVoltageThreshold) {
            _warningActive = true
        } else if (_busVoltage > clearVoltageThreshold) {
            _warningActive = false
        }
    }

    on_BusVoltageChanged:             _updateWarningState()
    on_CommunicationLostChanged:      _updateWarningState()
    onLowVoltageThresholdChanged:     _updateWarningState()
    onClearVoltageThresholdChanged:   _updateWarningState()
    Component.onCompleted:            _updateWarningState()

    visible:        _warningActive
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
