import QtQuick
import QtQuick.Layouts

import QGroundControl.Controls
import QGroundControl.Palette
import QGroundControl.ScreenTools

// Local to Fly View: do not change the sizing contract of other settings pages.
Item {
    id: root

    default property alias contentItem: mainLayout.data
    readonly property real pageMargin: ScreenTools.defaultFontPixelWidth
                                       * (width < ScreenTools.defaultFontPixelWidth * 60 ? 1.5 : 3)

    QGCPalette { id: qgcPal }

    QGCFlickable {
        id: flickable
        objectName: "flyViewSettingsFlickable"
        anchors.fill: parent
        contentWidth: width
        contentHeight: mainLayout.height + root.pageMargin * 2
        flickableDirection: Flickable.VerticalFlick

        ColumnLayout {
            id: mainLayout
            objectName: "flyViewSettingsContent"
            x: root.pageMargin
            y: root.pageMargin
            // Never derive page width from a long label, path or combo model.
            width: Math.max(0, flickable.width - root.pageMargin * 2)
            spacing: ScreenTools.defaultFontPixelHeight * 1.25
        }
    }
}
