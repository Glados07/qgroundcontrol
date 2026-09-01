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
                text: qsTr("UniRC SDK")
                wrapMode: Text.WordWrap
                font.bold: true
            }

            FactCheckBoxSlider {
                Layout.fillWidth: true
                text: qsTr("Enabled")
                fact: root.gimbalControlSettings.uniRcChannelControlEnabled
            }

            LabelledFactComboBox {
                Layout.fillWidth: true
                label: qsTr("SDK Interface")
                fact: root.gimbalControlSettings.uniRcSdkInterface
                indexModel: false
                enabled: root.gimbalControlSettings.uniRcChannelControlEnabled.rawValue
            }

            LabelledFactTextField {
                Layout.fillWidth: true
                label: qsTr("SDK Bluetooth Address")
                fact: root.gimbalControlSettings.uniRcSdkBluetoothAddress
                enabled: root.gimbalControlSettings.uniRcChannelControlEnabled.rawValue
            }

            QGCLabel {
                Layout.fillWidth: true
                text: qsTr("Channel Values")
                font.bold: true
            }

            GridLayout {
                Layout.fillWidth: true
                columns: root.width >= ScreenTools.defaultFontPixelWidth * 60 ? 3 : 2
                columnSpacing: ScreenTools.defaultFontPixelWidth * 3
                rowSpacing: ScreenTools.defaultFontPixelHeight / 2

                Repeater {
                    model: 16

                    delegate: RowLayout {
                        Layout.fillWidth: true
                        spacing: ScreenTools.defaultFontPixelWidth

                        QGCLabel {
                            text: qsTr("CH%1").arg(index + 1)
                            font.bold: true
                        }

                        QGCLabel {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignRight
                            font.family: ScreenTools.fixedFontFamily
                            text: root.uniRcChannelController
                                  && root.uniRcChannelController.sdkRouteActive
                                  && root.uniRcChannelController.channelValues
                                  && root.uniRcChannelController.channelValues.length > index
                                  ? root.uniRcChannelController.channelValues[index]
                                  : "--"
                        }
                    }
                }
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

            FactCheckBoxSlider {
                Layout.fillWidth: true
                text: qsTr("Reverse channel gimbal zoom control")
                fact: root.gimbalControlSettings.uniRcZoomDirectionReversed
                visible: Qt.platform.os === "android"
                enabled: root.gimbalControlSettings.enabled.rawValue
                         && root.gimbalControlSettings.uniRcChannelControlEnabled.rawValue
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
