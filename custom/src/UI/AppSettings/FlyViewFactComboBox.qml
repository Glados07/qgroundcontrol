import QtQuick
import QtQuick.Layouts

import QGroundControl.FactControls

FlyViewSettingsRow {
    id: root
    property alias fact: field.fact
    property alias indexModel: field.indexModel
    property alias comboBox: field
    signal activated(int index)

    FactComboBox {
        id: field
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        sizeToContents: false
        onActivated: (index) => root.activated(index)

        delegate: FlyViewComboBoxDelegate {
            comboBoxWidth: field.width
            text: modelData
            font: field.font
            highlighted: field.highlightedIndex === index
        }
    }

    Binding {
        target: field.contentItem
        property: "elide"
        value: Text.ElideRight
    }
}
