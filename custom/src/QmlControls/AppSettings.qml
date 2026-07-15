/****************************************************************************
 *
 * Custom App Settings entry for SecDev Viewer3D build.
 * 该文件只改设置页的入口路由：Fly View 页面强制加载 custom 版本，
 * 其他设置页继续使用 QGC 原有 AppSettings 模块。
 *
 ****************************************************************************/

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Palette
import QGroundControl.Controls
import QGroundControl.ScreenTools
import QGroundControl.AppSettings

Rectangle {
    id:     settingsView
    color:  qgcPal.window
    z:      QGroundControl.zOrderTopMost

    readonly property real _defaultTextHeight:  ScreenTools.defaultFontPixelHeight
    readonly property real _defaultTextWidth:   ScreenTools.defaultFontPixelWidth
    readonly property real _horizontalMargin:   _defaultTextWidth / 2
    readonly property real _verticalMargin:     _defaultTextHeight / 2
    readonly property real _buttonHeight:       ScreenTools.isTinyScreen ? ScreenTools.defaultFontPixelHeight * 3 : ScreenTools.defaultFontPixelHeight * 2

    property bool _first: true
    property bool _commingFromRIDSettings: false

    function customSettingsPageUrl(url) {
        // 目标分支的 Fly View 设置页必须走 custom 覆盖页，否则会落到 src 中残留的旧 Viewer3D 设置。
        if (url === "qrc:/qml/QGroundControl/AppSettings/FlyViewSettings.qml") {
            return "qrc:/Custom/qml/QGroundControl/AppSettings/FlyViewSettings.qml"
        }
        return url
    }

    function showSettingsPage(settingsPage) {
        for (var i = 0; i < buttonRepeater.count; i++) {
            var button = buttonRepeater.itemAt(i)
            if (button.text === settingsPage) {
                button.clicked()
                break
            }
        }
    }

    DeadMouseArea {
        anchors.fill: parent
    }

    QGCPalette { id: qgcPal }

    Component.onCompleted: {
        if (globals.commingFromRIDIndicator) {
            rightPanel.source = "qrc:/qml/QGroundControl/AppSettings/RemoteIDSettings.qml"
            globals.commingFromRIDIndicator = false
        } else {
            rightPanel.source = "qrc:/qml/QGroundControl/AppSettings/GeneralSettings.qml"
        }
    }

    SettingsPagesModel { id: settingsPagesModel }

    QGCFlickable {
        id:                 buttonList
        width:              buttonColumn.width
        anchors.topMargin:  _verticalMargin
        anchors.top:        parent.top
        anchors.bottom:     parent.bottom
        anchors.leftMargin: _horizontalMargin
        anchors.left:       parent.left
        contentHeight:      buttonColumn.height + _verticalMargin
        flickableDirection: Flickable.VerticalFlick
        clip:               true

        ColumnLayout {
            id:         buttonColumn
            spacing:    ScreenTools.defaultFontPixelHeight / 4

            property real _maxButtonWidth: 0

            Repeater {
                id:     buttonRepeater
                model:  settingsPagesModel

                SettingsButton {
                    Layout.fillWidth:   true
                    text:               name
                    icon.source:        iconUrl
                    visible:            pageVisible()

                    onClicked: {
                        if (mainWindow.allowViewSwitch()) {
                            var targetUrl = settingsView.customSettingsPageUrl(url)
                            if (String(rightPanel.source) !== targetUrl) {
                                rightPanel.source = targetUrl
                            }
                            checked = true
                        }
                    }

                    Component.onCompleted: {
                        if (globals.commingFromRIDIndicator) {
                            _commingFromRIDSettings = true
                        }
                        if (_first) {
                            _first = false
                            checked = true
                        }
                        if (_commingFromRIDSettings) {
                            checked = false
                            _commingFromRIDSettings = false
                            if (modelData.url === "qrc:/qml/QGroundControl/AppSettings/RemoteIDSettings.qml") {
                                checked = true
                            }
                        }
                    }
                }
            }
        }
    }

    Rectangle {
        id:                     divider
        anchors.topMargin:      _verticalMargin
        anchors.bottomMargin:   _verticalMargin
        anchors.leftMargin:     _horizontalMargin
        anchors.left:           buttonList.right
        anchors.top:            parent.top
        anchors.bottom:         parent.bottom
        width:                  1
        color:                  qgcPal.windowShade
    }

    Loader {
        id:                     rightPanel
        anchors.leftMargin:     _horizontalMargin
        anchors.rightMargin:    _horizontalMargin
        anchors.topMargin:      _verticalMargin
        anchors.bottomMargin:   _verticalMargin
        anchors.left:           divider.right
        anchors.right:          parent.right
        anchors.top:            parent.top
        anchors.bottom:         parent.bottom
    }
}
