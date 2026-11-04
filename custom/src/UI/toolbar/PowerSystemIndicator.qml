import QtQuick

import QGroundControl
import QGroundControl.Controls
import QGroundControl.ScreenTools
import QGroundControl.FactSystem
import QGroundControl.FactControls
import MAVLink
import Custom.FuelCell
import Custom.Widgets

Item {
    id: control
    anchors.top: parent.top
    anchors.bottom: parent.bottom
    width: indicatorRow.width

    readonly property var manager: QGroundControl.corePlugin.fuelCellController
    readonly property QtObject batteryModel: QtObject {
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
        readonly property var configurationFacts: [actionFact, lowVoltageFact, criticalVoltageFact, emergencyVoltageFact]
        readonly property bool configurationAvailable: configurationFacts.every(fact => Boolean(fact))
        readonly property bool _thresholdsValid: Boolean(lowVoltageFact && criticalVoltageFact && emergencyVoltageFact
                                                       && Number(lowVoltageFact.rawValue) > Number(criticalVoltageFact.rawValue)
                                                       && Number(criticalVoltageFact.rawValue) > Number(emergencyVoltageFact.rawValue)
                                                       && Number(emergencyVoltageFact.rawValue) > 0)
        readonly property string voltageText: formatFact(battery ? battery.voltage : null, "V")
        readonly property string powerText: formatPower(battery)
        readonly property color iconColor: stateColor(batteryState(battery))
        property var _parameterController: null
        property bool _ready: false
        property Component _controllerComponent: Component { FactPanelController {} }

        Component.onCompleted: {
            _ready = true
            bindVehicle()
        }
        onVehicleChanged: if (_ready) bindVehicle()

        function bindVehicle() {
            // FactPanelController captures activeVehicle in its constructor; do not reuse it across vehicles.
            const previous = _parameterController
            _parameterController = vehicleAvailable ? _controllerComponent.createObject(control) : null
            if (previous) previous.destroy()
        }

        function parameterFact(name) {
            return parametersReady && _parameterController && _parameterController.vehicle === vehicle
                    ? _parameterController.getParameterFact(-1, name, false) : null
        }

        function hasValue(fact) {
            return fact && fact.rawValue !== null && fact.rawValue !== undefined
                    && Number.isFinite(Number(fact.rawValue))
        }

        function formatFact(fact, units) {
            return measurementsAvailable && hasValue(fact) ? fact.valueString + " " + units : "— " + units
        }

        function formatPower(value) {
            if (measurementsAvailable && value && hasValue(value.voltage) && hasValue(value.current)) {
                const power = Number(value.voltage.rawValue) * Number(value.current.rawValue)
                if (Number.isFinite(power)) return power.toFixed(0) + " W"
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
    }
    // 0: none, 1: warning, 2: critical. Critical states take priority over current limiting.
    readonly property int fcsAlertSeverity: {
        if (!manager || !manager.telemetryFresh) return 0
        if (manager.connectionState === FuelCellControllerManager.Disconnected) return 2
        if (!manager.deviceDataValid) return 0
        if (manager.fcsState >= 9 && manager.fcsState <= 12) return 2
        if (manager.fcsState === 13) return 1
        return 0
    }
    readonly property bool showIndicator: batteryModel.vehicleAvailable

    Row {
        id: indicatorRow
        anchors.verticalCenter: parent.verticalCenter
        spacing: ScreenTools.defaultFontPixelWidth / 2

        QGCColoredImage {
            id: powerIcon
            width: control.height * 0.8
            height: width
            anchors.verticalCenter: parent.verticalCenter
            source: "qrc:/custom/img/FuelCellController.svg"
            sourceSize.width: width
            color: control.batteryModel.iconColor

            Rectangle {
                anchors.top: parent.top
                anchors.right: parent.right
                width: ScreenTools.defaultFontPixelHeight / 2
                height: width
                radius: width / 2
                visible: control.fcsAlertSeverity > 0
                color: control.fcsAlertSeverity === 2 ? QGroundControl.globalPalette.colorRed
                                                      : QGroundControl.globalPalette.colorOrange
                border.color: QGroundControl.globalPalette.window
            }
        }
        Column {
            anchors.verticalCenter: parent.verticalCenter
            QGCLabel { text: control.batteryModel.voltageText }
            QGCLabel { text: control.batteryModel.powerText }
        }
    }

    MouseArea {
        anchors.fill: parent
        enabled: control.showIndicator
        onClicked: mainWindow.showIndicatorDrawer(indicatorPage, control)
    }

    Component {
        id: indicatorPage
        PowerSystemIndicatorPage { indicator: control }
    }
}
