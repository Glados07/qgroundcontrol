import QtQuick
import QtQuick.Controls

import QGroundControl.Controls
import QGroundControl.Palette
import QGroundControl.ScreenTools

// Keep long translated enum entries readable inside a bounded popup.
ItemDelegate {
    id: root
    property real comboBoxWidth
    width: ListView.view ? ListView.view.width : comboBoxWidth
    implicitHeight: Math.max(ScreenTools.defaultFontPixelHeight * 2,
                             contentItem.implicitHeight + topPadding + bottomPadding)

    QGCPalette { id: optionPalette; colorGroupEnabled: root.enabled }

    contentItem: QGCLabel {
        text: root.text
        font: root.font
        wrapMode: Text.Wrap
        color: root.highlighted ? optionPalette.buttonHighlightText : optionPalette.buttonText
    }

    background: Rectangle {
        color: root.highlighted ? optionPalette.buttonHighlight : optionPalette.button
    }
}
