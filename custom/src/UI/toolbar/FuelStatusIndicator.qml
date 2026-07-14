/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

import QtQuick

import QGroundControl
import QGroundControl.Controls
import QGroundControl.MultiVehicleManager
import QGroundControl.ScreenTools
import QGroundControl.Palette

import Custom.Widgets

//-------------------------------------------------------------------------
//-- Fuel Status Indicator
Item {
    id:             control
    anchors.top:    parent.top
    anchors.bottom: parent.bottom
    width:          fuelIndicatorRow.width

    property bool showIndicator:     _hasFuel
    property bool waitForParameters: false

    property var  _activeVehicle: QGroundControl.multiVehicleManager.activeVehicle
    property var  _fuelStatus:    _activeVehicle ? _activeVehicle.fuelStatus : null
    property bool _hasFuel:       _fuelStatus && _fuelStatus.telemetryAvailable
    property real _pctRemaining:  _hasFuel ? _fuelStatus.percentRemaining.rawValue : NaN

    function getFuelColor() {
        if (isNaN(_pctRemaining)) {
            return qgcPal.text
        }
        if (_pctRemaining > 50) {
            return qgcPal.colorGreen
        } else if (_pctRemaining > 25) {
            return qgcPal.colorOrange
        }
        return qgcPal.colorRed
    }

    function getFuelText() {
        return isNaN(_pctRemaining) ? qsTr("n/a") : Math.round(_pctRemaining) + "%"
    }

    Row {
        id:             fuelIndicatorRow
        anchors.top:    parent.top
        anchors.bottom: parent.bottom
        visible:        _hasFuel
        spacing:        ScreenTools.defaultFontPixelWidth / 2

        QGCColoredImage {
            anchors.top:        parent.top
            anchors.bottom:     parent.bottom
            width:              height
            sourceSize.width:   width
            source:             "/custom/img/FuelIcon.svg"
            fillMode:           Image.PreserveAspectFit
            color:              getFuelColor()
        }

        QGCLabel {
            anchors.verticalCenter: parent.verticalCenter
            text:                   getFuelText()
            font.pointSize:         ScreenTools.mediumFontPointSize
            color:                  getFuelColor()
        }
    }

    MouseArea {
        anchors.fill: parent
        onClicked:    mainWindow.showIndicatorDrawer(indicatorPage, control)
    }

    Component {
        id: indicatorPage

        FuelStatusIndicatorPage {
            waitForParameters: control.waitForParameters
        }
    }
}
