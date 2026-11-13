/****************************************************************************
 * Icon-first flight control; text remains available to assistive technology
 * and in a tooltip. Camera settings use their own labelled field controls.
 ****************************************************************************/
import QtQuick
import QtQuick.Window
import QtQuick.Controls as QtControls
import QGroundControl.ScreenTools

QtControls.Button {
    id: root
    property string iconName: "camera"
    property bool recording: false
    property bool muted: false
    property bool menuIndicator: false
    property string caption: ""
    property color iconColor: recording ? "#c73d4d" : "#152733"
    readonly property bool mutedAppearance: !enabled || (muted && !recording)
    readonly property color foreground: mutedAppearance ? "#8795a3"
                                       : down ? "#152733" : recording ? iconColor
                                       : checked || hovered ? "#176b83" : iconColor
    implicitWidth: Math.max(ScreenTools.defaultFontPixelHeight * 2.25,
                             ScreenTools.isMobile ? ScreenTools.minTouchPixels : 0)
    implicitHeight: implicitWidth
    padding: 0
    hoverEnabled: true
    Accessible.name: text
    CameraControlToolTip {
        visible: root.hovered && !root.down
        delay: 450
        text: root.text
        width: Math.min(implicitWidth, ScreenTools.defaultFontPixelWidth * 36,
                        root.Window.window ? root.Window.window.width - 24 : 400)
    }
    background: Rectangle {
        radius: ScreenTools.defaultFontPixelHeight * 0.4
        color: root.down ? (root.mutedAppearance ? "#e9eff4" : "#c5e1e9") : "transparent"
        border.color: root.visualFocus ? "#176b83" : "transparent"
        border.width: 1
        Behavior on color { ColorAnimation { duration: 100 } }
    }
    contentItem: Item {
        scale: root.down ? 0.94 : 1
        Behavior on scale { NumberAnimation { duration: 90 } }
        CameraControlIcon {
            anchors.horizontalCenter: parent.horizontalCenter
            y: root.caption.length ? (Math.min(parent.height, root.implicitWidth) - height) / 2
                                   : (parent.height - height) / 2
            width: ScreenTools.defaultFontPixelHeight * 1.2
            height: width
            kind: root.iconName
            color: root.foreground
            Behavior on color { ColorAnimation { duration: 100 } }
        }
        Text {
            visible: root.caption.length > 0
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            text: root.caption
            font.family: ScreenTools.normalFontFamily
            font.pointSize: ScreenTools.smallFontPointSize * 0.8
            font.weight: Font.DemiBold
            color: root.mutedAppearance ? "#8795a3" : root.down ? "#152733" : root.checked ? "#176b83" : "#3d5262"
        }
        Rectangle {
            visible: root.menuIndicator
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 4
            width: 3; height: 3; radius: 1.5
            color: root.mutedAppearance ? "#8795a3" : "#176b83"
        }
    }
}
