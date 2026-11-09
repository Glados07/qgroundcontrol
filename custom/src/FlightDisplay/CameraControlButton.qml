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
    property color iconColor: recording ? "#ff6c80" : "#dcecf4"
    readonly property bool mutedAppearance: !enabled || (muted && !recording)
    readonly property color foreground: mutedAppearance ? "#7d8e99"
                                       : down ? "#163744" : recording ? iconColor
                                       : checked || hovered ? "#a8eaf7" : iconColor
    implicitWidth: Math.max(ScreenTools.defaultFontPixelHeight * 2.25,
                             ScreenTools.isMobile ? ScreenTools.minTouchPixels : 0)
    implicitHeight: implicitWidth
    padding: 0
    hoverEnabled: true
    opacity: enabled ? (mutedAppearance ? 0.55 : 1) : 0.32
    Accessible.name: text
    QtControls.ToolTip {
        visible: root.hovered && !root.down
        delay: 450
        text: root.text
        width: Math.min(implicitWidth, ScreenTools.defaultFontPixelWidth * 36,
                        root.Window.window ? root.Window.window.width - 24 : 400)
        padding: ScreenTools.defaultFontPixelHeight * 0.5
        background: Rectangle { color: "#172833"; radius: 8; border.color: "#476878" }
        contentItem: Text {
            text: root.text
            color: "#e1eef5"
            font.family: ScreenTools.normalFontFamily
            font.pointSize: ScreenTools.smallFontPointSize
            wrapMode: Text.Wrap
        }
    }
    background: Rectangle {
        radius: ScreenTools.defaultFontPixelHeight * 0.4
        color: root.down ? (root.mutedAppearance ? "#263b47" : "#b8e7f2") : "transparent"
        border.color: root.visualFocus ? "#c0f5ff" : "transparent"
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
            color: root.mutedAppearance ? "#7d8e99" : root.down ? "#163744" : root.checked ? "#83e1f6" : "#b4c9d4"
        }
        Rectangle {
            visible: root.menuIndicator
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 4
            width: 3; height: 3; radius: 1.5
            color: root.mutedAppearance ? "#7d8e99" : "#65d9f4"
        }
    }
}
