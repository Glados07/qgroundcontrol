/****************************************************************************
 *
 * Reusable horizontal direction compass bar for Fly View.
 *
 ****************************************************************************/

import QtQuick

import QGroundControl
import QGroundControl.Controls
import QGroundControl.Palette
import QGroundControl.ScreenTools

Item {
    id: root

    implicitWidth:  ScreenTools.defaultFontPixelWidth * 50
    implicitHeight: (_headingIndicatorHeight / 2) + _barHeight + (_pointerSize / 2)

    property var vehicle: QGroundControl.multiVehicleManager.activeVehicle
    property real directionDegrees: vehicle ? Number(vehicle.heading.rawValue) : NaN
    property string indicatorPrefix: ""
    property real secondaryDegrees: NaN
    property string secondaryPrefix: ""

    readonly property real _rawHeading: Number(directionDegrees)
    readonly property bool headingValid: isFinite(_rawHeading)
    readonly property real heading: headingValid ? _normalize(_rawHeading) : 0
    readonly property real _barHeight: ScreenTools.defaultFontPixelHeight * 1.5
    readonly property real _headingIndicatorHeight: ScreenTools.defaultFontPixelHeight
    readonly property real _pointerSize: ScreenTools.defaultFontPixelHeight

    function _normalize(degrees) {
        var normalized = degrees % 360
        return normalized < 0 ? normalized + 360 : normalized
    }

    function _directionLabel(degrees) {
        switch (Math.round(_normalize(degrees)) % 360) {
        case 0:   return "N"
        case 45:  return "NE"
        case 90:  return "E"
        case 135: return "SE"
        case 180: return "S"
        case 225: return "SW"
        case 270: return "W"
        case 315: return "NW"
        default:  return ""
        }
    }

    function _indicatorText() {
        var primaryText = headingValid
                ? Math.round(heading) % 360 + "°"
                : "---°"
        if (indicatorPrefix.length > 0) {
            primaryText = indicatorPrefix + " " + primaryText
        }

        var relativeValue = Number(secondaryDegrees)
        if (isFinite(relativeValue)) {
            var roundedRelative = Math.round(relativeValue)
            if (Math.abs(roundedRelative) < 1) {
                roundedRelative = 0
            }
            var relativeText = (roundedRelative > 0 ? "+" : "")
                    + roundedRelative + "°"
            if (secondaryPrefix.length > 0) {
                relativeText = secondaryPrefix + " " + relativeText
            }
            primaryText += "  " + relativeText
        }
        return primaryText
    }

    QGCPalette {
        id: qgcPal
    }

    // custom-example 使用 720 个 Label 模拟滚动。本实现只维护中心附近 11 个
    // 45 度方位标签，在 Android 上显著减少航向更新时的 QML 重算量。
    Rectangle {
        id: compassBar
        anchors.top: parent.top
        anchors.topMargin: root._headingIndicatorHeight / 2
        anchors.left: parent.left
        anchors.right: parent.right
        height: root._barHeight
        color: "#DEDEDE"
        radius: 2
        clip: true

        Repeater {
            model: 11

            delegate: QGCLabel {
                readonly property real _baseAngle: Math.floor(root.heading / 45) * 45
                readonly property real _unwrappedAngle: _baseAngle + (index - 5) * 45

                x: (compassBar.width / 2)
                   + ((_unwrappedAngle - root.heading) * compassBar.width / 360)
                   - (width / 2)
                anchors.verticalCenter: parent.verticalCenter
                visible: (x + width) >= 0 && x <= compassBar.width
                text: root._directionLabel(_unwrappedAngle)
                color: "#505565"
                opacity: 0.85
                font.pointSize: ScreenTools.smallFontPointSize
                font.bold: true
            }
        }
    }

    Rectangle {
        id: headingIndicator
        z: 1
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.min(root.width,
                        Math.max(headingLabel.implicitWidth + ScreenTools.defaultFontPixelWidth,
                                 ScreenTools.defaultFontPixelWidth * 4))
        height: root._headingIndicatorHeight
        radius: 2
        color: qgcPal.windowShadeDark

        QGCLabel {
            id: headingLabel
            anchors.centerIn: parent
            width: Math.max(0, parent.width - ScreenTools.defaultFontPixelWidth)
            text: root._indicatorText()
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
            color: qgcPal.text
            font.pointSize: ScreenTools.smallFontPointSize
            font.bold: true
        }
    }

    QGCColoredImage {
        id: compassArrowIndicator
        z: 1
        anchors.top: compassBar.bottom
        anchors.topMargin: -height / 2
        anchors.horizontalCenter: parent.horizontalCenter
        width: root._pointerSize
        height: width
        source: "qrc:/custom/img/compassPointer.svg"
        sourceSize.height: height
        color: qgcPal.text
        fillMode: Image.PreserveAspectFit
    }

}
