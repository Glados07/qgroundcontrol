import QtQuick

QtObject {
    enum Theme { Light, Dark }
    property bool colorGroupEnabled: true
    property int globalTheme: layoutTestStyle.light ? QGCPalette.Light : QGCPalette.Dark
    // Mirror src/QmlControls/QGCPalette.cc so previews use the native frame/background.
    property color window: layoutTestStyle.light ? "#ffffff" : "#222222"
    property color windowShade: layoutTestStyle.light ? "#d9d9d9" : "#333333"
    property color groupBorder: layoutTestStyle.light ? "#bbbbbb" : "#707070"
    property color text: layoutTestStyle.light
                         ? (colorGroupEnabled ? "#000000" : "#9d9d9d")
                         : (colorGroupEnabled ? "#ffffff" : "#707070")
    property color button: layoutTestStyle.light ? "#ffffff" : (colorGroupEnabled ? "#626270" : "#707070")
    property color primaryButton: colorGroupEnabled ? "#8cb3be" : "#585858"
    property color buttonBorder: layoutTestStyle.light
                                 ? (colorGroupEnabled ? "#d9d9d9" : "#ffffff")
                                 : (colorGroupEnabled ? "#adadb8" : "#707070")
    property color buttonText: layoutTestStyle.light
                               ? (colorGroupEnabled ? "#000000" : "#9d9d9d")
                               : (colorGroupEnabled ? "#ffffff" : "#a6a6a6")
    property color buttonHighlight: layoutTestStyle.light
                                    ? (colorGroupEnabled ? "#946120" : "#e4e4e4")
                                    : (colorGroupEnabled ? "#fff291" : "#3a3a3a")
    property color buttonHighlightText: !colorGroupEnabled ? "#2c2c2c" : layoutTestStyle.light ? "#ffffff" : "#000000"
}
