import QtQuick

import QGroundControl.Controls
import QGroundControl.ScreenTools

Item {
    id: unavailableRoot

    property var viewer3DManager: null
    property bool isViewer3DOpen: false

    Rectangle {
        anchors.fill: parent
        color: "#20242a"

        Column {
            anchors.centerIn: parent
            width: Math.min(parent.width * 0.78, ScreenTools.defaultFontPixelWidth * 66)
            spacing: ScreenTools.defaultFontPixelHeight

            QGCLabel {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                color: "white"
                text: qsTr("Google 3D Maps is enabled, but this build was not compiled with Qt WebEngine.")
            }

            QGCLabel {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                color: "#cfd6df"
                text: qsTr("Install the Qt WebEngine component for the selected Qt kit, rerun CMake, and rebuild QGroundControl. If Google 3D Maps is disabled, Viewer3D will keep using the local OSM 3D Map File path.")
            }
        }
    }
}