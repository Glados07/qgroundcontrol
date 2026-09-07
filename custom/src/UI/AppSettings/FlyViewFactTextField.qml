import QtQuick
import QtQuick.Layouts

import QGroundControl.FactControls

FlyViewSettingsRow {
    label: fact ? fact.shortDescription : ""
    property alias fact: field.fact
    property alias textField: field

    FactTextField {
        id: field
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        // Keep native Fact validation, units, help and editing behavior.
    }
}
