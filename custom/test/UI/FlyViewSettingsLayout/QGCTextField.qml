import QtQuick
import QtQuick.Controls
import QGroundControl.ScreenTools
import QGroundControl.Palette

// Layout boundary double only. Production FactTextField is used on top of it.
TextField {
    property string unitsLabel
    property bool showUnits: false
    property bool showHelp: false
    property bool numericValuesOnly: false
    signal helpClicked()
    function clearValidationError() {}
    function showValidationError(error, value) {}
    implicitWidth: 200
    implicitHeight: ScreenTools.defaultFontPixelHeight * 2
    font.pointSize: ScreenTools.defaultFontPointSize
    font.family: ScreenTools.normalFontFamily
    color: fieldPalette.text
    selectByMouse: true
    rightPadding: units.visible ? units.width + 12 : 8
    QGCPalette { id: fieldPalette; colorGroupEnabled: enabled }
    background: Rectangle {
        radius: 4
        color: fieldPalette.button
        border.color: fieldPalette.buttonBorder
    }
    Label {
        id: units
        anchors.right: parent.right
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        text: unitsLabel
        visible: showUnits && text !== ""
        color: fieldPalette.text
    }
}
