/****************************************************************************
 *
 * UniRC air-link status details shown in the native toolbar indicator drawer.
 *
 ****************************************************************************/

import QtQuick
import QtQuick.Layouts

import QGroundControl.Controls
import QGroundControl.Palette
import QGroundControl.ScreenTools

ToolIndicatorPage {
    id: root
    objectName: "uniRcLinkIndicatorPage"

    showExpand: false
    waitForParameters: false

    required property var linkStatus
    readonly property bool available: !!linkStatus && linkStatus.available
    readonly property bool lightTheme: linkPalette.globalTheme === QGCPalette.Light
    readonly property string qualityText: !available ? qsTr("Not available")
                                          : linkStatus.qualityLevel === 2 ? qsTr("Strong")
                                          : linkStatus.qualityLevel === 1 ? qsTr("Medium") : qsTr("Weak")
    readonly property color qualityColor: !available ? (lightTheme ? "#737d86" : "#99a5ae")
                                          : linkStatus.qualityLevel === 2 ? (lightTheme ? "#087f73" : "#49d7bd")
                                          : linkStatus.qualityLevel === 1 ? (lightTheme ? "#a66309" : "#efb957")
                                          : (lightTheme ? "#bd3e43" : "#f27e82")

    QGCPalette { id: linkPalette }

    FontMetrics {
        id: captionMetrics
        font.family: ScreenTools.normalFontFamily
        font.pointSize: ScreenTools.defaultFontPointSize * 1.25
    }

    FontMetrics {
        id: valueMetrics
        font.family: ScreenTools.normalFontFamily
        font.pointSize: ScreenTools.defaultFontPointSize * 2.4
        font.bold: true
    }

    contentComponent: Component {
        ColumnLayout {
            id: content

            // Size from actual translated text, not the width of a Latin glyph.
            // Reserve the same outer padding as the native indicator drawer.
            width: Math.min(Math.max(ScreenTools.defaultFontPixelHeight * 20,
                                     naturalColumnWidth * 2 + horizontalOverhead),
                            Math.max(1, mainWindow.width - ScreenTools.defaultFontPixelHeight * 1.5))
            spacing: ScreenTools.defaultFontPixelHeight * 0.8

            readonly property bool compact: mainWindow.width < ScreenTools.defaultFontPixelHeight * 20
            readonly property real sidePadding: ScreenTools.defaultFontPixelHeight * (compact ? 0.35 : 0.6)
            readonly property real columnSpacing: ScreenTools.defaultFontPixelHeight * (compact ? 0.45 : 0.75)
            readonly property real horizontalOverhead: sidePadding * 2 + columnSpacing * 2 + 1
            readonly property real columnWidth: Math.max(1, (width - horizontalOverhead) / 2)
            readonly property real captionTextWidth: Math.max(
                captionMetrics.advanceWidth(qsTr("Signal strength")),
                captionMetrics.advanceWidth(qsTr("Signal quality"))) + 4
            readonly property real valueTextWidth: Math.max(
                valueMetrics.advanceWidth("100%"),
                valueMetrics.advanceWidth(qsTr("Strong")),
                valueMetrics.advanceWidth(qsTr("Medium")),
                valueMetrics.advanceWidth(qsTr("Weak")),
                valueMetrics.advanceWidth(qsTr("Not available"))) + 4
            readonly property real naturalColumnWidth: Math.max(captionTextWidth, valueTextWidth)
                                                       + ScreenTools.defaultFontPixelHeight * 0.6
            readonly property real captionPointSize: captionMetrics.font.pointSize
                                                     * Math.min(1, columnWidth / captionTextWidth)
            // Both values share one fitted size, including unknown status.
            // Independent Text.Fit would shrink 100% but leave the quality larger.
            readonly property real valueScale: Math.min(1, columnWidth / valueTextWidth)
            readonly property real valuePointSize: valueMetrics.font.pointSize * valueScale
            readonly property real valueHeight: Math.ceil(valueMetrics.height * valueScale)
                                                + ScreenTools.defaultFontPixelHeight * 0.4

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: content.sidePadding
                Layout.rightMargin: content.sidePadding
                Layout.topMargin: ScreenTools.defaultFontPixelHeight * 0.4
                spacing: ScreenTools.defaultFontPixelWidth

                UniRcSignalIcon {
                    id: strengthIcon

                    Layout.preferredWidth: ScreenTools.defaultFontPixelHeight * 1.6
                    Layout.preferredHeight: Layout.preferredWidth
                    available: root.available
                    strengthLevel: root.available ? root.linkStatus.strengthLevel : 0
                }

                QGCLabel {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    text: qsTr("Air Link")
                    font.pointSize: ScreenTools.defaultFontPointSize * 1.3
                    font.bold: true
                    wrapMode: Text.Wrap
                }
            }

            GridLayout {
                Layout.fillWidth: true
                Layout.leftMargin: content.sidePadding
                Layout.rightMargin: content.sidePadding
                Layout.bottomMargin: ScreenTools.defaultFontPixelHeight * 0.6
                columns: 3
                columnSpacing: content.columnSpacing
                rowSpacing: ScreenTools.defaultFontPixelHeight * 0.3

                QGCLabel {
                    objectName: "uniRcDetailStrengthCaption"
                    Layout.row: 0
                    Layout.column: 0
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.preferredWidth: 1
                    text: qsTr("Signal strength")
                    font.pointSize: content.captionPointSize
                    opacity: 0.85
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.NoWrap
                }

                Rectangle {
                    Layout.row: 0
                    Layout.column: 1
                    Layout.rowSpan: 2
                    Layout.preferredWidth: 1
                    Layout.fillHeight: true
                    Layout.topMargin: ScreenTools.defaultFontPixelHeight * 0.2
                    Layout.bottomMargin: ScreenTools.defaultFontPixelHeight * 0.2
                    color: linkPalette.groupBorder
                    opacity: 0.4
                }

                QGCLabel {
                    objectName: "uniRcDetailQualityCaption"
                    Layout.row: 0
                    Layout.column: 2
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.preferredWidth: 1
                    text: qsTr("Signal quality")
                    font.pointSize: content.captionPointSize
                    opacity: 0.85
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.NoWrap
                }

                QGCLabel {
                    objectName: "uniRcDetailStrength"
                    Layout.row: 1
                    Layout.column: 0
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.preferredWidth: 1
                    Layout.preferredHeight: content.valueHeight
                    text: root.available ? root.linkStatus.strengthPercent + "%" : qsTr("Not available")
                    color: strengthIcon.signalColor
                    font.pointSize: content.valuePointSize
                    font.bold: true
                    wrapMode: Text.NoWrap
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }

                QGCLabel {
                    objectName: "uniRcDetailQuality"
                    Layout.row: 1
                    Layout.column: 2
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.preferredWidth: 1
                    Layout.preferredHeight: content.valueHeight
                    text: root.qualityText
                    color: root.qualityColor
                    font.pointSize: content.valuePointSize
                    font.bold: true
                    wrapMode: Text.NoWrap
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }
}
