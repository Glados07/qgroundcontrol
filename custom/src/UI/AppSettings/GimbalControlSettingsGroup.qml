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
import QGroundControl.Palette
import QGroundControl.ScreenTools

ColumnLayout {
    id: root

    Layout.fillWidth: true
    Layout.minimumWidth: 0
    spacing: ScreenTools.defaultFontPixelHeight * 1.25
    property bool showUniRcSettings: Qt.platform.os === "android"

    QGCPalette { id: channelPalette }

    property var gimbalControlSettings: QGroundControl.corePlugin
                                                ? QGroundControl.corePlugin.gimbalControlSettings
                                                : null
    property var uniRcChannelController: QGroundControl.corePlugin
                                                ? QGroundControl.corePlugin.uniRcChannelController
                                                : null

    FlyViewSettingsSection {
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
                columns: width >= ScreenTools.defaultFontPixelWidth * 72 ? 2 : 1
                columnSpacing: ScreenTools.defaultFontPixelWidth * 2
                rowSpacing: ScreenTools.defaultFontPixelHeight / 2

                FlyViewFactTextField {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    label: qsTr("A8 Mini")
                    fact: root.gimbalControlSettings.zoomStep
                    enabled: root.gimbalControlSettings.enabled.rawValue
                }

                FlyViewFactTextField {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    label: qsTr("MT11")
                    fact: root.gimbalControlSettings.mt11ZoomStep
                    enabled: root.gimbalControlSettings.mt11Enabled.rawValue
                }
            }
        }
    }

    FlyViewSettingsSection {
        objectName: "uniRcSettingsSection"
        heading: qsTr("UniRC SDK")
        visible: root.showUniRcSettings

        FlyViewFactSwitch {
            Layout.fillWidth: true
            text: qsTr("Enabled")
            fact: root.gimbalControlSettings.uniRcChannelControlEnabled
        }

        FlyViewComboBox {
            Layout.fillWidth: true
            label: qsTr("SDK Interface")
            model: [qsTranslate("GimbalControl.SettingsGroup.json", "Bluetooth")]
            currentIndex: root.gimbalControlSettings.uniRcSdkInterface.enumIndex
            enabled: root.gimbalControlSettings.uniRcChannelControlEnabled.rawValue

            onActivated: (index) => {
                const interfaceFact = root.gimbalControlSettings.uniRcSdkInterface
                if (index >= 0 && index < interfaceFact.enumValues.length) {
                    interfaceFact.value = interfaceFact.enumValues[index]
                }
            }
        }

        FlyViewFactTextField {
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
            objectName: "uniRcChannelGrid"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            columns: width >= ScreenTools.defaultFontPixelWidth * 90 ? 4
                     : width >= ScreenTools.defaultFontPixelWidth * 60 ? 3
                     : width >= ScreenTools.defaultFontPixelWidth * 30 ? 2 : 1
            columnSpacing: ScreenTools.defaultFontPixelWidth
            rowSpacing: ScreenTools.defaultFontPixelHeight / 2

            Repeater {
                model: 16

                delegate: Rectangle {
                    objectName: "uniRcChannelTile"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.preferredWidth: 1
                    implicitHeight: ScreenTools.defaultFontPixelHeight * 2.2
                    radius: ScreenTools.defaultFontPixelHeight / 3
                    color: channelPalette.window
                    border.color: channelPalette.groupBorder
                    border.width: 1

                    QGCLabel {
                        anchors.left: parent.left
                        anchors.leftMargin: ScreenTools.defaultFontPixelWidth
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("CH%1").arg(index + 1)
                        font.pointSize: ScreenTools.smallFontPointSize
                        font.bold: true
                    }

                    QGCLabel {
                        anchors.right: parent.right
                        anchors.rightMargin: ScreenTools.defaultFontPixelWidth
                        anchors.verticalCenter: parent.verticalCenter
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

    GridLayout {
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        columns: width >= ScreenTools.defaultFontPixelWidth * 100 ? 2 : 1
        columnSpacing: ScreenTools.defaultFontPixelHeight
        rowSpacing: ScreenTools.defaultFontPixelHeight * 1.25

        FlyViewSettingsSection {
            Layout.preferredWidth: 1
            Layout.alignment: Qt.AlignTop
            heading: qsTr("SIYI A8 Mini Gimbal Camera")

            FlyViewFactSwitch {
                Layout.fillWidth: true
                text: qsTr("Enabled")
                fact: root.gimbalControlSettings.enabled
            }

            FlyViewFactSwitch {
                Layout.fillWidth: true
                text: qsTr("Reverse channel gimbal zoom control")
                fact: root.gimbalControlSettings.uniRcZoomDirectionReversed
                visible: root.showUniRcSettings
                enabled: root.gimbalControlSettings.enabled.rawValue
                         && root.gimbalControlSettings.uniRcChannelControlEnabled.rawValue
            }

            FlyViewFactTextField {
                Layout.fillWidth: true
                label: qsTr("SDK Host")
                fact: root.gimbalControlSettings.sdkHost
                enabled: root.gimbalControlSettings.enabled.rawValue
            }

            FlyViewFactTextField {
                Layout.fillWidth: true
                label: qsTr("SDK Port")
                fact: root.gimbalControlSettings.sdkPort
                enabled: root.gimbalControlSettings.enabled.rawValue
            }
        }

        FlyViewSettingsSection {
            Layout.preferredWidth: 1
            Layout.alignment: Qt.AlignTop
            heading: qsTr("UniPod MT11 Gimbal Camera")

            FlyViewFactSwitch {
                Layout.fillWidth: true
                text: qsTr("Enabled")
                fact: root.gimbalControlSettings.mt11Enabled
            }

            FlyViewFactTextField {
                Layout.fillWidth: true
                label: qsTr("SDK Host")
                fact: root.gimbalControlSettings.mt11SdkHost
                enabled: root.gimbalControlSettings.mt11Enabled.rawValue
            }

            FlyViewFactTextField {
                Layout.fillWidth: true
                label: qsTr("SDK Port")
                fact: root.gimbalControlSettings.mt11SdkPort
                enabled: root.gimbalControlSettings.mt11Enabled.rawValue
            }
        }
    }
}
