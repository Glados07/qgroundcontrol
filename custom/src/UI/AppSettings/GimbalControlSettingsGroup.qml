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
                label: qsTr("SDK Serial Device")
                fact: root.gimbalControlSettings.uniRcSdkSerialPort
                enabled: root.gimbalControlSettings.uniRcChannelControlEnabled.rawValue
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
                          : root.uniRcChannelController.lastError !== ""
                            ? root.uniRcChannelController.lastError
                            : root.uniRcChannelController.serialOpen
                              ? qsTr("Serial open; waiting for channel data")
                              : qsTr("Waiting to open SDK serial device")
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
