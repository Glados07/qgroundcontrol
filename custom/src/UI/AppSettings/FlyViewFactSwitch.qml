import QtQuick
import QtQuick.Layouts

import QGroundControl.Controls
import QGroundControl.FactControls
import QGroundControl.Palette
import QGroundControl.ScreenTools

// Retain FactCheckBoxSlider's Fact binding and full-row mouse/touch behavior.
FactCheckBoxSlider {
    id: root
    Layout.fillWidth: true
    Layout.minimumWidth: 0
    implicitWidth: 0
    implicitHeight: Math.max(ScreenTools.defaultFontPixelHeight * 2,
                             contentItem.implicitHeight)

    QGCPalette { id: switchPalette; colorGroupEnabled: root.enabled }

    contentItem: Item {
        implicitHeight: Math.max(label.implicitHeight, indicator.height)

        QGCLabel {
            id: label
            anchors.left: parent.left
            anchors.right: indicator.left
            anchors.rightMargin: ScreenTools.defaultFontPixelWidth * 2
            anchors.verticalCenter: parent.verticalCenter
            text: root.text
            wrapMode: Text.Wrap
        }

        Rectangle {
            id: indicator
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            height: ScreenTools.defaultFontPixelHeight * 1.1
            width: height * 2
            radius: height / 2
            color: root.checked ? switchPalette.primaryButton : switchPalette.button
            border.color: switchPalette.buttonBorder

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                x: root.checked ? indicator.width - width - 2 : 2
                height: parent.height - 4
                width: height
                radius: height / 2
                color: switchPalette.buttonText
            }
        }
    }
}
