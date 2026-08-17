/****************************************************************************
 *
 * Video surface for the independent secondary RTSP receiver.
 * Mirrors src/FlightDisplay/FlightDisplayViewVideo.qml.
 *
 ****************************************************************************/

import QtQuick
import QtQuick.Controls
import QtQuick.Window

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FlightDisplay
import QGroundControl.ScreenTools

Item {
    id: root
    clip: true

    property var manager: null
    property bool useSmallFont: true
    readonly property var _renderWindow: root.Window.window

    readonly property real _aspectRatio: manager && manager.aspectRatio > 0
                                         ? manager.aspectRatio : (16 / 9)
    readonly property bool _showGrid: QGroundControl.settingsManager
                                          .videoSettings.gridLines.rawValue
    readonly property int _fitMode: QGroundControl.settingsManager
                                        .videoSettings.videoFit.rawValue
    readonly property bool _fitWidth: _fitMode === 0
    readonly property bool _fitHeight: _fitMode === 1
    readonly property bool _fill: _fitMode === 2
    readonly property bool _noCrop: _fitMode === 3

    function getWidth() {
        return videoBackground.getWidth()
    }

    function getHeight() {
        return videoBackground.getHeight()
    }

    function initVideoItem() {
        if (root.manager && root._renderWindow && videoLoader.item) {
            root.manager.initVideoItem(root._renderWindow, videoLoader.item)
        }
    }

    on_RenderWindowChanged: Qt.callLater(root.initVideoItem)

    Image {
        id: noVideo
        z: 1
        anchors.fill: parent
        source: "/res/NoVideoBackground.jpg"
        fillMode: Image.PreserveAspectCrop
        visible: !root.manager || !root.manager.decoding

        Rectangle {
            anchors.centerIn: parent
            width: noVideoLabel.contentWidth + ScreenTools.defaultFontPixelHeight
            height: noVideoLabel.contentHeight + ScreenTools.defaultFontPixelHeight
            radius: ScreenTools.defaultFontPixelWidth / 2
            color: "black"
            opacity: 0.5
        }

        QGCLabel {
            id: noVideoLabel
            text: root.manager && root.manager.hasVideo
                  ? qsTr("WAITING FOR VIDEO") : qsTr("VIDEO DISABLED")
            font.bold: true
            color: "white"
            font.pointSize: root.useSmallFont
                            ? ScreenTools.smallFontPointSize
                            : ScreenTools.largeFontPointSize
            anchors.centerIn: parent
        }
    }

    Rectangle {
        id: videoBackground
        z: 0
        anchors.fill: parent
        color: "black"
        // Keep the dynamically loaded GL item in the scene graph while the
        // receiver starts. The no-video image above it remains visible until
        // the first decoded frame arrives.
        visible: root.manager
                 && (root.manager.hasVideo || root.manager.initialized)

        function getWidth() {
            if (root._aspectRatio > 0) {
                if (root._fitHeight
                        || (root._fill && width / height < root._aspectRatio)
                        || (root._noCrop && width / height > root._aspectRatio)) {
                    return height * root._aspectRatio
                }
            }
            return width
        }

        function getHeight() {
            if (root._aspectRatio > 0) {
                if (root._fitWidth
                        || (root._fill && width / height > root._aspectRatio)
                        || (root._noCrop && width / height < root._aspectRatio)) {
                    return width / root._aspectRatio
                }
            }
            return height
        }

        Component {
            id: videoBackgroundComponent

            QGCVideoBackground {
                id: videoContent
                objectName: "secondaryVideoContent"

                Rectangle {
                    color: Qt.rgba(1, 1, 1, 0.5)
                    height: parent.height
                    width: 1
                    x: parent.width * 0.33
                    visible: root._showGrid && !root.manager.fullScreen
                }

                Rectangle {
                    color: Qt.rgba(1, 1, 1, 0.5)
                    height: parent.height
                    width: 1
                    x: parent.width * 0.66
                    visible: root._showGrid && !root.manager.fullScreen
                }

                Rectangle {
                    color: Qt.rgba(1, 1, 1, 0.5)
                    width: parent.width
                    height: 1
                    y: parent.height * 0.33
                    visible: root._showGrid && !root.manager.fullScreen
                }

                Rectangle {
                    color: Qt.rgba(1, 1, 1, 0.5)
                    width: parent.width
                    height: 1
                    y: parent.height * 0.66
                    visible: root._showGrid && !root.manager.fullScreen
                }
            }
        }

        Loader {
            id: videoLoader
            width: videoBackground.getWidth()
            height: videoBackground.getHeight()
            anchors.centerIn: parent
            visible: active
            active: root.manager
                    && (root.manager.hasVideo || root.manager.initialized)
            sourceComponent: videoBackgroundComponent

            onLoaded: Qt.callLater(root.initVideoItem)
        }
    }
}
