/****************************************************************************
 *
 * UniRC air-link indicator for the active vehicle's connected toolbar.
 *
 ****************************************************************************/

import QtQuick

import QGroundControl
import QGroundControl.Controls
import QGroundControl.Palette
import QGroundControl.ScreenTools

import Custom.Widgets

Item {
    id: control
    objectName: "uniRcLinkIndicator"

    anchors.top: parent.top
    anchors.bottom: parent.bottom
    width: Math.max(indicatorRow.width, ScreenTools.defaultFontPixelHeight * 2.5)
    activeFocusOnTab: showIndicator

    property bool showIndicator: !!_controller && !!_vehicleLinkManager
                                 && !_vehicleLinkManager.communicationLost
    property bool waitForParameters: false
    readonly property var _activeVehicle: QGroundControl.multiVehicleManager.activeVehicle
    readonly property var _vehicleLinkManager: _activeVehicle ? _activeVehicle.vehicleLinkManager : null
    readonly property var _controller: QGroundControl.corePlugin
                                       ? QGroundControl.corePlugin.uniRcChannelController : null
    readonly property var _linkStatus: _controller ? _controller.linkStatus : null
    readonly property bool _available: !!_linkStatus && _linkStatus.available
    readonly property string _strengthText: _available ? _linkStatus.strengthPercent + "%" : "--"

    Accessible.role: Accessible.Button
    Accessible.name: qsTr("Air link") + ": " + (_available ? _strengthText : qsTr("Not available"))
    Accessible.onPressAction: openDetails()

    function openDetails() {
        if (showIndicator) {
            mainWindow.showIndicatorDrawer(indicatorPage, control)
        }
    }

    Keys.onReturnPressed: openDetails()
    Keys.onSpacePressed: openDetails()

    QGCPalette { id: linkPalette }

    TextMetrics {
        id: maximumStrengthMetrics
        font: strengthLabel.font
        text: "100%"
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: -ScreenTools.defaultFontPixelWidth / 3
        radius: ScreenTools.defaultFontPixelWidth / 2
        color: "transparent"
        border.width: 1
        border.color: signalIcon.signalColor
        visible: control.activeFocus
    }

    Row {
        id: indicatorRow

        anchors.centerIn: parent
        spacing: ScreenTools.defaultFontPixelWidth / 2

        UniRcSignalIcon {
            id: signalIcon
            objectName: "uniRcToolbarSignalIcon"

            width: control.height
            height: width
            available: control._available
            strengthLevel: control._available ? control._linkStatus.strengthLevel : 0
        }

        Column {
            anchors.verticalCenter: parent.verticalCenter
            width: Math.max(linkLabel.implicitWidth, maximumStrengthMetrics.width)

            QGCLabel {
                id: linkLabel
                text: qsTr("Air link")
                font.pointSize: ScreenTools.smallFontPointSize
                color: linkPalette.text
            }

            QGCLabel {
                id: strengthLabel
                objectName: "uniRcToolbarStrength"
                text: control._strengthText
                font.pointSize: ScreenTools.smallFontPointSize
                font.bold: true
                color: signalIcon.signalColor
            }
        }
    }

    MouseArea {
        objectName: "uniRcLinkTouchArea"
        anchors.centerIn: parent
        width: parent.width
        height: Math.max(parent.height, ScreenTools.defaultFontPixelHeight * 2.2)
        enabled: control.showIndicator
        onClicked: control.openDetails()
    }

    Component {
        id: indicatorPage

        UniRcLinkIndicatorPage {
            linkStatus: control._linkStatus
        }
    }
}
