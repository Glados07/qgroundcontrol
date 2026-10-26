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
    readonly property real availableWidth: Math.max(0, mainWindow.width - ScreenTools.defaultFontPixelWidth * 4)
    readonly property bool wide: availableWidth >= columnWidth * 2 + gap * 2 + 1
    readonly property real panelWidth: Math.min(availableWidth, wide ? columnWidth * 2 + gap * 2 + 1 : columnWidth)

    Connections {
        target: root.indicator.batteryModel
        // Close only this drawer, before its old parameter editors can be reused for another vehicle.
        function onVehicleChanged() { if (root.drawer) root.drawer.close() }
    }

    contentComponent: Component {
        ColumnLayout {
            width: root.panelWidth
            spacing: ScreenTools.defaultFontPixelHeight / 2

            RowLayout {
                Layout.fillWidth: true
                visible: !root.wide
                spacing: root.gap
                QGCButton {
                    Layout.fillWidth: true
                    text: qsTr("Power Status")
                    checkable: true
                    autoExclusive: true
                    checked: root.indicator.selectedTab === 0
                    onClicked: root.indicator.selectedTab = 0
                }
                QGCButton {
                    Layout.fillWidth: true
                    text: qsTr("Fuel Cell")
                    checkable: true
                    autoExclusive: true
                    checked: root.indicator.selectedTab === 1
                    onClicked: root.indicator.selectedTab = 1
                }
            }

            // Keep both panes alive across tab/width changes: no control state or editor is recreated.
            RowLayout {
                Layout.fillWidth: true
                spacing: root.gap
                Loader {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.preferredWidth: root.columnWidth
                    Layout.alignment: Qt.AlignTop
                    visible: root.wide || root.indicator.selectedTab === 0
                    sourceComponent: root.indicator.batteryModel.contentComponent
                }
                Rectangle {
                    Layout.preferredWidth: 1
                    Layout.fillHeight: true
                    color: QGroundControl.globalPalette.groupBorder
                    visible: root.wide
                }
                FuelCellControllerPage {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.preferredWidth: contentWidth
                    Layout.alignment: Qt.AlignTop
                    visible: root.wide || root.indicator.selectedTab === 1
                    contentWidth: root.wide ? root.columnWidth : root.panelWidth
                    manager: root.indicator.manager
                }
            }
        }
    }
}
