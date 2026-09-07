import QtQuick
import QtQuick.Layouts

import QGroundControl.Controls

FlyViewSettingsRow {
    id: root
    property alias model: field.model
    property alias currentIndex: field.currentIndex
    property alias currentText: field.currentText
    property alias comboBox: field
    signal activated(int index)

    QGCComboBox {
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
