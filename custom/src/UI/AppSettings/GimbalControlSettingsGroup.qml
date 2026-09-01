/****************************************************************************
 *
 * Application Settings -> Fly View private gimbal camera settings.
 *
 ****************************************************************************/

import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FactControls
import QGroundControl.ScreenTools

ColumnLayout {
    id: root

    Layout.fillWidth: true
    spacing: ScreenTools.defaultFontPixelHeight

    property var gimbalControlSettings: QGroundControl.corePlugin
                                                ? QGroundControl.corePlugin.gimbalControlSettings
                                                : null
    property var uniRcChannelController: QGroundControl.corePlugin
                                                ? QGroundControl.corePlugin.uniRcChannelController
                                                : null

    function uniRcConnectedStatus(stage) {
        switch (stage) {
        case "RFCOMM_CONNECTED":
            return qsTr("UniRC Bluetooth connected; preparing the 0x42 request")
        case "REQUEST_0X42_QUEUED":
            return qsTr("UniRC request queued locally; waiting for Bluetooth write")
        case "REQUEST_0X42_TRANSMITTED":
            return qsTr("UniRC request written locally; waiting for Bluetooth data")
        case "BT_RX_NO_SDK_FRAME":
            return qsTr("Bluetooth data received, but no valid UniRC SDK frame yet")
        case "SDK_FRAME_NO_0X42":
            return qsTr("Valid UniRC SDK frame received, but no 0x42 channel response yet")
        case "SDK_ROUTE_ACTIVE":
            return qsTr("UniRC Bluetooth SDK route and 0x42 response confirmed")
        default:
            return qsTr("UniRC Bluetooth connected; checking the SDK route")
        }
    }

    SettingsGroupLayout {
        Layout.fillWidth: true
        heading: qsTr("Gimbal Camera")

        ColumnLayout {
            Layout.fillWidth: true
            spacing: ScreenTools.defaultFontPixelHeight / 2

            QGCLabel {
                Layout.fillWidth: true
                text: qsTr("Zoom Step")
                font.bold: true
            }

            GridLayout {
                Layout.fillWidth: true
                columns: ScreenTools.isMobile ? 1 : 2
                columnSpacing: ScreenTools.defaultFontPixelWidth * 2
                rowSpacing: ScreenTools.defaultFontPixelHeight / 2

                LabelledFactTextField {
                    Layout.fillWidth: true
                    label: qsTr("A8 Mini")
                    fact: root.gimbalControlSettings.zoomStep
                    enabled: root.gimbalControlSettings.enabled.rawValue
                }

                LabelledFactTextField {
                    Layout.fillWidth: true
                    label: qsTr("MT11")
                    fact: root.gimbalControlSettings.mt11ZoomStep
                    enabled: root.gimbalControlSettings.mt11Enabled.rawValue
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: ScreenTools.defaultFontPixelHeight / 2
            visible: Qt.platform.os === "android"

            QGCLabel {
                Layout.fillWidth: true
                text: qsTr("UniRC CH9/CH10 Gimbal Control")
                wrapMode: Text.WordWrap
                font.bold: true
            }

            FactCheckBoxSlider {
                Layout.fillWidth: true
                text: qsTr("Enabled")
                fact: root.gimbalControlSettings.uniRcChannelControlEnabled
            }

            LabelledFactTextField {
                Layout.fillWidth: true
                label: qsTr("SDK Bluetooth Address")
                fact: root.gimbalControlSettings.uniRcSdkBluetoothAddress
                enabled: root.gimbalControlSettings.uniRcChannelControlEnabled.rawValue
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: ScreenTools.defaultFontPixelWidth

                QGCButton {
                    text: root.uniRcChannelController
                          && root.uniRcChannelController.bluetoothScanning
                          ? qsTr("Scanning...")
                          : qsTr("Scan BLUE Device")
                    enabled: root.gimbalControlSettings.uniRcChannelControlEnabled.rawValue
                             && root.uniRcChannelController
                             && !root.uniRcChannelController.bluetoothScanning
                    onClicked: root.uniRcChannelController.startBluetoothScan()
                }

                QGCLabel {
                    Layout.fillWidth: true
                    elide: Text.ElideMiddle
                    text: root.uniRcChannelController
                          && root.uniRcChannelController.selectedBluetoothDevice !== ""
                          ? root.uniRcChannelController.selectedBluetoothDevice
                          : qsTr("No Bluetooth device selected")
                }
            }

            Repeater {
                model: root.uniRcChannelController
                       ? root.uniRcChannelController.bluetoothDevices
                       : []

                delegate: QGCButton {
                    Layout.fillWidth: true
                    text: modelData
                    enabled: root.gimbalControlSettings.uniRcChannelControlEnabled.rawValue
                    onClicked: root.uniRcChannelController.selectBluetoothDevice(index)
                }
            }

            QGCLabel {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                font.pointSize: ScreenTools.smallFontPointSize
                text: qsTr("Set the UniGCS SDK interface to Bluetooth and pair the BLUE94/BLUE- device in Android Bluetooth settings.")
            }

            QGCLabel {
                Layout.fillWidth: true
                wrapMode: Text.WrapAnywhere
                font.pointSize: ScreenTools.smallFontPointSize
                visible: root.gimbalControlSettings.uniRcChannelControlEnabled.rawValue
                         && root.uniRcChannelController
                text: root.uniRcChannelController
                      ? root.uniRcChannelController.diagnosticSummary
                      : ""
            }

            QGCLabel {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                font.pointSize: ScreenTools.smallFontPointSize
                text: !root.gimbalControlSettings.uniRcChannelControlEnabled.rawValue
                      ? qsTr("Disabled")
                      : !root.uniRcChannelController
                        ? qsTr("Controller unavailable")
                          : root.uniRcChannelController.channelInputActive
                          ? qsTr("Receiving: CH9 %1, CH10 %2")
                                .arg(root.uniRcChannelController.channel9)
                                .arg(root.uniRcChannelController.channel10)
                          : root.uniRcChannelController.bluetoothScanning
                            ? qsTr("Scanning for UniRC Bluetooth devices")
                          : root.uniRcChannelController.lastError !== ""
                            ? root.uniRcChannelController.lastError
                            : root.uniRcChannelController.bluetoothConnected
                              ? root.uniRcConnectedStatus(
                                    root.uniRcChannelController.diagnosticStage)
                              : qsTr("Waiting for the UniRC SDK Bluetooth connection")
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: ScreenTools.defaultFontPixelHeight / 2

            QGCLabel {
                Layout.fillWidth: true
                text: qsTr("SIYI A8 Mini Gimbal Camera")
                wrapMode: Text.WordWrap
                font.bold: true
            }

            FactCheckBoxSlider {
                Layout.fillWidth: true
                text: qsTr("Enabled")
                fact: root.gimbalControlSettings.enabled
            }

            LabelledFactTextField {
                Layout.fillWidth: true
                label: qsTr("SDK Host")
                fact: root.gimbalControlSettings.sdkHost
                enabled: root.gimbalControlSettings.enabled.rawValue
            }

            LabelledFactTextField {
                Layout.fillWidth: true
                label: qsTr("SDK Port")
                fact: root.gimbalControlSettings.sdkPort
                enabled: root.gimbalControlSettings.enabled.rawValue
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: ScreenTools.defaultFontPixelHeight / 2

            QGCLabel {
                Layout.fillWidth: true
                text: qsTr("UniPod MT11 Gimbal Camera")
                wrapMode: Text.WordWrap
                font.bold: true
            }

            FactCheckBoxSlider {
                Layout.fillWidth: true
                text: qsTr("Enabled")
                fact: root.gimbalControlSettings.mt11Enabled
            }

            LabelledFactTextField {
                Layout.fillWidth: true
                label: qsTr("SDK Host")
                fact: root.gimbalControlSettings.mt11SdkHost
                enabled: root.gimbalControlSettings.mt11Enabled.rawValue
            }

            LabelledFactTextField {
                Layout.fillWidth: true
                label: qsTr("SDK Port")
                fact: root.gimbalControlSettings.mt11SdkPort
                enabled: root.gimbalControlSettings.mt11Enabled.rawValue
            }
        }
    }
}
