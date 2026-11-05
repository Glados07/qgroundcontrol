/****************************************************************************
 *
 * Five-segment UniRC air-link signal icon, shared by toolbar and details.
 *
 ****************************************************************************/

import QtQuick

import QGroundControl.Palette

Item {
    id: root

    property bool available: false
    property int strengthLevel: 0
    readonly property int level: Math.max(0, Math.min(5, strengthLevel))
    readonly property bool lightTheme: linkPalette.globalTheme === QGCPalette.Light
    readonly property color signalColor: !available ? (lightTheme ? "#737d86" : "#99a5ae")
                                         : level >= 4 ? (lightTheme ? "#087f73" : "#49d7bd")
                                         : level >= 2 ? (lightTheme ? "#a66309" : "#efb957")
                                         : (lightTheme ? "#bd3e43" : "#f27e82")
    readonly property color inactiveColor: lightTheme ? "#c8d0d4" : "#56626a"

    QGCPalette { id: linkPalette }

    Item {
        id: glyph

        anchors.centerIn: parent
        width: Math.min(parent.width, parent.height)
        height: width

        Repeater {
            model: 5

            Rectangle {
                readonly property bool filled: root.available && index < root.level

                // Fixed baseline, increasing heights and a half-bar gap keep
                // all five levels distinct even at toolbar icon sizes.
                x: glyph.width * (4 + index * 12) / 64
                anchors.bottom: parent.bottom
                anchors.bottomMargin: glyph.height / 8
                width: glyph.width / 8
                height: glyph.height * (12 + index * 9) / 64
                radius: glyph.width / 48
                antialiasing: true
                color: filled ? root.signalColor : "transparent"
                border.width: Math.max(1, glyph.width / 48)
                // Red empty bars mean a confirmed zero; gray empty bars mean
                // unavailable data. Neither state lights a strength segment.
                border.color: filled || !root.available || root.level === 0
                              ? root.signalColor : root.inactiveColor
            }
        }
    }
}
