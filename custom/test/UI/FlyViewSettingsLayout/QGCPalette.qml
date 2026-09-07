import QtQuick

QtObject {
    enum Theme { Light, Dark }
    property bool colorGroupEnabled: true
    property int globalTheme: layoutTestStyle.light ? QGCPalette.Light : QGCPalette.Dark
    property color window: layoutTestStyle.light ? "#f4f6f8" : "#151a21"
    property color windowShade: layoutTestStyle.light ? "#ffffff" : "#202731"
    property color groupBorder: layoutTestStyle.light ? "#dce2e9" : "#36414e"
    property color text: !colorGroupEnabled ? "#818b99" : layoutTestStyle.light ? "#202b39" : "#e4eaf1"
    property color button: layoutTestStyle.light ? "#e9eef4" : "#303b49"
    property color primaryButton: "#438be4"
    property color buttonBorder: groupBorder
    property color buttonText: text
    property color buttonHighlight: primaryButton
    property color buttonHighlightText: "#ffffff"
}
