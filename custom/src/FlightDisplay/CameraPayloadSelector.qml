/****************************************************************************
 * Compact, distinct payload pictograms with short identity captions.
 ****************************************************************************/
import QtQuick
import QtQuick.Layouts
import QGroundControl.ScreenTools

Rectangle {
    id: root
    property int selectedCamera: 0
    property bool a8Online: false
    property bool roukeOnline: false
    signal cameraSelected(int cameraIndex)
    readonly property real inset: ScreenTools.defaultFontPixelHeight * 0.22
    implicitWidth: choices.implicitWidth + inset * 2
    implicitHeight: choices.implicitHeight + inset * 2
    radius: ScreenTools.defaultFontPixelHeight * 0.55
    color: "#f2172833"
    border.color: "#355262"
    RowLayout {
        id: choices
        anchors.fill: parent
        anchors.margins: root.inset
        spacing: root.inset
        Repeater {
            model: [0, 1]
            CameraControlButton {
                required property int modelData
                objectName: modelData === 0 ? "selectA8" : "selectRouke"
                implicitHeight: implicitWidth * 1.24
                text: modelData === 0 ? qsTr("A8 Mini") : qsTr("Rouke")
                caption: modelData === 0 ? "A8" : "RK"
                iconName: modelData === 0 ? "payloadA8" : "payloadRouke"
                checked: root.selectedCamera === modelData
                iconColor: checked ? "#9ce9fa" : "#9db2bf"
                onClicked: root.cameraSelected(modelData)
                Rectangle {
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 4
                    width: 4; height: 4; radius: 2
                    color: (parent.modelData === 0 ? root.a8Online : root.roukeOnline) ? "#65d9f4" : "#617583"
                }
            }
        }
    }
}
