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

    implicitWidth: item ? item.implicitWidth : 0
    implicitHeight: item ? item.implicitHeight : 0
    active: QGroundControl.corePlugin && QGroundControl.corePlugin.gimbalControlSettings
    sourceComponent: settingsComponent

    property var gimbalControlSettings: QGroundControl.corePlugin ? QGroundControl.corePlugin.gimbalControlSettings : undefined

    Component {
        id: settingsComponent

        SettingsGroupLayout {
            Layout.fillWidth: true
            heading: qsTr("SIYI Gimbal Camera")
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
    }
}
