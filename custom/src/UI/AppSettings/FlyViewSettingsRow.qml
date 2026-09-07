import QtQuick
import QtQuick.Layouts

import QGroundControl.Controls
import QGroundControl.ScreenTools

GridLayout {
    id: root

    default property alias contentItem: controlLayout.data
    property string label
    property real controlPreferredWidth: ScreenTools.defaultFontPixelWidth * 26
    readonly property bool stacked: width < ScreenTools.defaultFontPixelWidth * 66

    Layout.fillWidth: true
    Layout.minimumWidth: 0
    columns: stacked ? 1 : 2
    columnSpacing: ScreenTools.defaultFontPixelWidth * 3
    rowSpacing: ScreenTools.defaultFontPixelHeight * 0.35

    QGCLabel {
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        text: root.label
        wrapMode: Text.Wrap
    }

    RowLayout {
        id: controlLayout
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        Layout.preferredWidth: root.controlPreferredWidth
        Layout.maximumWidth: root.stacked ? Number.POSITIVE_INFINITY : root.controlPreferredWidth
        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
        spacing: ScreenTools.defaultFontPixelWidth
    }
}
