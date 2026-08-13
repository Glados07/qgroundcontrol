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

Loader {
    id: root

    implicitWidth: item ? item.implicitWidth : 0
    implicitHeight: item ? item.implicitHeight : 0
    active: QGroundControl.corePlugin && QGroundControl.corePlugin.gimbalControlSettings
    sourceComponent: settingsComponent

    property var gimbalControlSettings: QGroundControl.corePlugin
                                               ? QGroundControl.corePlugin.gimbalControlSettings
                                               : undefined

    Component {
        id: settingsComponent

        ColumnLayout {
            spacing: ScreenTools.defaultFontPixelHeight

            SettingsGroupLayout {
                Layout.fillWidth: true
                heading: qsTr("SIYI A8 Mini Gimbal Camera")
                headingDescription: qsTr("Private SDK camera controls.")

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

                LabelledFactTextField {
                    Layout.fillWidth: true
                    label: qsTr("Zoom Step")
                    fact: root.gimbalControlSettings.zoomStep
                    enabled: root.gimbalControlSettings.enabled.rawValue
                }
            }

            SettingsGroupLayout {
                Layout.fillWidth: true
                heading: qsTr("UniPod MT11 Gimbal Camera")
                headingDescription: qsTr("Independent SDK camera controls.")

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

                LabelledFactTextField {
                    Layout.fillWidth: true
                    label: qsTr("Zoom Step")
                    fact: root.gimbalControlSettings.mt11ZoomStep
                    enabled: root.gimbalControlSettings.mt11Enabled.rawValue
                }

                QGCLabel {
                    Layout.fillWidth: true
                    text: qsTr("MT11 SDK Host and Port control zoom, photo, recording and thermal mode. Configure its independent video address under Video > MT11 Second Video.")
                    wrapMode: Text.WordWrap
                    font.pointSize: ScreenTools.smallFontPointSize
                    opacity: 0.72
                }
            }
        }
    }
}
