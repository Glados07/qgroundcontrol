import QtQuick
import QtQuick.Layouts

import QGroundControl.Controls
import QGroundControl.Palette
import QGroundControl.ScreenTools

ColumnLayout {
    id: root

    default property alias contentItem: contentLayout.data
    property string heading
    property string headingDescription
    property alias contentSpacing: contentLayout.spacing
    readonly property real padding: ScreenTools.defaultFontPixelHeight * 0.8

    Layout.fillWidth: true
    Layout.minimumWidth: 0
    implicitWidth: 0
    spacing: ScreenTools.defaultFontPixelHeight * 0.5

    QGCPalette { id: sectionPalette; colorGroupEnabled: root.enabled }

    ColumnLayout {
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        Layout.leftMargin: root.padding / 3
        Layout.rightMargin: root.padding / 3
        spacing: ScreenTools.defaultFontPixelHeight / 4
        visible: root.heading !== "" || root.headingDescription !== ""

        QGCLabel {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            text: root.heading
            visible: text !== ""
            wrapMode: Text.Wrap
            font.bold: true
            font.pointSize: ScreenTools.defaultFontPointSize + 1
        }

        QGCLabel {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            text: root.headingDescription
            visible: text !== ""
            wrapMode: Text.Wrap
            font.pointSize: ScreenTools.smallFontPointSize
            opacity: 0.75
        }
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        implicitHeight: contentLayout.implicitHeight + root.padding * 2
        radius: ScreenTools.defaultFontPixelHeight * 0.6
        color: sectionPalette.windowShade
        border.color: sectionPalette.groupBorder

        ColumnLayout {
            id: contentLayout
            x: root.padding
            y: root.padding
            width: Math.max(0, parent.width - root.padding * 2)
            spacing: ScreenTools.defaultFontPixelHeight * 0.65
        }
    }
}
