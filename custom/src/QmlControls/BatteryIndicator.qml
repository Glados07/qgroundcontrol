/****************************************************************************
 *
 * (c) 2009-2020 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.MultiVehicleManager
import QGroundControl.ScreenTools
import QGroundControl.Palette
import QGroundControl.FactSystem
import QGroundControl.FactControls
import MAVLink

//-------------------------------------------------------------------------
//-- Battery Indicator
Item {
    id:             control
    anchors.top:    parent.top
    anchors.bottom: parent.bottom
    width:          batteryIndicatorRow.width

    property bool showIndicator: true

    property var _activeVehicle: QGroundControl.multiVehicleManager.activeVehicle

    readonly property bool _parametersReady: _activeVehicle && _activeVehicle.parameterManager.parametersReady
    readonly property var _lowVoltageFact:       _parameterFact("UAVCAN_POW_LOW")
    readonly property var _criticalVoltageFact:  _parameterFact("UAVCAN_POW_CRITI")
    readonly property var _emergencyVoltageFact: _parameterFact("UAVCAN_POW_EMERG")
    readonly property var _lowBatteryActionFact: _parameterFact("COM_LOW_BAT_ACT")
    readonly property real _lowVoltage:       _factNumber(_lowVoltageFact)
    readonly property real _criticalVoltage:  _factNumber(_criticalVoltageFact)
    readonly property real _emergencyVoltage: _factNumber(_emergencyVoltageFact)
    readonly property bool _thresholdFactsAvailable: _lowVoltageFact !== null &&
                                                     _criticalVoltageFact !== null &&
                                                     _emergencyVoltageFact !== null
    readonly property bool _failsafeParametersAvailable: _thresholdFactsAvailable && _lowBatteryActionFact !== null
    readonly property bool _voltageThresholdsValid: _thresholdFactsAvailable &&
                                                     _lowVoltage > _criticalVoltage &&
                                                     _criticalVoltage > _emergencyVoltage &&
                                                     _emergencyVoltage > 0

    FactPanelController { id: parameterController }

    function _parameterFact(name) {
        return _parametersReady ? parameterController.getParameterFact(-1, name, false) : null
    }

    function _factNumber(fact) {
        return fact ? Number(fact.rawValue) : NaN
    }

    function _formatFactValue(fact, units) {
        return fact && !isNaN(fact.rawValue)
                ? fact.valueString + " " + units
                : qsTr("n/a")
    }

    function _formatPower(battery) {
        if (!battery) {
            return qsTr("n/a")
        }

        const voltage = Number(battery.voltage.rawValue)
        const current = Number(battery.current.rawValue)
        if (isNaN(voltage) || isNaN(current)) {
            return qsTr("n/a")
        }

        // BATTERY_STATUS voltage is the sum of voltages/voltages_ext; current is current_battery.
        return (voltage * current).toFixed(0) + " W"
    }

    function _batteryState(battery) {
        const reportedState = Number(battery.chargeState.rawValue)
        const voltage = Number(battery.voltage.rawValue)

        if (reportedState === MAVLink.MAV_BATTERY_CHARGE_STATE_FAILED ||
                reportedState === MAVLink.MAV_BATTERY_CHARGE_STATE_UNHEALTHY ||
                reportedState === MAVLink.MAV_BATTERY_CHARGE_STATE_CHARGING ||
                !_voltageThresholdsValid || isNaN(voltage)) {
            return reportedState
        }

        if (voltage < _emergencyVoltage) {
            return MAVLink.MAV_BATTERY_CHARGE_STATE_EMERGENCY
        } else if (voltage < _criticalVoltage) {
            return MAVLink.MAV_BATTERY_CHARGE_STATE_CRITICAL
        } else if (voltage < _lowVoltage) {
            return MAVLink.MAV_BATTERY_CHARGE_STATE_LOW
        }
        return MAVLink.MAV_BATTERY_CHARGE_STATE_OK
    }

    function _stateText(battery) {
        const batteryState = _batteryState(battery)
        for (let i = 0; i < battery.chargeState.enumValues.length; ++i) {
            if (Number(battery.chargeState.enumValues[i]) === batteryState) {
                return battery.chargeState.enumStrings[i]
            }
        }
        return qsTr("n/a")
    }

    function _stateVisual(batteryState) {
        switch (batteryState) {
        case MAVLink.MAV_BATTERY_CHARGE_STATE_OK:
            return { source: "/qmlimages/BatteryGreen.svg", color: qgcPal.colorGreen }
        case MAVLink.MAV_BATTERY_CHARGE_STATE_LOW:
            return { source: "/qmlimages/BatteryOrange.svg", color: qgcPal.colorOrange }
        case MAVLink.MAV_BATTERY_CHARGE_STATE_CRITICAL:
            return { source: "/qmlimages/BatteryCritical.svg", color: qgcPal.colorRed }
        case MAVLink.MAV_BATTERY_CHARGE_STATE_EMERGENCY:
        case MAVLink.MAV_BATTERY_CHARGE_STATE_FAILED:
        case MAVLink.MAV_BATTERY_CHARGE_STATE_UNHEALTHY:
            return { source: "/qmlimages/BatteryEMERGENCY.svg", color: qgcPal.colorRed }
        default:
            return { source: "/qmlimages/Battery.svg", color: qgcPal.text }
        }
    }

    Row {
        id:             batteryIndicatorRow
        anchors.top:    parent.top
        anchors.bottom: parent.bottom

        Repeater {
            model: _activeVehicle ? _activeVehicle.batteries : 0

            Loader {
                anchors.top:        parent.top
                anchors.bottom:     parent.bottom
                sourceComponent:    batteryVisual

                property var battery: object
            }
        }
    }
    MouseArea {
        anchors.fill:   parent
        onClicked: {
            mainWindow.showIndicatorDrawer(batteryPopup, control)
        }
    }

    Component {
        id: batteryPopup

        ToolIndicatorPage {
            showExpand:         true
            waitForParameters:  true
            contentComponent:   batteryContentComponent
            expandedComponent:  batteryExpandedComponent
        }
    }

    Component {
        id: batteryVisual

        Row {
            anchors.top:    parent.top
            anchors.bottom: parent.bottom

            readonly property int batteryState: control._batteryState(battery)
            readonly property var stateVisual: control._stateVisual(batteryState)

            QGCColoredImage {
                anchors.top:        parent.top
                anchors.bottom:     parent.bottom
                width:              height
                sourceSize.width:   width
                source:             stateVisual.source
                fillMode:           Image.PreserveAspectFit
                color:              stateVisual.color
            }

           ColumnLayout {
                id:                     batteryInfoColumn
                anchors.top:            parent.top
                anchors.bottom:         parent.bottom
                spacing:                0

                QGCLabel {
                    Layout.alignment:       Qt.AlignHCenter
                    verticalAlignment:      Text.AlignVCenter
                    color:                  qgcPal.text
                    text:                   control._formatFactValue(battery.voltage, "V")
                    font.pointSize:         ScreenTools.defaultFontPointSize
                }

                QGCLabel {
                    Layout.alignment:       Qt.AlignHCenter
                    font.pointSize:         ScreenTools.defaultFontPointSize
                    color:                  qgcPal.text
                    text:                   control._formatPower(battery)
                }
            }
        }
    }

    Component {
        id: batteryContentComponent

        ColumnLayout {
            spacing: ScreenTools.defaultFontPixelHeight / 2

            Repeater {
                model: _activeVehicle ? _activeVehicle.batteries : 0

                SettingsGroupLayout {
                    heading:        qsTr("Power %1").arg(_activeVehicle.batteries.length === 1 ? qsTr("Status") : object.id.rawValue)
                    contentSpacing: 0
                    showDividers:   false

                    LabelledLabel {
                        label:      qsTr("Power State")
                        labelText:  control._stateText(object)
                    }

                    LabelledLabel {
                        label:      qsTr("Measured Voltage")
                        labelText:  control._formatFactValue(object.voltage, "V")
                    }

                    LabelledLabel {
                        label:      qsTr("Power")
                        labelText:  control._formatPower(object)
                    }

                    LabelledLabel {
                        label:      qsTr("Current")
                        labelText:  control._formatFactValue(object.current, "A")
                    }

                    LabelledLabel {
                        label:      qsTr("Consumed")
                        labelText:  control._formatFactValue(object.mahConsumed, "mAh")
                    }
                }
            }
        }
    }

    Component {
        id: batteryExpandedComponent

        ColumnLayout {
            spacing: ScreenTools.defaultFontPixelHeight / 2

            SettingsGroupLayout {
                heading:            qsTr("Low Power Failsafe")
                Layout.fillWidth:   true
                visible:            _failsafeParametersAvailable

                LabelledFactComboBox {
                    label:      qsTr("Vehicle Action")
                    fact:       _lowBatteryActionFact
                    indexModel: false
                }

                LabelledFactTextField {
                    label:  qsTr("Low Voltage")
                    fact:   _lowVoltageFact
                }

                LabelledFactTextField {
                    label:  qsTr("Critical Voltage")
                    fact:   _criticalVoltageFact
                }

                LabelledFactTextField {
                    label:  qsTr("Emergency Voltage")
                    fact:   _emergencyVoltageFact
                }
            }

        }
    }
}
