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
    property bool showIndicator: manager && manager.vehicleAvailable

    readonly property string statusText: {
        if (!manager || !manager.telemetryAvailable) return qsTr("Waiting for status")
        switch (manager.connectionState) {
        case FuelCellControllerManager.NeverConnected: return qsTr("Never connected")
        case FuelCellControllerManager.Connected: return qsTr("Connected")
        case FuelCellControllerManager.Disconnected: return qsTr("Disconnected")
        default: return qsTr("Unknown")
        }
    }
    readonly property color statusColor: {
        if (!manager || !manager.telemetryAvailable) return QGroundControl.globalPalette.text
        if (manager.connectionState === FuelCellControllerManager.Disconnected
                || (manager.deviceDataValid && manager.fcsState >= 9)) {
            return QGroundControl.globalPalette.colorRed
        }
        return manager.deviceDataValid ? QGroundControl.globalPalette.colorGreen
                                       : QGroundControl.globalPalette.colorOrange
    }

    Row {
        id: indicatorRow
        anchors.verticalCenter: parent.verticalCenter
        spacing: ScreenTools.defaultFontPixelWidth / 2

        QGCColoredImage {
            width: control.height * 0.8
            height: width
            anchors.verticalCenter: parent.verticalCenter
            source: "qrc:/custom/img/FuelCellController.svg"
            sourceSize.width: width
            color: control.statusColor
        }

        Column {
            anchors.verticalCenter: parent.verticalCenter
            QGCLabel {
                text: "FCS"
                color: control.statusColor
                font.pointSize: ScreenTools.smallFontPointSize
            }
            QGCLabel {
                text: control.statusText
                color: control.statusColor
                font.pointSize: ScreenTools.smallFontPointSize
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        enabled: control.showIndicator
        onClicked: mainWindow.showIndicatorDrawer(indicatorPage, control)
    }

    Component {
        id: indicatorPage
        FuelCellControllerPage {
            manager: control.manager
        }
    }
}
