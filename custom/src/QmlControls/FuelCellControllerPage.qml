import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.ScreenTools
import Custom.FuelCell

ToolIndicatorPage {
    id: root

    required property var manager
    property real contentWidth: Math.min(ScreenTools.defaultFontPixelWidth * 44,
                                         mainWindow.width - ScreenTools.defaultFontPixelWidth * 4)
    readonly property var vehicle: QGroundControl.multiVehicleManager.activeVehicle
    property var _confirmationDialog: null

    // 顺序对应协议 FUEL_CELL_DEVICE_STATE 的 0～12，不能用控制指令代替实际状态。
    readonly property var _fcsStates: [
        qsTr("Powering on"), qsTr("Power on complete"), qsTr("Entering standby"), qsTr("Standby"),
        qsTr("Starting"), qsTr("Start complete"), qsTr("Running"), qsTr("Stopping"),
        qsTr("Stop complete"), qsTr("Fault shutdown in progress"), qsTr("Fault shutdown complete"),
        qsTr("Emergency stop in progress"), qsTr("Emergency stop complete")
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
        const command = commandText(manager.requestedCommand)
        switch (manager.requestState) {
        case FuelCellControllerManager.Pending:
            return qsTr("%1: Waiting for flight controller confirmation...").arg(command)
                    + (manager.lastError ? "\n" + manager.lastError : "")
        case FuelCellControllerManager.Accepted:
            return manager.lastError || qsTr("Flight controller accepted %1. Check FCS system status for progress.").arg(command)
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
        if (!manager.commandPending && !manager.canControl) return qsTr("Control link is unavailable.")
        return ""
    }

    function formatValue(value, decimals, unit) {
        return isFinite(value) ? Number(value).toFixed(decimals) + " " + unit : "—"
    }

    function commandText(command) {
        return command >= 0 && command < _commands.length ? _commands[command] : "—"
    }

    function closeConfirmation() {
        if (_confirmationDialog) _confirmationDialog.close()
    }

    function confirmCommand(command) {
        if (_confirmationDialog || !vehicle
                || !(command === 2 ? manager.canEmergencyStop : manager.canControl)) return

        // 在打开确认框时捕获会话，确认时交回后端校验，禁止向切换后的飞控发送旧操作。
        const revision = manager.sessionRevision
        _confirmationDialog = confirmationComponent.createObject(mainWindow, {
            title: qsTr("Confirm %1").arg(commandText(command)),
            text: qsTr("Send %2 to the Fuel Cell Controller on vehicle #%1?")
                      .arg(vehicle.id).arg(commandText(command)),
            acceptFunction: function() {
                if (command === 1) manager.requestStartFcs(revision)
                else if (command === 0) manager.requestStopFcs(revision)
                else manager.requestEmergencyStopFcs(revision)
            }
        })
        _confirmationDialog.open()
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
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignRight
        }
    }

    contentComponent: Component {
        // Bound the Loader's implicit width; long wrapped text must not widen the pane.
        Item {
            implicitWidth: root.contentWidth
            implicitHeight: content.implicitHeight
            ColumnLayout {
                id: content
                width: parent.width
                spacing: ScreenTools.defaultFontPixelHeight / 2

                SettingsGroupLayout {
                    Layout.fillWidth: true
                    heading: qsTr("Fuel Cell Controller")
                    showDividers: false
                    contentSpacing: ScreenTools.defaultFontPixelHeight / 4

                    StatusField {
                        label: qsTr("Device connection")
                        labelText: root.connectionText
                    }
                    StatusField {
                        label: qsTr("FCS system status")
                        labelText: root.manager.fcsState >= 0 && root.manager.fcsState < root._fcsStates.length
                                   ? root._fcsStates[root.manager.fcsState] : "—"
                    }
                    StatusField {
                        label: qsTr("Lithium battery SOC")
                        labelText: root.formatValue(root.manager.socPercent, 1, "%")
                    }
                }

                SettingsGroupLayout {
                    Layout.fillWidth: true
                    heading: qsTr("Flight controller setpoints")
                    showDividers: false
                    contentSpacing: ScreenTools.defaultFontPixelHeight / 4

                    StatusField {
                        label: qsTr("Bus voltage")
                        labelText: root.formatValue(root.manager.busVoltage, 1, "V")
                    }
                    StatusField {
                        label: qsTr("Bus current")
                        labelText: root.formatValue(root.manager.busCurrent, 1, "A")
                    }
                    StatusField {
                        label: qsTr("Requested power")
                        labelText: root.formatValue(root.manager.requestedPower, 0, "W")
                    }
                }

                SettingsGroupLayout {
                    Layout.fillWidth: true
                    heading: qsTr("FCS control")
                    showDividers: false
                    contentSpacing: ScreenTools.defaultFontPixelHeight / 2

                    StatusField {
                        label: qsTr("Current command")
                        labelText: root.commandText(root.manager.acceptedCommand)
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        QGCButton {
                            Layout.fillWidth: true
                            text: qsTr("FCS On")
                            enabled: root.manager.canControl
                            onClicked: root.confirmCommand(1)
                        }
                        QGCButton {
                            Layout.fillWidth: true
                            text: qsTr("FCS Off")
                            enabled: root.manager.canControl
                            onClicked: root.confirmCommand(0)
                        }
                    }
                    // 急停有独立门限，普通请求等待确认时仍可抢占。
                    QGCButton {
                        Layout.fillWidth: true
                        text: qsTr("FCS Emergency Stop")
                        enabled: root.manager.canEmergencyStop
                        backgroundColor: enabled ? QGroundControl.globalPalette.colorRed : QGroundControl.globalPalette.button
                        textColor: enabled ? "white" : QGroundControl.globalPalette.buttonText
                        onClicked: root.confirmCommand(2)
                    }
                }

                QGCLabel {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    visible: text.length > 0
                    text: root.requestText
                    wrapMode: Text.Wrap
                }
                QGCLabel {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    visible: text.length > 0
                    text: root.controlHint
                    wrapMode: Text.Wrap
                }
            }
        }
    }
}
