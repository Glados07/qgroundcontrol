import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.ScreenTools
import QGroundControl.FactControls
import Custom.FuelCell

ToolIndicatorPage {
    id: root
    required property var indicator
    readonly property var batteryModel: indicator.batteryModel
    readonly property var manager: indicator.manager
    readonly property var vehicle: batteryModel.vehicle
    readonly property real columnWidth: ScreenTools.defaultFontPixelWidth * 44
    readonly property real gap: ScreenTools.defaultFontPixelWidth
    readonly property int _commandOff: 0
    readonly property int _commandOn: 1
    readonly property int _commandEmergencyStop: 2
    property var _confirmationDialog: null

    // 顺序对应协议 FUEL_CELL_DEVICE_STATE 的 0～13，不能用控制指令代替实际状态。
    readonly property var _fcsStates: [
        qsTr("Powering on"), qsTr("Power on complete"), qsTr("Entering standby"), qsTr("Standby"),
        qsTr("Starting"), qsTr("Start complete"), qsTr("Running"), qsTr("Stopping"),
        qsTr("Stop complete"), qsTr("Fault shutdown in progress"), qsTr("Fault shutdown complete"),
        qsTr("Emergency stop in progress"), qsTr("Emergency stop complete"),
        qsTr("Output current limited")
    ]
    readonly property var _commands: [qsTr("FCS Off"), qsTr("FCS On"), qsTr("FCS Emergency Stop")]
    readonly property string connectionText: {
        if (!manager.telemetryAvailable) return qsTr("Waiting for status")
        switch (manager.connectionState) {
        case FuelCellControllerManager.NeverConnected: return qsTr("Never connected")
        case FuelCellControllerManager.Connected: return qsTr("Connected")
        case FuelCellControllerManager.Disconnected: return qsTr("Disconnected")
        default: return qsTr("Unknown (telemetry expired)")
        }
    }
    readonly property string requestText: {
        const operation = manager.requestedFaultReset ? qsTr("FCS Fault Reset")
                                                       : commandText(manager.requestedCommand)
        switch (manager.requestState) {
        case FuelCellControllerManager.Pending:
            return qsTr("%1: Waiting for flight controller confirmation...").arg(operation)
                    + (manager.lastError ? "\n" + manager.lastError : "")
        case FuelCellControllerManager.Accepted:
            if (manager.requestedFaultReset)
                return qsTr("Flight controller started the fault reset pulse. Check FCS system status to see whether the fault clears.")
            return manager.lastError || qsTr("Flight controller accepted %1. Check FCS system status for progress.").arg(operation)
        case FuelCellControllerManager.Failed:
            return qsTr("Operation failed: %1").arg(manager.lastError)
        case FuelCellControllerManager.Unconfirmed:
            return qsTr("Execution result unconfirmed: %1").arg(manager.lastError)
        default:
            return manager.lastError
        }
    }
    readonly property string controlHint: {
        if (!manager.telemetryFresh) return qsTr("Waiting for valid telemetry. Controls are unavailable.")
        if (!manager.deviceDataValid) return qsTr("Device is not connected or has disconnected. Controls are unavailable.")
        if (!manager.commandPending && !manager.canEmergencyStop && !manager.canControl)
            return qsTr("Control link is unavailable.")
        if (manager.resetCooldownActive) return qsTr("Fault reset pulse started. Start, stop and reset are temporarily unavailable.")
        return ""
    }

    function formatValue(value, decimals, unit) {
        const numericValue = Number(value)
        return Number.isFinite(numericValue) ? numericValue.toFixed(decimals) + " " + unit : "—"
    }

    function fcsStateText(state) {
        if (state >= 0 && state < _fcsStates.length) return _fcsStates[state]
        if (state === 14 || state === 15) return qsTr("Unknown state (%1)").arg(state)
        return "—"
    }

    function commandText(command) {
        return command >= _commandOff && command <= _commandEmergencyStop ? _commands[command] : "—"
    }

    function canSendCommand(command) {
        if (command === _commandEmergencyStop) return manager.canEmergencyStop
        return (command === _commandOff || command === _commandOn) && manager.canControl
    }

    function sendCommand(command, revision) {
        if (command === _commandOn) return manager.requestStartFcs(revision)
        if (command === _commandOff) return manager.requestStopFcs(revision)
        if (command === _commandEmergencyStop) return manager.requestEmergencyStopFcs(revision)
        return false
    }

    function closeConfirmation() {
        if (_confirmationDialog) _confirmationDialog.close()
    }

    function startFcsNow() {
        if (_confirmationDialog || !vehicle || !canSendCommand(_commandOn)) return
        sendCommand(_commandOn, manager.sessionRevision)
    }

    function confirmCommand(command) {
        if (command !== _commandOff && command !== _commandEmergencyStop) return
        if (_confirmationDialog || !vehicle || !canSendCommand(command)) return

        // 在打开确认框时捕获会话，确认时交回后端校验，禁止向切换后的飞控发送旧操作。
        const revision = manager.sessionRevision
        _confirmationDialog = confirmationComponent.createObject(mainWindow, {
            title: qsTr("Confirm %1").arg(commandText(command)),
            text: qsTr("Send %2 to the Fuel Cell Controller on vehicle #%1?")
                      .arg(vehicle.id).arg(commandText(command)),
            acceptFunction: function() { root.sendCommand(command, revision) }
        })
        _confirmationDialog.open()
    }

    function confirmFaultReset() {
        if (_confirmationDialog || !vehicle || !manager.canResetFault) return

        const revision = manager.sessionRevision
        _confirmationDialog = confirmationComponent.createObject(mainWindow, {
            title: qsTr("Confirm FCS Fault Reset"),
            text: qsTr("Reset the Fuel Cell Controller fault on vehicle #%1? This switches the current FCS command to Off.")
                      .arg(vehicle.id),
            acceptFunction: function() { root.manager.requestFaultReset(revision) }
        })
        _confirmationDialog.open()
    }

    Connections {
        target: root.batteryModel
        // Close before editors created for the previous Vehicle object can be reused after reconnect or a switch.
        function onVehicleChanged() { if (root.drawer) root.drawer.close() }
    }
    Connections {
        target: root.manager
        function onSessionChanged() { root.closeConfirmation() }
    }
    Component.onDestruction: closeConfirmation()

    Component {
        id: confirmationComponent
        QGCSimpleMessageDialog {
            buttons: Dialog.Yes | Dialog.Cancel
            closeFunction: function() { root._confirmationDialog = null }
        }
    }

    component StatusField: Item {
        id: field
        property string label
        property string labelText
        property color labelTextColor: QGroundControl.globalPalette.text
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        implicitHeight: Math.max(nameLabel.implicitHeight, valueLabel.implicitHeight)

        QGCLabel {
            id: nameLabel
            width: Math.max(0, (field.width - ScreenTools.defaultFontPixelWidth * 2) * 0.45)
            text: field.label
            wrapMode: Text.Wrap
        }
        QGCLabel {
            id: valueLabel
            anchors.right: parent.right
            width: Math.max(0, (field.width - ScreenTools.defaultFontPixelWidth * 2) * 0.55)
            text: field.labelText
            color: field.labelTextColor
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignRight
        }
    }

    component BatteryPane: ColumnLayout {
        id: batteryPane
        required property var model
        required property var vehicle
        property var openedVehicle: null
        property Component _settingsComponent: Component {
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

        spacing: ScreenTools.defaultFontPixelHeight / 2
        Component.onCompleted: openedVehicle = vehicle

        function stateText(value) {
            if (!model.measurementsAvailable || !value) return qsTr("Unavailable")
            const state = model.batteryState(value)
            for (let i = 0; i < value.chargeState.enumValues.length; ++i) {
                if (Number(value.chargeState.enumValues[i]) === state) return value.chargeState.enumStrings[i]
            }
            return qsTr("Unavailable")
        }

        SettingsGroupLayout {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            heading: qsTr("Power Status")
            contentSpacing: 0
            showDividers: false

            LabelledLabel {
                label: qsTr("Power State")
                labelText: batteryPane.stateText(batteryPane.model.battery)
            }
            LabelledLabel {
                label: qsTr("Measured Voltage")
                labelText: batteryPane.model.voltageText
            }
            LabelledLabel {
                label: qsTr("Power")
                labelText: batteryPane.model.powerText
            }
            LabelledLabel {
                label: qsTr("Current")
                labelText: batteryPane.model.formatFact(batteryPane.model.battery ? batteryPane.model.battery.current : null, "A")
            }
            LabelledLabel {
                label: qsTr("Consumed")
                labelText: batteryPane.model.formatFact(batteryPane.model.battery ? batteryPane.model.battery.mahConsumed : null, "mAh")
            }
        }

        QGCLabel {
            Layout.fillWidth: true
            visible: !batteryPane.model.configurationAvailable
            text: batteryPane.model.parametersReady ? qsTr("Power configuration is unavailable.")
                                                   : qsTr("Waiting for parameters...")
            wrapMode: Text.WordWrap
        }

        Loader {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            active: batteryPane.model.configurationAvailable && batteryPane.openedVehicle === batteryPane.vehicle
            enabled: batteryPane.model.measurementsAvailable
            sourceComponent: batteryPane._settingsComponent
            onLoaded: {
                // Pin editors to these Facts. A vehicle switch must never retarget an unfinished edit.
                item.facts = batteryPane.model.configurationFacts
            }
        }
    }

    component FuelCellPane: ColumnLayout {
        id: fuelCellPane
        required property var controller
        readonly property var manager: controller.manager
        spacing: ScreenTools.defaultFontPixelHeight / 2

        SettingsGroupLayout {
            Layout.fillWidth: true
            heading: qsTr("Fuel Cell Controller")
            showDividers: false
            contentSpacing: ScreenTools.defaultFontPixelHeight / 4

            StatusField {
                label: qsTr("Device connection")
                labelText: fuelCellPane.controller.connectionText
            }
            StatusField {
                label: qsTr("FCS system status")
                labelText: fuelCellPane.controller.fcsStateText(fuelCellPane.manager.fcsState)
                labelTextColor: fuelCellPane.manager.fcsState === 13
                                ? QGroundControl.globalPalette.colorOrange
                                : QGroundControl.globalPalette.text
            }
            StatusField {
                label: qsTr("Lithium battery SOC")
                labelText: fuelCellPane.controller.formatValue(fuelCellPane.manager.socPercent, 2, "%")
            }
            StatusField {
                label: qsTr("DCDC output current")
                labelText: fuelCellPane.controller.formatValue(fuelCellPane.manager.dcdcOutputCurrent, 2, "A")
            }
        }

        SettingsGroupLayout {
            Layout.fillWidth: true
            heading: qsTr("Flight controller setpoints")
            showDividers: false
            contentSpacing: ScreenTools.defaultFontPixelHeight / 4

            StatusField {
                label: qsTr("Requested power")
                labelText: fuelCellPane.controller.formatValue(fuelCellPane.manager.requestedPower, 0, "W")
            }
        }

        SettingsGroupLayout {
            Layout.fillWidth: true
            heading: qsTr("FCS control")
            showDividers: false
            contentSpacing: ScreenTools.defaultFontPixelHeight / 2

            StatusField {
                label: qsTr("Current command")
                labelText: fuelCellPane.controller.commandText(fuelCellPane.manager.acceptedCommand)
            }
            RowLayout {
                Layout.fillWidth: true
                QGCButton {
                    Layout.fillWidth: true
                    text: qsTr("FCS On")
                    enabled: fuelCellPane.manager.canControl
                    onClicked: fuelCellPane.controller.startFcsNow()
                }
                QGCButton {
                    Layout.fillWidth: true
                    text: qsTr("FCS Off")
                    enabled: fuelCellPane.manager.canControl
                    onClicked: fuelCellPane.controller.confirmCommand(fuelCellPane.controller._commandOff)
                }
            }
            QGCButton {
                Layout.fillWidth: true
                text: qsTr("FCS Fault Reset")
                enabled: fuelCellPane.manager.canResetFault
                onClicked: fuelCellPane.controller.confirmFaultReset()
            }
            // 急停有独立门限，普通请求等待确认时仍可抢占。
            QGCButton {
                Layout.fillWidth: true
                text: qsTr("FCS Emergency Stop")
                enabled: fuelCellPane.manager.canEmergencyStop
                backgroundColor: enabled ? QGroundControl.globalPalette.colorRed
                                         : QGroundControl.globalPalette.button
                textColor: enabled ? "white" : QGroundControl.globalPalette.buttonText
                onClicked: fuelCellPane.controller.confirmCommand(fuelCellPane.controller._commandEmergencyStop)
            }
        }

        QGCLabel {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            visible: text.length > 0
            text: fuelCellPane.controller.requestText
            wrapMode: Text.Wrap
        }
        QGCLabel {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            visible: text.length > 0
            text: fuelCellPane.controller.controlHint
            wrapMode: Text.Wrap
        }
    }

    contentComponent: Component {
        RowLayout {
            width: root.columnWidth * 2 + root.gap * 2 + 1
            spacing: root.gap

            BatteryPane {
                Layout.minimumWidth: root.columnWidth
                Layout.preferredWidth: root.columnWidth
                Layout.maximumWidth: root.columnWidth
                Layout.alignment: Qt.AlignTop
                model: root.batteryModel
                vehicle: root.vehicle
            }

            Rectangle {
                Layout.preferredWidth: 1
                Layout.fillHeight: true
                color: QGroundControl.globalPalette.groupBorder
            }

            FuelCellPane {
                Layout.minimumWidth: root.columnWidth
                Layout.preferredWidth: root.columnWidth
                Layout.maximumWidth: root.columnWidth
                Layout.alignment: Qt.AlignTop
                controller: root
            }
        }
    }
}
