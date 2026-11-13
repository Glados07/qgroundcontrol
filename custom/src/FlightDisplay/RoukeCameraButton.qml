/****************************************************************************
 * Shared visual treatment for the Rouke payload settings controls.
 ****************************************************************************/
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QtControls
import QGroundControl.ScreenTools

QtControls.Button {
    id: root
    property bool primary: false
    property bool destructive: false
    property bool compact: false
    property bool subtle: false
    property bool tabStyle: false
    property bool iconOnly: false
    property bool iconAbove: false
    property string iconName: ""
    property string symbol: ""
    readonly property color accent: "#176b83"
    readonly property color foreground: !enabled ? "#8b98a3" : destructive ? "#c73d4d" : "#152733"
    readonly property real iconSize: ScreenTools.defaultFontPixelHeight * (iconOnly ? 1.05 : 0.9)
    implicitHeight: Math.max(ScreenTools.defaultFontPixelHeight * (compact ? 2.1 : 2.45),
                             contentItem.implicitHeight + topPadding + bottomPadding,
                             ScreenTools.isMobile ? ScreenTools.minTouchPixels : 0)
    implicitWidth: iconOnly ? implicitHeight : contentItem.implicitWidth + leftPadding + rightPadding
    leftPadding: ScreenTools.defaultFontPixelWidth * 1.25
    rightPadding: leftPadding
    topPadding: ScreenTools.defaultFontPixelHeight * 0.3
    bottomPadding: topPadding
    hoverEnabled: true
    font.pointSize: ScreenTools.defaultFontPointSize
    font.family: controlFont.status === FontLoader.Ready ? controlFont.name : ScreenTools.normalFontFamily
    font.styleName: ""
    font.weight: Font.DemiBold
    Accessible.name: text
    CameraControlFont { id: controlFont }
    QtControls.ToolTip {
        id: helpTip
        visible: root.hovered && root.iconOnly && root.visible && root.enabled
        text: root.text
        delay: 550
        font.family: ScreenTools.normalFontFamily
        font.pointSize: ScreenTools.smallFontPointSize
        font.weight: Font.Medium
        horizontalPadding: ScreenTools.defaultFontPixelWidth
        verticalPadding: ScreenTools.defaultFontPixelHeight * 0.4
        contentItem: Text {
            text: helpTip.text
            font: helpTip.font
            color: "#152733"
            wrapMode: Text.Wrap
        }
        background: Rectangle {
            radius: ScreenTools.defaultFontPixelHeight * 0.3
            color: "#ffffff"
            border.color: "#cbd5df"
        }
    }
    background: Rectangle {
        radius: root.iconOnly ? height / 2 : ScreenTools.defaultFontPixelHeight * 0.45
        color: !root.enabled ? (root.tabStyle || (root.iconOnly && root.iconName !== "read") ? "#00ffffff" : "#f0f3f5")
               : root.down ? "#bfdde6" : root.tabStyle && root.checked ? "#ffffff"
               : root.hovered ? "#d3e0e8" : root.primary ? "#dfe8ee"
               : root.iconOnly ? (root.iconName === "read" ? "#edf3f6" : "#00ffffff")
               : root.subtle || root.tabStyle ? "#00ffffff" : "#dfe8ee"
        border.width: root.visualFocus ? 2 : (root.tabStyle && root.checked) || (root.primary && root.enabled) ? 1 : 0
        border.color: root.visualFocus ? root.accent : root.tabStyle ? "#b9cbd5" : "#b1ced9"
        Behavior on color { ColorAnimation { duration: 100 } }
        Rectangle {
            visible: root.tabStyle && root.checked
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 3
            width: ScreenTools.defaultFontPixelWidth * 2
            height: 2
            radius: 1
            color: root.accent
        }
    }
    contentItem: GridLayout {
        scale: root.iconOnly && root.down ? 0.94 : 1
        Behavior on scale { NumberAnimation { duration: 90 } }
        columns: root.iconAbove ? 1 : 2
        rowSpacing: ScreenTools.defaultFontPixelHeight * 0.2
        columnSpacing: ScreenTools.defaultFontPixelWidth * 0.75
        CameraControlIcon {
            visible: root.iconName.length > 0
            Layout.preferredWidth: root.iconSize
            Layout.preferredHeight: root.iconSize
            Layout.alignment: Qt.AlignCenter
            kind: root.iconName
            color: root.foreground
        }
        Text {
            visible: !root.iconOnly
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.alignment: Qt.AlignCenter
            text: root.symbol.length ? root.symbol + "  " + root.text : root.text
            color: root.foreground
            font: root.font
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            wrapMode: Text.Wrap
        }
    }
}
