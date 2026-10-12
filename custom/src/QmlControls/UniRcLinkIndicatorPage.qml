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
    readonly property real detailScale: 2 / 3
    readonly property real detailFontPointSize: ScreenTools.defaultFontPointSize * detailScale
    readonly property real detailFontHeight: ScreenTools.defaultFontPixelHeight * detailScale
    readonly property real detailFontWidth: ScreenTools.defaultFontPixelWidth * detailScale
    readonly property bool available: !!linkStatus && linkStatus.available
    readonly property bool lightTheme: linkPalette.globalTheme === QGCPalette.Light
    readonly property string qualityTier: !available ? qsTr("Not available")
                                          : linkStatus.qualityLevel === 2 ? qsTr("Strong")
                                          : linkStatus.qualityLevel === 1 ? qsTr("Medium") : qsTr("Weak")
    readonly property string qualityText: available
                                          ? formatQuality(qualityTier, linkStatus.signalQuality)
                                          : qsTr("Not available")
    readonly property color qualityColor: !available ? (lightTheme ? "#737d86" : "#99a5ae")
                                          : linkStatus.qualityLevel === 2 ? (lightTheme ? "#087f73" : "#49d7bd")
                                          : linkStatus.qualityLevel === 1 ? (lightTheme ? "#a66309" : "#efb957")
                                          : (lightTheme ? "#bd3e43" : "#f27e82")

    QGCPalette { id: linkPalette }

    function formatQuality(tier, value) {
        return qsTr("%1 (%2)").arg(tier).arg(value)
    }

    function maximumQualityWidth() {
        // Reserve every signed value so live updates cannot resize the drawer.
        let maximum = 0
        const tiers = [qsTr("Strong"), qsTr("Medium"), qsTr("Weak")]
        for (const tier of tiers) {
            for (let value = -15; value <= 30; ++value) {
                maximum = Math.max(maximum, valueMetrics.advanceWidth(formatQuality(tier, value)))
            }
        }
        return maximum
    }

    FontMetrics {
        id: captionMetrics
        font.family: ScreenTools.normalFontFamily
        font.pointSize: root.detailFontPointSize * 1.05
    }

    FontMetrics {
        id: valueMetrics
        font.family: ScreenTools.normalFontFamily
        font.pointSize: root.detailFontPointSize * 1.65
        font.bold: true
    }

    contentComponent: Component {
        ColumnLayout {
            id: content

            // Size from actual translated text, not the width of a Latin glyph.
            // Reserve the same outer padding as the native indicator drawer.
            readonly property real preferredContentWidth: Math.min(
                Math.max(root.detailFontHeight * 13.5,
                         naturalColumnWidth * 2 + horizontalOverhead),
                Math.max(1, mainWindow.width - ScreenTools.defaultFontPixelHeight * 1.5))
            spacing: root.detailFontHeight * 0.4

            readonly property bool compact: mainWindow.width < root.detailFontHeight * 13.5
            readonly property real sidePadding: root.detailFontHeight * (compact ? 0.25 : 0.35)
            readonly property real columnSpacing: root.detailFontHeight * (compact ? 0.3 : 0.4)
            readonly property real horizontalOverhead: sidePadding * 2 + columnSpacing * 2 + 1
            readonly property real columnWidth: Math.max(1, (width - horizontalOverhead) / 2)
            readonly property real captionTextWidth: Math.max(
                captionMetrics.advanceWidth(qsTr("Signal strength")),
                captionMetrics.advanceWidth(qsTr("Signal quality"))) + 4 * root.detailScale
            readonly property real valueTextWidth: Math.max(
                valueMetrics.advanceWidth("100%"),
                root.maximumQualityWidth(),
                valueMetrics.advanceWidth(qsTr("Not available"))) + 4 * root.detailScale
            readonly property real naturalColumnWidth: Math.max(captionTextWidth, valueTextWidth)
                                                       + root.detailFontHeight * 0.25
            // Keep captions smaller when narrow screens also scale the values.
            readonly property real captionPointSize: Math.min(
                captionMetrics.font.pointSize * Math.min(1, columnWidth / captionTextWidth),
                valuePointSize * 0.8)
            // Both values share one fitted size, including unknown status.
            // Independent Text.Fit would shrink 100% but leave the quality larger.
            readonly property real valueScale: Math.min(1, columnWidth / valueTextWidth)
            readonly property real valuePointSize: valueMetrics.font.pointSize * valueScale
            readonly property real valueHeight: Math.ceil(valueMetrics.height * valueScale)
                                                + root.detailFontHeight * 0.2

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: content.sidePadding
                Layout.rightMargin: content.sidePadding
                Layout.topMargin: root.detailFontHeight * 0.2
                spacing: root.detailFontWidth

                UniRcSignalIcon {
                    id: strengthIcon

                    Layout.preferredWidth: root.detailFontHeight * 1.2
                    Layout.preferredHeight: Layout.preferredWidth
                    available: root.available
                    strengthLevel: root.available ? root.linkStatus.strengthLevel : 0
                }

                QGCLabel {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    text: qsTr("Air Link")
                    font.pointSize: root.detailFontPointSize * 1.05
                    font.bold: true
                    wrapMode: Text.Wrap
                }
            }

            GridLayout {
                // Feed the natural width into layout negotiation so the parent
                // Loader cannot squeeze the first-open page down to its header.
                Layout.preferredWidth: content.preferredContentWidth - content.sidePadding * 2
                Layout.fillWidth: true
                Layout.leftMargin: content.sidePadding
                Layout.rightMargin: content.sidePadding
                Layout.bottomMargin: root.detailFontHeight * 0.3
                columns: 3
                columnSpacing: content.columnSpacing
                rowSpacing: root.detailFontHeight * 0.2

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
                    Layout.topMargin: root.detailFontHeight * 0.2
                    Layout.bottomMargin: root.detailFontHeight * 0.2
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
                    id: strengthValue
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
                    transform: Translate {
                        x: strengthInk.advanceWidth / 2
                           - strengthInk.tightBoundingRect.x - strengthInk.tightBoundingRect.width / 2
                    }
                    TextMetrics {
                        id: strengthInk
                        font: strengthValue.font
                        text: strengthValue.text
                    }
                }

                QGCLabel {
                    id: qualityValue
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
                    // Center the visible glyphs, including the whole quality
                    // value, rather than the full-width bracket's trailing space.
                    transform: Translate {
                        x: qualityInk.advanceWidth / 2
                           - qualityInk.tightBoundingRect.x - qualityInk.tightBoundingRect.width / 2
                    }
                    TextMetrics {
                        id: qualityInk
                        font: qualityValue.font
                        text: qualityValue.text
                    }
                }
            }
        }
    }
}
