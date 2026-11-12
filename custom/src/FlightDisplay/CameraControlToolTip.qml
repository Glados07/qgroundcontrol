/****************************************************************************
 * Shared light tooltip for camera rails, independent of the application theme.
 ****************************************************************************/
import QtQuick
import QtQuick.Controls as QtControls
import QGroundControl.ScreenTools

QtControls.ToolTip {
    id: root
    delay: 450
    width: Math.min(implicitWidth, ScreenTools.defaultFontPixelWidth * 36)
    padding: ScreenTools.defaultFontPixelHeight * 0.5
    font.family: ScreenTools.normalFontFamily
    font.pointSize: ScreenTools.smallFontPointSize
    font.weight: Font.Medium
    background: Rectangle {
        color: "#ffffff"
        radius: ScreenTools.defaultFontPixelHeight * 0.35
        border.color: "#cbd5df"
    }
    contentItem: Text {
        text: root.text
        color: "#152733"
        font: root.font
        wrapMode: Text.Wrap
    }
}
