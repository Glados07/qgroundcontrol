/****************************************************************************
 * Centred SD availability light. Only confirmed presence is shown in green.
 ****************************************************************************/
import QtQuick
import QtQuick.Controls as QtControls
import QGroundControl.ScreenTools

Item {
    id: root
    property bool known: false
    property bool inserted: false
    readonly property bool available: known && inserted
    readonly property color statusColor: available ? "#23835f" : "#c73d4d"
    readonly property string statusText: !known ? qsTr("SD card status unknown")
                                               : inserted ? qsTr("SD card inserted") : qsTr("SD card not inserted")
    implicitWidth: contents.implicitWidth
    implicitHeight: Math.max(ScreenTools.defaultFontPixelHeight * 1.25, contents.implicitHeight)
    Accessible.role: Accessible.StaticText
    Accessible.name: statusText

    Row {
        id: contents
        anchors.centerIn: parent
        anchors.alignWhenCentered: false
        spacing: ScreenTools.defaultFontPixelHeight * 0.28
        Rectangle {
            objectName: "sdStatusDot"
            anchors.verticalCenter: parent.verticalCenter
            width: ScreenTools.defaultFontPixelHeight * 0.3
            height: width
            radius: width / 2
            color: root.statusColor
            Behavior on color { ColorAnimation { duration: 100 } }
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: "SD"
            font.family: ScreenTools.normalFontFamily
            font.pointSize: ScreenTools.smallFontPointSize * 0.85
            font.weight: Font.DemiBold
            color: "#3d5262"
        }
    }
    CameraControlToolTip {
        visible: helpMouse.containsMouse || helpMouse.pressed
        text: statusText
    }
    MouseArea {
        id: helpMouse
        anchors.fill: parent
        hoverEnabled: true
    }
}
