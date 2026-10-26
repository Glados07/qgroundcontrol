import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.ScreenTools
import QGroundControl.FactSystem
import QGroundControl.FactControls
import MAVLink

// One persistent data owner for the toolbar and the lazily created battery pane.
Item {
    id: root
    visible: false

    readonly property var vehicle: QGroundControl.multiVehicleManager.activeVehicle
    readonly property bool vehicleAvailable: Boolean(vehicle && !vehicle.isOfflineEditingVehicle)
    readonly property bool measurementsAvailable: Boolean(vehicleAvailable && vehicle.vehicleLinkManager
                                                         && !vehicle.vehicleLinkManager.communicationLost)
    readonly property var battery: vehicleAvailable && vehicle.batteries.count > 0 ? vehicle.batteries.get(0) : null
    readonly property bool parametersReady: Boolean(vehicleAvailable && vehicle.parameterManager.parametersReady)
    readonly property var lowVoltageFact: parameterFact("UAVCAN_POW_LOW")
    readonly property var criticalVoltageFact: parameterFact("UAVCAN_POW_CRITI")
    readonly property var emergencyVoltageFact: parameterFact("UAVCAN_POW_EMERG")
    readonly property var actionFact: parameterFact("COM_LOW_BAT_ACT")
    readonly property bool configurationAvailable: Boolean(lowVoltageFact && criticalVoltageFact && emergencyVoltageFact && actionFact)
    readonly property bool _thresholdsValid: Boolean(lowVoltageFact && criticalVoltageFact && emergencyVoltageFact
                                                   && Number(lowVoltageFact.rawValue) > Number(criticalVoltageFact.rawValue)
                                                   && Number(criticalVoltageFact.rawValue) > Number(emergencyVoltageFact.rawValue)
                                                   && Number(emergencyVoltageFact.rawValue) > 0)
    readonly property string voltageText: formatFact(battery ? battery.voltage : null, "V")
    readonly property string powerText: formatPower(battery)
    readonly property color iconColor: stateColor(batteryState(battery))
    property var _parameterController: null
    property bool _ready: false

    Component { id: controllerComponent; FactPanelController {} }
    Component.onCompleted: { _ready = true; bindVehicle() }
    onVehicleChanged: if (_ready) bindVehicle()

    function bindVehicle() {
        // FactPanelController captures activeVehicle in its constructor; do not reuse it across vehicles.
        const previous = _parameterController
        _parameterController = vehicleAvailable ? controllerComponent.createObject(root) : null
        if (previous) previous.destroy()
    }

    function parameterFact(name) {
        return parametersReady && _parameterController && _parameterController.vehicle === vehicle
                ? _parameterController.getParameterFact(-1, name, false) : null
    }

    function hasValue(fact) {
        return fact && fact.rawValue !== null && fact.rawValue !== undefined && isFinite(Number(fact.rawValue))
    }

    function formatFact(fact, units) {
        return measurementsAvailable && hasValue(fact) ? fact.valueString + " " + units : "— " + units
    }

    function formatPower(value) {
        if (measurementsAvailable && value && hasValue(value.voltage) && hasValue(value.current)) {
            const power = Number(value.voltage.rawValue) * Number(value.current.rawValue)
            if (isFinite(power)) return power.toFixed(0) + " W"
        }
        return "— W"
    }

    function batteryState(value) {
        if (!measurementsAvailable || !value) return MAVLink.MAV_BATTERY_CHARGE_STATE_UNDEFINED
        const reported = Number(value.chargeState.rawValue)
        const voltage = Number(value.voltage.rawValue)
        if (reported === MAVLink.MAV_BATTERY_CHARGE_STATE_FAILED
                || reported === MAVLink.MAV_BATTERY_CHARGE_STATE_UNHEALTHY
                || reported === MAVLink.MAV_BATTERY_CHARGE_STATE_CHARGING
                || !_thresholdsValid || !hasValue(value.voltage)) return reported
        if (voltage < Number(emergencyVoltageFact.rawValue)) return MAVLink.MAV_BATTERY_CHARGE_STATE_EMERGENCY
        if (voltage < Number(criticalVoltageFact.rawValue)) return MAVLink.MAV_BATTERY_CHARGE_STATE_CRITICAL
        if (voltage < Number(lowVoltageFact.rawValue)) return MAVLink.MAV_BATTERY_CHARGE_STATE_LOW
        return MAVLink.MAV_BATTERY_CHARGE_STATE_OK
    }

    function stateText(value) {
        if (!measurementsAvailable || !value) return qsTr("Unavailable")
        const state = batteryState(value)
        for (let i = 0; i < value.chargeState.enumValues.length; ++i) {
            if (Number(value.chargeState.enumValues[i]) === state) return value.chargeState.enumStrings[i]
        }
        return qsTr("Unavailable")
    }

    function stateColor(state) {
        const palette = QGroundControl.globalPalette
        switch (state) {
        case MAVLink.MAV_BATTERY_CHARGE_STATE_OK: return palette.colorGreen
        case MAVLink.MAV_BATTERY_CHARGE_STATE_LOW: return palette.colorYellow
        case MAVLink.MAV_BATTERY_CHARGE_STATE_CRITICAL: return palette.colorOrange
        case MAVLink.MAV_BATTERY_CHARGE_STATE_EMERGENCY:
        case MAVLink.MAV_BATTERY_CHARGE_STATE_FAILED:
        case MAVLink.MAV_BATTERY_CHARGE_STATE_UNHEALTHY: return palette.colorRed
        default: return palette.text
        }
    }

    readonly property Component contentComponent: Component {
        ColumnLayout {
            id: pane
            spacing: ScreenTools.defaultFontPixelHeight / 2
            property var openedVehicle: null
            Component.onCompleted: openedVehicle = root.vehicle

            SettingsGroupLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                heading: qsTr("Power Status")
                contentSpacing: 0
                showDividers: false

                LabelledLabel {
                    label: qsTr("Power State")
                    labelText: root.stateText(root.battery)
                }
                LabelledLabel {
                    label: qsTr("Measured Voltage")
                    labelText: root.voltageText
                }
                LabelledLabel {
                    label: qsTr("Power")
                    labelText: root.powerText
                }
                LabelledLabel {
                    label: qsTr("Current")
                    labelText: root.formatFact(root.battery ? root.battery.current : null, "A")
                }
                LabelledLabel {
                    label: qsTr("Consumed")
                    labelText: root.formatFact(root.battery ? root.battery.mahConsumed : null, "mAh")
                }
            }

            QGCLabel {
                Layout.fillWidth: true
                visible: !root.configurationAvailable
                text: root.parametersReady ? qsTr("Power configuration is unavailable.") : qsTr("Waiting for parameters...")
                wrapMode: Text.WordWrap
            }

            Loader {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                active: root.configurationAvailable && pane.openedVehicle === root.vehicle
                enabled: root.measurementsAvailable
                sourceComponent: settingsComponent
                onLoaded: {
                    // Pin editors to these Facts. A vehicle switch must never retarget an unfinished edit.
                    item.facts = [root.actionFact, root.lowVoltageFact, root.criticalVoltageFact, root.emergencyVoltageFact]
                }
            }
        }
    }

    Component {
        id: settingsComponent
        SettingsGroupLayout {
            id: settings
            property var facts: []
            heading: qsTr("Low Power Failsafe")
            contentSpacing: ScreenTools.defaultFontPixelHeight / 2
            showDividers: false

            LabelledFactComboBox {
                Layout.fillWidth: true
                label: qsTr("Vehicle Action")
                fact: settings.facts.length ? settings.facts[0] : null
                indexModel: false
                comboBoxPreferredWidth: ScreenTools.defaultFontPixelWidth * 14
                Component.onCompleted: comboBox.contentItem.elide = Text.ElideRight
            }
            // Release the editor's global validation state when this pane is destroyed.
            LabelledFactTextField {
                Component.onDestruction: textField.clearValidationError()
                Layout.fillWidth: true
                label: qsTr("Low Voltage")
                fact: settings.facts.length ? settings.facts[1] : null
                textFieldPreferredWidth: ScreenTools.defaultFontPixelWidth * 10
            }
            LabelledFactTextField {
                Component.onDestruction: textField.clearValidationError()
                Layout.fillWidth: true
                label: qsTr("Critical Voltage")
                fact: settings.facts.length ? settings.facts[2] : null
                textFieldPreferredWidth: ScreenTools.defaultFontPixelWidth * 10
            }
            LabelledFactTextField {
                Component.onDestruction: textField.clearValidationError()
                Layout.fillWidth: true
                label: qsTr("Emergency Voltage")
                fact: settings.facts.length ? settings.facts[3] : null
                textFieldPreferredWidth: ScreenTools.defaultFontPixelWidth * 10
            }
        }
    }
}
