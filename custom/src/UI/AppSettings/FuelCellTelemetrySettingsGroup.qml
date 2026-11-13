import QtQuick
import QtQuick.Layouts

import Custom.FuelCell
import QGroundControl.Controls
import QGroundControl.FactControls
import QGroundControl.ScreenTools

SettingsGroupLayout {
    heading: qsTr("Fuel Cell Telemetry")
    headingDescription: qsTr("Address and port for requesting fuel cell data from the AI box.")

    RowLayout {
        Layout.fillWidth: true
        spacing: ScreenTools.defaultFontPixelWidth * 2

        QGCLabel {
            Layout.fillWidth: true
            text: qsTr("AI box IPv4 address")
        }

        QGCTextField {
            Layout.preferredWidth: ScreenTools.defaultFontPixelWidth * 20
            text: FuelCellTelemetrySettings.boxAddress.valueString

            onEditingFinished: {
                if (FuelCellTelemetrySettings.saveBoxAddress(text)) {
                    clearValidationError()
                } else {
                    showValidationError(qsTr("Enter a valid unicast IPv4 address."),
                                        FuelCellTelemetrySettings.boxAddress.valueString)
                }
            }
        }
    }

    LabelledFactTextField {
        Layout.fillWidth: true
        textFieldPreferredWidth: ScreenTools.defaultFontPixelWidth * 20
        label: qsTr("AI box UDP port")
        fact: FuelCellTelemetrySettings.boxPort
    }
}
