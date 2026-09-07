pragma Singleton
import QtQuick

QtObject {
    property real defaultFontPixelWidth: 8 * layoutTestStyle.scale
    property real defaultFontPixelHeight: 20 * layoutTestStyle.scale
    property real defaultFontPointSize: 11 * layoutTestStyle.scale
    property real smallFontPointSize: 10 * layoutTestStyle.scale
    property string normalFontFamily: "Microsoft YaHei"
    property string fixedFontFamily: "Consolas"
    property bool isMobile: false
    property real realPixelDensity: 4
    property real comboBoxPadding: 6
    property real buttonBorderRadius: 4
}
