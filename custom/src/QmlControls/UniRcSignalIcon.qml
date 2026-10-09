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

    onAvailableChanged: drawing.requestPaint()
    onLevelChanged: drawing.requestPaint()
    onSignalColorChanged: drawing.requestPaint()
    onInactiveColorChanged: drawing.requestPaint()

    Canvas {
        id: drawing

        anchors.fill: parent
        antialiasing: true
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()

        onPaint: {
            const context = getContext("2d")
            context.reset()
            const scale = Math.min(width, height) / 64
            context.translate((width - 64 * scale) / 2, (height - 64 * scale) / 2)
            context.scale(scale, scale)
            context.lineCap = "round"
            context.lineWidth = 4.5
            // The dot and four arcs represent 20, 40, 60, 80 and 100%.
            // A known zero has an empty red dot; unavailable is neutral gray.
            for (let segment = 1; segment <= 4; ++segment) {
                context.beginPath()
                context.strokeStyle = root.available && root.level > segment
                                      ? root.signalColor : root.inactiveColor
                context.arc(32, 52, 10 * segment, Math.PI * 1.25, Math.PI * 1.75)
                context.stroke()
            }
            context.beginPath()
            context.arc(32, 52, 3.1, 0, Math.PI * 2)
            if (root.available && root.level > 0) {
                context.fillStyle = root.signalColor
                context.fill()
            } else {
                context.lineWidth = 2
                context.strokeStyle = root.signalColor
                context.stroke()
            }
        }
    }
}
