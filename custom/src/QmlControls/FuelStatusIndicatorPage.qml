/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.MultiVehicleManager
import QGroundControl.ScreenTools

//-------------------------------------------------------------------------
//-- Fuel Status Indicator Page
ToolIndicatorPage {
    showExpand: false

    property var  _activeVehicle: QGroundControl.multiVehicleManager.activeVehicle
    property var  _fuelStatus:    _activeVehicle ? _activeVehicle.fuelStatus : null
    property bool _hasFuel:       _fuelStatus && _fuelStatus.telemetryAvailable
    property real _pctRemaining:  _hasFuel ? _fuelStatus.percentRemaining.rawValue : NaN
    property int  _fuelType:      _hasFuel ? _fuelStatus.fuelType.rawValue : 0

    function getFuelUnit() {
        // fuelType: 0=Unknown, 1=Liquid, 2=Gas
        if (_fuelType === 1) {
            return "ml"
        } else if (_fuelType === 2) {
            return "MPa"
        }
        return ""
    }

    contentComponent: Component {
        ColumnLayout {
            spacing: ScreenTools.defaultFontPixelHeight / 2

            SettingsGroupLayout {
                heading:        qsTr("Fuel Status")
                contentSpacing: 0
                showDividers:   false

                LabelledLabel {
                    label:     qsTr("Remaining")
                    labelText: !isNaN(_pctRemaining) ? Math.round(_pctRemaining) + " %" : qsTr("n/a")
                }

                LabelledLabel {
                    label:     qsTr("Remaining Fuel")
                    labelText: _hasFuel && !isNaN(_fuelStatus.remainingFuel.rawValue) ? (_fuelStatus.remainingFuel.valueString + (getFuelUnit() !== "" ? " " + getFuelUnit() : "")) : qsTr("n/a")
                    visible:   _hasFuel && !isNaN(_fuelStatus.remainingFuel.rawValue)
                }

                LabelledLabel {
                    label:     qsTr("Maximum Fuel")
                    labelText: _hasFuel && !isNaN(_fuelStatus.maximumFuel.rawValue) ? (_fuelStatus.maximumFuel.valueString + (getFuelUnit() !== "" ? " " + getFuelUnit() : "")) : qsTr("n/a")
                    visible:   _hasFuel && !isNaN(_fuelStatus.maximumFuel.rawValue)
                }

                LabelledLabel {
                    label:     qsTr("Consumed Fuel")
                    labelText: _hasFuel && !isNaN(_fuelStatus.consumedFuel.rawValue) ? (_fuelStatus.consumedFuel.valueString + (getFuelUnit() !== "" ? " " + getFuelUnit() : "")) : qsTr("n/a")
                    visible:   _hasFuel && !isNaN(_fuelStatus.consumedFuel.rawValue)
                }

                LabelledLabel {
                    label:     qsTr("Flow Rate")
                    labelText: _hasFuel && !isNaN(_fuelStatus.flowRate.rawValue) ? _fuelStatus.flowRate.valueString : qsTr("n/a")
                    visible:   _hasFuel && !isNaN(_fuelStatus.flowRate.rawValue)
                }

                LabelledLabel {
                    label:     qsTr("Temperature")
                    labelText: _hasFuel && !isNaN(_fuelStatus.temperature.rawValue) ? (_fuelStatus.temperature.valueString + " " + _fuelStatus.temperature.units) : qsTr("n/a")
                    visible:   _hasFuel && !isNaN(_fuelStatus.temperature.rawValue)
                }
            }
        }
    }
}
