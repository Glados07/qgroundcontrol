/****************************************************************************
 *
 * Application Settings -> Fly View 中的思翼云台缩放设置组。
 *
 ****************************************************************************/

import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FactControls

Loader {
    id: root

    active: QGroundControl.corePlugin && QGroundControl.corePlugin.gimbalControlSettings
    sourceComponent: settingsComponent

    property var gimbalControlSettings: QGroundControl.corePlugin ? QGroundControl.corePlugin.gimbalControlSettings : undefined

    Component {
        id: settingsComponent

        SettingsGroupLayout {
            Layout.fillWidth: true
            heading: qsTr("SIYI Gimbal Zoom")
            headingDescription: qsTr("Controls the SIYI camera zoom through the private UDP SDK. The 1080p zoom range is limited to 1.0x - 5.5x.")

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
    }
}
