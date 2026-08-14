/****************************************************************************
 *
 * Fly View wrapper for the independent secondary RTSP receiver.
 * Mirrors src/FlightDisplay/FlyViewVideo.qml.
 *
 ****************************************************************************/

import QtQuick

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FlightDisplay
import QGroundControl.ScreenTools

Item {
    id: root

    property Item pipView
    property Item pipState: videoPipState
    property var manager: QGroundControl.corePlugin
                          ? QGroundControl.corePlugin.dualVideoManager : null

    PipState {
        id: videoPipState
        pipView: root.pipView
        isDark: true

        onWindowAboutToOpen: {
            if (root.manager) {
                root.manager.stopVideo()
                videoStartDelay.start()
            }
        }

        onWindowAboutToClose: {
            if (root.manager) {
                root.manager.stopVideo()
                videoStartDelay.start()
            }
        }

        onStateChanged: {
            if (root.manager && state !== fullState) {
                root.manager.fullScreen = false
            }
        }
    }

    Timer {
        id: videoStartDelay
        interval: 2000
        repeat: false
        onTriggered: {
            if (root.manager) {
                root.manager.startVideo()
            }
        }
    }

    FlightDisplayViewSecondaryVideo {
        id: videoStreaming
        anchors.fill: parent
        manager: root.manager
        useSmallFont: root.pipState.state !== root.pipState.fullState
    }

    QGCLabel {
        text: qsTr("Double-click to exit full screen")
        font.pointSize: ScreenTools.largeFontPointSize
        visible: root.manager && root.manager.fullScreen
                 && videoMouseArea.containsMouse
        anchors.centerIn: parent

        onVisibleChanged: {
            if (visible) {
                labelAnimation.start()
            }
        }

        PropertyAnimation on opacity {
            id: labelAnimation
            duration: 10000
            from: 1.0
            to: 0.0
            easing.type: Easing.InExpo
        }
    }

    MouseArea {
        id: videoMouseArea
        anchors.fill: parent
        enabled: root.pipState.state === root.pipState.fullState
        hoverEnabled: true

        onDoubleClicked: {
            if (root.manager) {
                root.manager.fullScreen = !root.manager.fullScreen
            }
        }
    }

    ProximityRadarVideoView {
        anchors.fill: parent
        vehicle: QGroundControl.multiVehicleManager.activeVehicle
    }

    ObstacleDistanceOverlayVideo {
        showText: root.pipState.state === root.pipState.fullState
    }
}
