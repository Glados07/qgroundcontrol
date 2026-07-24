/****************************************************************************
 *
 * Application Settings -> Fly View 中的思翼云台相机设置组。
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
            heading: qsTr("SIYI Gimbal Camera")
            headingDescription: qsTr("Controls zoom, photo capture and video recording through the private UDP SDK. Tap sends one configured step; holding repeats confirmed steps. Intermediate moves keep the full step, while the last move snaps 5.0x to 5.5x for 1080p or 3.0x to 3.5x for 2K; zooming out reverses that boundary move first. The first decoded 1080p or 2K pull-stream resolution is used for this application session; card-recording resolution and command 0x16 are not used.")

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
