/****************************************************************************
 *
 * UniPod MT11 camera control wrapper.
 * Reuses the A8 Mini control panel and injects the MT11 manager while adding
 * the SDK-acknowledged zoom, thermal and combined-stream mode selector.
 *
 ****************************************************************************/

import QtQuick

import QGroundControl

Item {
    id: root

    property var manager: QGroundControl.corePlugin ? QGroundControl.corePlugin.mt11ControlManager : null

    implicitWidth: controlLoader.item ? controlLoader.item.implicitWidth : 0
    implicitHeight: controlLoader.item ? controlLoader.item.implicitHeight : 0
    width: implicitWidth
    height: implicitHeight

    function closeTransientUi() {
        if (controlLoader.item
                && typeof controlLoader.item.closeTransientUi === "function") {
            controlLoader.item.closeTransientUi()
        }
    }

    Component.onDestruction: closeTransientUi()

    Loader {
        id: controlLoader

        anchors.fill: parent
        source: "qrc:/Custom/qml/QGroundControl/FlightDisplay/GimbalCameraControl.qml"

        onLoaded: {
            item.manager = Qt.binding(function() { return root.manager })
            item.thermalControlsVisible = true
            item.showActualZoom = true
        }

        onStatusChanged: {
            if (status === Loader.Error) {
                console.warn("MT11 camera control failed to load:", source)
            }
        }
    }
}
