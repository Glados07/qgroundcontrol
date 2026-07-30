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

import Custom.Widgets

Item {
    id: control

    anchors.top:    parent.top
    anchors.bottom: parent.bottom
    width:          radarIcon.width * 1.1

    readonly property int  _blinkFadeDuration:   400
    readonly property real _blinkMinimumOpacity: 0.25

    property bool showIndicator: radarModel.telemetryAvailable

    QtObject {
        id: radarModel

        readonly property real alertDistanceMeters: 5.0
        readonly property var  activeVehicle:        QGroundControl.multiVehicleManager.activeVehicle
        readonly property var  distanceSensors:      activeVehicle ? activeVehicle.distanceSensors : null
        readonly property var  entries:              distanceSensors ? [
            { fact: distanceSensors.rotationNone,     direction: qsTr("Forward") },
            { fact: distanceSensors.rotationYaw45,    direction: qsTr("Forward/Right") },
            { fact: distanceSensors.rotationYaw90,    direction: qsTr("Right") },
            { fact: distanceSensors.rotationYaw135,   direction: qsTr("Rear/Right") },
            { fact: distanceSensors.rotationYaw180,   direction: qsTr("Rear") },
            { fact: distanceSensors.rotationYaw225,   direction: qsTr("Rear/Left") },
            { fact: distanceSensors.rotationYaw270,   direction: qsTr("Left") },
            { fact: distanceSensors.rotationYaw315,   direction: qsTr("Forward/Left") },
            { fact: distanceSensors.rotationPitch90,  direction: qsTr("Up") },
            { fact: distanceSensors.rotationPitch270, direction: qsTr("Down") }
        ] : []
        readonly property bool telemetryAvailable: _hasTelemetry()
        readonly property bool proximityAlert:     _hasProximityAlert()

        function factAvailable(fact) {
            return fact && !isNaN(fact.value)
        }

        function factInAlert(fact) {
            return factAvailable(fact) && fact.value < alertDistanceMeters
        }

        function _hasTelemetry() {
            for (let i = 0; i < entries.length; i++) {
                if (factAvailable(entries[i].fact)) {
                    return true
                }
            }
            return false
        }

        function _hasProximityAlert() {
            for (let i = 0; i < entries.length; i++) {
                if (factInAlert(entries[i].fact)) {
                    return true
                }
            }
            return false
        }
    }

    QGCColoredImage {
        id: radarIcon

        anchors.top:        parent.top
        anchors.bottom:     parent.bottom
        width:              height
        sourceSize.height:  height
        source:             "/InstrumentValueIcons/radar.svg"
        fillMode:           Image.PreserveAspectFit
        color:              radarModel.proximityAlert ? qgcPal.colorRed : qgcPal.buttonText

        SequentialAnimation on opacity {
            running: radarModel.proximityAlert
            loops:   Animation.Infinite

            NumberAnimation {
                from:     1.0
                to:       control._blinkMinimumOpacity
                duration: control._blinkFadeDuration
            }

            NumberAnimation {
                from:     control._blinkMinimumOpacity
                to:       1.0
                duration: control._blinkFadeDuration
            }

            onRunningChanged: {
                if (!running) {
                    radarIcon.opacity = 1.0
                }
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        enabled:      control.showIndicator
        onClicked:    mainWindow.showIndicatorDrawer(indicatorPage, control)
    }

    Component {
        id: indicatorPage

        ProximityRadarIndicatorPage {
            radarData: radarModel
        }
    }
}
