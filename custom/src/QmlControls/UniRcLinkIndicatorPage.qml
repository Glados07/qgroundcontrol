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

    contentComponent: Component {
        ColumnLayout {
            id: content

            width: Math.min(ScreenTools.defaultFontPixelWidth * 36,
                            Math.max(1, mainWindow.width - ScreenTools.defaultFontPixelWidth * 6))
            spacing: ScreenTools.defaultFontPixelHeight * 0.8

            readonly property real sidePadding: ScreenTools.defaultFontPixelWidth
            readonly property real valueHeight: ScreenTools.defaultFontPixelHeight * 2.2

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
                columnSpacing: ScreenTools.defaultFontPixelWidth * 2
                rowSpacing: ScreenTools.defaultFontPixelHeight * 0.3

                QGCLabel {
                    Layout.row: 0
                    Layout.column: 0
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.preferredWidth: 1
                    text: qsTr("Signal strength")
                    font.pointSize: ScreenTools.smallFontPointSize
                    opacity: 0.7
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
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
                    Layout.row: 0
                    Layout.column: 2
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.preferredWidth: 1
                    text: qsTr("Signal quality")
                    font.pointSize: ScreenTools.smallFontPointSize
                    opacity: 0.7
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
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
                    font.pointSize: ScreenTools.defaultFontPointSize * (root.available ? 2 : 1.15)
                    font.bold: true
                    fontSizeMode: Text.Fit
                    minimumPointSize: ScreenTools.defaultFontPointSize
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
                    font.pointSize: ScreenTools.defaultFontPointSize * (root.available ? 2 : 1.15)
                    font.bold: true
                    fontSizeMode: Text.Fit
                    minimumPointSize: ScreenTools.defaultFontPointSize
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }
}
