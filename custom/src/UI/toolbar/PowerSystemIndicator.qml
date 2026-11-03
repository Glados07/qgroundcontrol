import QtQuick

import QGroundControl
import QGroundControl.Controls
import QGroundControl.ScreenTools
import Custom.FuelCell
import Custom.Widgets

Item {
    id: control
    anchors.top: parent.top
    anchors.bottom: parent.bottom
    width: indicatorRow.width

    readonly property var manager: QGroundControl.corePlugin.fuelCellController
    readonly property var batteryModel: battery
    property bool showIndicator: battery.vehicleAvailable
    readonly property bool fcsAlert: Boolean(manager && manager.telemetryFresh
                                            && (manager.connectionState === FuelCellControllerManager.Disconnected
                                                || (manager.deviceDataValid && manager.fcsState >= 9 && manager.fcsState <= 12)))

    PowerSystemBattery { id: battery }

    Row {
        id: indicatorRow
        anchors.verticalCenter: parent.verticalCenter
        spacing: ScreenTools.defaultFontPixelWidth / 2

        QGCColoredImage {
            id: powerIcon
            width: control.height * 0.8
            height: width
            anchors.verticalCenter: parent.verticalCenter
            source: "qrc:/custom/img/FuelCellController.svg"
            sourceSize.width: width
            color: battery.iconColor

            Rectangle {
                anchors.top: parent.top
                anchors.right: parent.right
                width: ScreenTools.defaultFontPixelHeight / 2
                height: width
                radius: width / 2
                visible: control.fcsAlert
                color: QGroundControl.globalPalette.colorRed
                border.color: QGroundControl.globalPalette.window
            }
        }
        Column {
            anchors.verticalCenter: parent.verticalCenter
            QGCLabel { text: battery.voltageText }
            QGCLabel { text: battery.powerText }
        }
    }

    MouseArea {
        anchors.fill: parent
        enabled: control.showIndicator
        onClicked: mainWindow.showIndicatorDrawer(indicatorPage, control)
    }

    Component {
        id: indicatorPage
        PowerSystemIndicatorPage { indicator: control }
    }
}
