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
    readonly property real contentMaximumWidth: ScreenTools.defaultFontPixelWidth * 100

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
            x: (flickable.width - width) / 2
            y: root.pageMargin
            // Center a readable, font-scaled column; shrink it to fit narrow viewports.
            // Never derive page width from a long label, path or combo model.
            width: Math.min(root.contentMaximumWidth,
                            Math.max(0, flickable.width - root.pageMargin * 2))
            spacing: ScreenTools.defaultFontPixelHeight * 1.25
        }
    }
}
