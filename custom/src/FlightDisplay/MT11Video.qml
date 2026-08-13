/****************************************************************************
 *
 * Fly View video surface for the independent UniPod MT11 RTSP stream.
 *
 ****************************************************************************/

import QtQuick
import QtQuick.Window

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FlightDisplay
import QGroundControl.ScreenTools

Item {
    id: _root

    property Item pipView
    property Item pipState: mt11PipState
    property var manager: QGroundControl.corePlugin
                          ? QGroundControl.corePlugin.dualVideoManager : null

    readonly property bool _useSmallFont: pipState.state
                                               !== pipState.fullState
    readonly property bool _showGrid: QGroundControl.settingsManager
                                          .videoSettings.gridLines.rawValue
    readonly property int _fitMode: QGroundControl.settingsManager
                                        .videoSettings.videoFit.rawValue
    readonly property bool _fitWidth: _fitMode === 0
    readonly property bool _fitHeight: _fitMode === 1
    readonly property bool _fill: _fitMode === 2
    readonly property bool _noCrop: _fitMode === 3
    readonly property real _aspectRatio: manager && manager.aspectRatio > 0
                                         ? manager.aspectRatio : (16 / 9)

    function _initializeVideo() {
        if (manager && _root.Window.window) {
            manager.init(_root.Window.window)
        }
    }

    Component.onCompleted: Qt.callLater(_initializeVideo)
    onManagerChanged: Qt.callLater(_initializeVideo)

    PipState {
        id: mt11PipState
        pipView: _root.pipView
        isDark: true

        onWindowAboutToOpen: {
            if (_root.manager) {
                _root.manager.stopVideo()
                videoStartDelay.restart()
            }
        }

        onWindowAboutToClose: {
            if (_root.manager) {
                _root.manager.stopVideo()
                videoStartDelay.restart()
            }
        }

        onStateChanged: {
            if (_root.manager && state !== fullState) {
                _root.manager.fullScreen = false
            }
        }
    }

    Timer {
        id: videoStartDelay
        interval: 2000
        repeat: false
        onTriggered: {
            if (_root.manager) {
                _root.manager.startVideo()
            }
        }
    }

    Image {
        id: noVideo
        anchors.fill: parent
        source: "/res/NoVideoBackground.jpg"
        fillMode: Image.PreserveAspectCrop
        visible: !_root.manager || !_root.manager.decoding

        Rectangle {
            anchors.centerIn: parent
            width: noVideoLabel.contentWidth + ScreenTools.defaultFontPixelHeight
            height: noVideoLabel.contentHeight
                    + ScreenTools.defaultFontPixelHeight
            radius: ScreenTools.defaultFontPixelWidth / 2
            color: "black"
            opacity: 0.5
        }

        QGCLabel {
            id: noVideoLabel
            text: _root.manager && _root.manager.hasVideo
                  ? qsTr("WAITING FOR VIDEO") : qsTr("VIDEO DISABLED")
            font.bold: true
            color: "white"
            font.pointSize: _root._useSmallFont
                            ? ScreenTools.smallFontPointSize
                            : ScreenTools.largeFontPointSize
            anchors.centerIn: parent
        }
    }

    Item {
        id: videoBackground
        anchors.fill: parent
        visible: _root.manager && (_root.manager.hasVideo
                                   || _root.manager.initialized)

        Rectangle {
            anchors.fill: parent
            color: "black"
        }

        function videoWidth() {
            if (_root._aspectRatio > 0) {
                if (_root._fitHeight
                        || (_root._fill
                            && (width / height < _root._aspectRatio))
                        || (_root._noCrop
                            && (width / height > _root._aspectRatio))) {
                    return height * _root._aspectRatio
                }
            }
            return width
        }

        function videoHeight() {
            if (_root._aspectRatio > 0) {
                if (_root._fitWidth
                        || (_root._fill
                            && (width / height > _root._aspectRatio))
                        || (_root._noCrop
                            && (width / height < _root._aspectRatio))) {
                    return width / _root._aspectRatio
                }
            }
            return height
        }

        Component {
            id: videoBackgroundComponent

            QGCVideoBackground {
                id: videoContent
                objectName: "mt11VideoContent"

                Rectangle {
                    color: Qt.rgba(1, 1, 1, 0.5)
                    height: parent.height
                    width: 1
                    x: parent.width * 0.33
                    visible: _root._showGrid
                             && (!_root.manager || !_root.manager.fullScreen)
                }

                Rectangle {
                    color: Qt.rgba(1, 1, 1, 0.5)
                    height: parent.height
                    width: 1
                    x: parent.width * 0.66
                    visible: _root._showGrid
                             && (!_root.manager || !_root.manager.fullScreen)
                }

                Rectangle {
                    color: Qt.rgba(1, 1, 1, 0.5)
                    width: parent.width
                    height: 1
                    y: parent.height * 0.33
                    visible: _root._showGrid
                             && (!_root.manager || !_root.manager.fullScreen)
                }

                Rectangle {
                    color: Qt.rgba(1, 1, 1, 0.5)
                    width: parent.width
                    height: 1
                    y: parent.height * 0.66
                    visible: _root._showGrid
                             && (!_root.manager || !_root.manager.fullScreen)
                }
            }
        }

        Loader {
            id: videoLoader
            width: videoBackground.videoWidth()
            height: videoBackground.videoHeight()
            anchors.centerIn: parent
            visible: _root.manager && _root.manager.decoding
            active: _root.manager
                    && (_root.manager.hasVideo || _root.manager.initialized)
            sourceComponent: videoBackgroundComponent

            onLoaded: Qt.callLater(_root._initializeVideo)
        }
    }

    QGCLabel {
        text: qsTr("Double-click to exit full screen")
        font.pointSize: ScreenTools.largeFontPointSize
        visible: _root.manager && _root.manager.fullScreen
                 && mt11VideoMouseArea.containsMouse
        anchors.centerIn: parent

        onVisibleChanged: {
            if (visible) {
                fullScreenLabelAnimation.restart()
            }
        }

        PropertyAnimation on opacity {
            id: fullScreenLabelAnimation
            duration: 10000
            from: 1.0
            to: 0.0
            easing.type: Easing.InExpo
        }
    }

    MouseArea {
        id: mt11VideoMouseArea
        anchors.fill: parent
        enabled: _root.pipState.state === _root.pipState.fullState
        hoverEnabled: true

        onDoubleClicked: {
            var vehicle = QGroundControl.multiVehicleManager.activeVehicle
            if (_root.manager && vehicle && !vehicle.communicationLost) {
                _root.manager.fullScreen = !_root.manager.fullScreen
            }
        }
    }

    ProximityRadarVideoView {
        anchors.fill: parent
        vehicle: QGroundControl.multiVehicleManager.activeVehicle
    }

    ObstacleDistanceOverlayVideo {
        showText: _root.pipState.state === _root.pipState.fullState
    }
}
