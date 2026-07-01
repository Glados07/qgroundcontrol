import QtQuick
import QtPositioning
import QtWebEngine

import QGroundControl
import QGroundControl.Controls
import QGroundControl.ScreenTools

Item {
    id: google3DRoot

    property var viewer3DManager: null
    property bool isViewer3DOpen: false

    property var _viewer3DSettings:      QGroundControl.corePlugin.viewer3DSettings
    property string apiKey:              String(_viewer3DSettings.google3DMapsApiKey.rawValue).trim()
    property var activeVehicle:          QGroundControl.multiVehicleManager.activeVehicle
    property var activeVehicleCoordinate: activeVehicle ? activeVehicle.coordinate : QtPositioning.coordinate()
    property var fallbackCoordinate:     QGroundControl.flightMapPosition
    property var centerCoordinate:       activeVehicleCoordinate && activeVehicleCoordinate.isValid ? activeVehicleCoordinate : fallbackCoordinate
    property bool centerValid:           centerCoordinate && centerCoordinate.isValid
    property bool canLoadGoogle3D:       apiKey.length > 0 && centerValid
    property string _lastLoadedSignature: ""

    function _centerAltitudeMeters() {
        var altitude = Number(centerCoordinate.altitude)
        return isNaN(altitude) ? 120 : Math.max(0, altitude + 120)
    }

    function _buildGoogle3DHtml() {
        var lat = Number(centerCoordinate.latitude)
        var lng = Number(centerCoordinate.longitude)
        var altitude = _centerAltitudeMeters()
        var encodedKey = encodeURIComponent(apiKey)

        // Google Maps JavaScript API 的 3D Maps 在浏览器环境中渲染，这里由 Qt WebEngine 承载。
        return [
            "<!doctype html>",
            "<html>",
            "<head>",
            "<meta charset=\"utf-8\">",
            "<meta name=\"viewport\" content=\"initial-scale=1,maximum-scale=1,user-scalable=no,width=device-width\">",
            "<style>",
            "html,body{width:100%;height:100%;margin:0;overflow:hidden;background:#111;}",
            "gmp-map-3d{width:100%;height:100%;display:block;}",
            "#error{position:absolute;left:16px;right:16px;bottom:16px;padding:10px 12px;background:rgba(0,0,0,.70);color:#fff;font:13px sans-serif;display:none;}",
            "</style>",
            "<script>",
            "async function initMap3D(){",
            "  try {",
            "    const { Map3DElement } = await google.maps.importLibrary('maps3d');",
            "    const map = new Map3DElement({",
            "      center: { lat: " + lat + ", lng: " + lng + ", altitude: " + altitude + " },",
            "      range: 1200,",
            "      tilt: 67.5,",
            "      heading: 0,",
            "      mode: 'HYBRID'",
            "    });",
            "    document.body.appendChild(map);",
            "    window.qgcGoogle3DMap = map;",
            "  } catch (e) {",
            "    const errorBox = document.getElementById('error');",
            "    errorBox.textContent = 'Google 3D Maps load failed: ' + e;",
            "    errorBox.style.display = 'block';",
            "  }",
            "}",
            "</script>",
            "<script async src=\"https://maps.googleapis.com/maps/api/js?key=" + encodedKey + "&v=alpha&libraries=maps3d&callback=initMap3D\"></script>",
            "</head>",
            "<body><div id=\"error\"></div></body>",
            "</html>"
        ].join("")
    }

    function reloadGoogle3DMap() {
        if (!canLoadGoogle3D) {
            _lastLoadedSignature = ""
            webView.url = "about:blank"
            return
        }

        var signature = apiKey + ":" + centerCoordinate.latitude.toFixed(7) + ":" + centerCoordinate.longitude.toFixed(7)
        if (signature === _lastLoadedSignature) {
            return
        }

        _lastLoadedSignature = signature
        webView.loadHtml(_buildGoogle3DHtml(), "https://qgroundcontrol.local/")
    }

    onApiKeyChanged: reloadTimer.restart()
    onCenterValidChanged: reloadTimer.restart()
    onCenterCoordinateChanged: {
        // 避免飞行中每次坐标刷新都重载在线地图；只在尚未成功加载时用新坐标初始化。
        if (_lastLoadedSignature === "") {
            reloadTimer.restart()
        }
    }
    onIsViewer3DOpenChanged: {
        if (isViewer3DOpen) {
            reloadTimer.restart()
        }
    }

    Connections {
        target: QGroundControl.multiVehicleManager
        function onActiveVehicleChanged() { reloadTimer.restart() }
    }

    Timer {
        id: reloadTimer
        interval: 150
        repeat: false
        onTriggered: google3DRoot.reloadGoogle3DMap()
    }

    WebEngineView {
        id: webView
        anchors.fill: parent
        visible: google3DRoot.canLoadGoogle3D
    }

    Rectangle {
        anchors.fill: parent
        visible: !google3DRoot.canLoadGoogle3D
        color: "#20242a"

        Column {
            anchors.centerIn: parent
            width: Math.min(parent.width * 0.75, ScreenTools.defaultFontPixelWidth * 62)
            spacing: ScreenTools.defaultFontPixelHeight

            QGCLabel {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                color: "white"
                text: google3DRoot.apiKey.length === 0 ?
                          qsTr("Google 3D Maps API Key is required.") :
                          qsTr("Waiting for a valid map center coordinate.")
            }

            QGCLabel {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                color: "#cfd6df"
                text: qsTr("Set the API key in Application Settings > Fly View > 3D View. The map center uses the active vehicle coordinate first, then the QGC flight map center.")
            }
        }
    }

    Component.onCompleted: reloadTimer.restart()
}