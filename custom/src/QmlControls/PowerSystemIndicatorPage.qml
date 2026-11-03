import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.ScreenTools

ToolIndicatorPage {
    id: root
    required property var indicator
    readonly property real columnWidth: ScreenTools.defaultFontPixelWidth * 44
    readonly property real gap: ScreenTools.defaultFontPixelWidth

    Connections {
        target: root.indicator.batteryModel
        // Close only this drawer, before its old parameter editors can be reused for another vehicle.
        function onVehicleChanged() { if (root.drawer) root.drawer.close() }
    }

    contentComponent: Component {
        RowLayout {
            width: root.columnWidth * 2 + root.gap * 2 + 1
            spacing: root.gap
            Loader {
                Layout.preferredWidth: root.columnWidth
                Layout.alignment: Qt.AlignTop
                sourceComponent: root.indicator.batteryModel.contentComponent
            }
            Rectangle {
                Layout.preferredWidth: 1
                Layout.fillHeight: true
                color: QGroundControl.globalPalette.groupBorder
            }
            FuelCellControllerPage {
                Layout.preferredWidth: root.columnWidth
                Layout.alignment: Qt.AlignTop
                manager: root.indicator.manager
            }
        }
    }
}
