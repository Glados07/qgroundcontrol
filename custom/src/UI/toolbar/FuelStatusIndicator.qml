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
import QGroundControl.Palette
import QGroundControl.ScreenTools

import Custom.Widgets

// 顶部工具栏在 Fuel 图标右侧显示剩余百分比和剩余燃料。
Item {
    id: control

    anchors.top:    parent.top
    anchors.bottom: parent.bottom
    width:          fuelIndicatorRow.width

    property bool showIndicator:     _hasFuel
    property bool waitForParameters: false

    property var  _activeVehicle: QGroundControl.multiVehicleManager.activeVehicle
    property var  _fuelStatus:    _activeVehicle ? _activeVehicle.fuelStatus : null
    property bool _hasFuel:       !!_fuelStatus && _fuelStatus.telemetryAvailable
    property real _pctRemaining:  _hasFuel ? _fuelStatus.percentRemaining.rawValue : NaN
    readonly property string _fuelUnitSuffix: {
        if (!_hasFuel) return ""

        switch (_fuelStatus.fuelType.rawValue) {
        case 1: return " ml"
        case 2: return " MPa"
        default: return ""
        }
    }

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

    TextMetrics {
        id: fuelPercentMetrics

        font: fuelPercentLabel.font
        text: "-100%"
    }

    Row {
        id: fuelIndicatorRow

        anchors.top:    parent.top
        anchors.bottom: parent.bottom
        visible:        _hasFuel
        spacing:        ScreenTools.defaultFontPixelWidth / 2

        QGCColoredImage {
            anchors.top:        parent.top
            anchors.bottom:     parent.bottom
            width:              height
            sourceSize.width:   width
            source:             "qrc:/custom/img/FuelIcon.svg"
            fillMode:           Image.PreserveAspectFit
            color:              getFuelColor()
        }

        Column {
            anchors.verticalCenter: parent.verticalCenter

            QGCLabel {
                id: fuelPercentLabel

                width:          Math.max(fuelPercentMetrics.width, implicitWidth)
                text:           getFuelText()
                font.pointSize: ScreenTools.mediumFontPointSize
                color:          getFuelColor()
            }

            QGCLabel {
                visible: text.length > 0
                text: {
                    const fuel = control._hasFuel ? control._fuelStatus.remainingFuel : null
                    return fuel && Number.isFinite(fuel.rawValue) ? fuel.valueString + control._fuelUnitSuffix : ""
                }
            }
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
            fuelUnitSuffix:    control._fuelUnitSuffix
        }
    }
}
