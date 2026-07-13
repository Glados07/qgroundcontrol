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
import QGroundControl.ScreenTools

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

            FactCheckBoxSlider {
                Layout.fillWidth: true
                text: qsTr("Use MAVLink automatic video stream")
                fact: root.gimbalControlSettings.mavlinkAutoVideoStream
                enabled: root.gimbalControlSettings.enabled.rawValue
            }

            QGCLabel {
                Layout.fillWidth: true
                text: qsTr("Keep this option off to edit the video source manually. Restart QGC after changing it.")
                wrapMode: Text.WordWrap
                font.pointSize: ScreenTools.smallFontPointSize
                opacity: 0.72
            }
        }
    }
}
