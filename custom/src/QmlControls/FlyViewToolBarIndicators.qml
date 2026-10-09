/****************************************************************************
 *
 * (c) 2009-2020 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

import QtQuick

import QGroundControl
import QGroundControl.ScreenTools
import QGroundControl.Toolbar

//-------------------------------------------------------------------------
//-- Toolbar Indicators
Row {
    id:                 indicatorRow
    anchors.top:        parent.top
    anchors.bottom:     parent.bottom
    anchors.margins:    _toolIndicatorMargins
    spacing:            ScreenTools.defaultFontPixelWidth * 1.75

    property var  _activeVehicle:           QGroundControl.multiVehicleManager.activeVehicle
    property real _toolIndicatorMargins:    ScreenTools.defaultFontPixelHeight * 0.66
    readonly property string _uniRcIndicatorSource: "qrc:/Custom/qml/QGroundControl/Toolbar/UniRcLinkIndicator.qml"

    function vehicleToolIndicators() {
        if (!_activeVehicle) {
            return []
        }
        const nativeIndicators = _activeVehicle.toolIndicators
        if (!QGroundControl.corePlugin || !QGroundControl.corePlugin.uniRcChannelController) {
            return nativeIndicators
        }

        // Preserve each firmware's order and place the air link immediately
        // before its gimbal entry, including custom resource replacements.
        const indicators = []
        let inserted = false
        for (let index = 0; index < nativeIndicators.length; ++index) {
            if (!inserted && String(nativeIndicators[index]).endsWith("/GimbalIndicator.qml")) {
                indicators.push(_uniRcIndicatorSource)
                inserted = true
            }
            indicators.push(nativeIndicators[index])
        }
        // Some firmware (for example ArduSub) has no gimbal indicator.
        if (!inserted) {
            indicators.push(_uniRcIndicatorSource)
        }
        return indicators
    }

    Repeater {
        id:     appRepeater
        model:  QGroundControl.corePlugin.toolBarIndicators
        Loader {
            anchors.top:        parent.top
            anchors.bottom:     parent.bottom
            source:             modelData
            visible:            item.showIndicator
        }
    }

    Repeater {
        id:     toolIndicatorsRepeater
        model:  indicatorRow.vehicleToolIndicators()

        Loader {
            objectName:         String(modelData) === indicatorRow._uniRcIndicatorSource ? "uniRcLinkIndicatorLoader" : ""
            anchors.top:        parent.top
            anchors.bottom:     parent.bottom
            source:             modelData
            visible:            item ? item.showIndicator : false
        }
    }

    Repeater {
        model: _activeVehicle ? _activeVehicle.modeIndicators : []
        Loader {
            anchors.top:        parent.top
            anchors.bottom:     parent.bottom
            source:             modelData
            visible:            item.showIndicator
        }
    }

}
