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
import QGroundControl.ScreenTools

ToolIndicatorPage {
    id: root

    showExpand: false

    required property var radarData

    contentComponent: Component {
        SettingsGroupLayout {
            heading:       qsTr("Proximity Radar")
            showDividers:  false
            contentSpacing: 0

            Repeater {
                model: root.radarData.entries

                delegate: RowLayout {
                    id: radarRow

                    required property var modelData

                    readonly property var   fact:           modelData ? modelData.fact : null
                    readonly property bool  proximityAlert: root.radarData.factInAlert(fact)
                    readonly property color textColor:       proximityAlert ? QGroundControl.globalPalette.colorRed : QGroundControl.globalPalette.text

                    visible: root.radarData.factAvailable(fact)
                    spacing: ScreenTools.defaultFontPixelWidth * 2

                    QGCLabel {
                        Layout.fillWidth: true
                        text:             radarRow.modelData ? qsTr("%1 Radar").arg(radarRow.modelData.direction) : ""
                        color:            radarRow.textColor
                    }

                    QGCLabel {
                        text:  radarRow.fact ? radarRow.fact.valueString + " " + radarRow.fact.units : ""
                        color: radarRow.textColor
                    }
                }
            }
        }
    }
}
