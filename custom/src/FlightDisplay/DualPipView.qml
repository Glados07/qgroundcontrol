/****************************************************************************
 *
 * Three-way Fly View PIP manager used by the map, A8 Mini and MT11 views.
 *
 ****************************************************************************/

import QtQuick
import QtQuick.Window

import QGroundControl
import QGroundControl.ScreenTools
import QGroundControl.Controls

Item {
    id: _root

    width: _pipSize
    height: _isExpanded && _pipCount > 0
            ? (_pipCount * _pipItemHeight) + ((_pipCount - 1) * _pipSpacing)
            : _pipItemHeight
    visible: show && _pipCount > 0

    property var item1: null
    property var item2: null
    property var item3: null
    property string currentItemSettingsKey
    property bool show: true

    readonly property alias item1PipView: item1PipAdapter
    readonly property alias item2PipView: item2PipAdapter
    readonly property alias item3PipView: item3PipAdapter

    readonly property string _pipExpandedSettingsKey: "IsPIPVisible"
    property var _fullItem: null
    property bool _isExpanded: true
    property real _pipSize: parent.width * 0.2
    property real _maxSize: 0.75
    property real _minSize: 0.10
    property real _pipSpacing: ScreenTools.defaultFontPixelHeight * 0.5
    readonly property real _pipItemHeight: _pipSize * (9 / 16)
    readonly property int _pipCount: (_isPip(item1) ? 1 : 0)
                                         + (_isPip(item2) ? 1 : 0)
                                         + (_isPip(item3) ? 1 : 0)
    property bool _componentComplete: false
    property var _knownItems: []

    Component.onCompleted: {
        _componentComplete = true
        _knownItems = [item1, item2, item3]
        _initForItems()
        _setPipIsExpanded(QGroundControl.loadBoolGlobalSetting(
                              _pipExpandedSettingsKey, true))
    }

    onItem1Changed: _itemsChanged()
    onItem2Changed: _itemsChanged()
    onItem3Changed: _itemsChanged()

    function _isPip(item) {
        return item && item.pipState
                && item.pipState.state === item.pipState.pipState
    }

    function _itemKey(item) {
        if (item === item1) {
            return "map"
        }
        if (item === item2) {
            return "a8mini"
        }
        if (item === item3) {
            return "mt11"
        }
        return ""
    }

    function _itemForKey(key) {
        if (key === "map") {
            return item1
        }
        if (key === "a8mini") {
            return item2
        }
        if (key === "mt11") {
            return item3
        }
        return null
    }

    function _firstAvailableItem() {
        return item1 || item2 || item3
    }

    function _itemsChanged() {
        if (!_componentComplete) {
            return
        }

        var currentItems = [item1, item2, item3]
        for (var i = 0; i < _knownItems.length; ++i) {
            var oldItem = _knownItems[i]
            if (oldItem && currentItems.indexOf(oldItem) < 0
                    && oldItem.pipState
                    && oldItem.pipState.state
                       === oldItem.pipState.windowState) {
                // A disabled/removed stream must not remain parented to the
                // shared popup window. Mirror PipView's close transition
                // before hiding the now-empty window.
                oldItem.pipState.windowAboutToClose()
                oldItem.pipState.state = oldItem.pipState.pipState
                pipWindow.hide()
            }
        }
        _knownItems = currentItems
        Qt.callLater(_initForItems)
    }

    function _initForItems() {
        if (!_componentComplete) {
            return
        }

        var selected = _itemForKey(QGroundControl.loadGlobalSetting(
                                       currentItemSettingsKey, ""))
        if (!selected) {
            // Preserve the native two-view setting on first upgrade.
            var mapWasFull = QGroundControl.loadBoolGlobalSetting(
                        "MainFlyWindowIsMap", true)
            selected = mapWasFull ? item1 : (item2 || item3 || item1)
        }
        if (!selected) {
            selected = _firstAvailableItem()
        }
        _setFullItem(selected, false)
    }

    function _setFullItem(item, persist) {
        if (!item || !item.pipState) {
            return
        }

        var items = [item1, item2, item3]
        if (_fullItem && _fullItem !== item
                && items.indexOf(_fullItem) < 0 && _fullItem.pipState) {
            _fullItem.pipState.state = _fullItem.pipState.pipState
        }
        for (var i = 0; i < items.length; ++i) {
            var candidate = items[i]
            if (!candidate || !candidate.pipState) {
                continue
            }
            if (candidate === item) {
                candidate.pipState.state = candidate.pipState.fullState
            } else if (candidate.pipState.state
                       !== candidate.pipState.windowState) {
                candidate.pipState.state = candidate.pipState.pipState
            }
        }

        _fullItem = item
        if (persist && currentItemSettingsKey.length > 0) {
            QGroundControl.saveGlobalSetting(currentItemSettingsKey,
                                             _itemKey(item))
        }
    }

    function _pipIndex(item) {
        var index = 0
        var items = [item1, item2, item3]
        for (var i = 0; i < items.length; ++i) {
            var candidate = items[i]
            if (candidate === item) {
                return index
            }
            if (_isPip(candidate)) {
                ++index
            }
        }
        return -1
    }

    function _pipBottomMargin(item) {
        var index = _pipIndex(item)
        return index < 0 ? 0 : index * (_pipItemHeight + _pipSpacing)
    }

    function _setPipIsExpanded(isExpanded) {
        QGroundControl.saveBoolGlobalSetting(_pipExpandedSettingsKey,
                                             isExpanded)
        _isExpanded = isExpanded
    }

    function _showWindow(item) {
        pipWindow.width = _root.width
        pipWindow.height = _pipItemHeight
        pipWindow.title = _itemKey(item)
        pipWindow.show()
    }

    Window {
        id: pipWindow
        visible: false

        onClosing: {
            var item = contentItem.children[0]
            if (item && item.pipState) {
                item.pipState.windowAboutToClose()
                item.pipState.state = item.pipState.pipState
            }
        }
    }

    // PipState needs a view-specific content item but also expects pipView.parent
    // to be the common full-size Fly View holder. These invisible adapters keep
    // that native contract intact for all three views.
    Item {
        id: item1PipAdapter
        parent: _root.parent
        visible: false
        property Item _pipContentItem: item1Pane
        property Item _windowContentItem: pipWindow.contentItem
        function showWindow() { _root._showWindow(_root.item1) }
    }

    Item {
        id: item2PipAdapter
        parent: _root.parent
        visible: false
        property Item _pipContentItem: item2Pane
        property Item _windowContentItem: pipWindow.contentItem
        function showWindow() { _root._showWindow(_root.item2) }
    }

    Item {
        id: item3PipAdapter
        parent: _root.parent
        visible: false
        property Item _pipContentItem: item3Pane
        property Item _windowContentItem: pipWindow.contentItem
        function showWindow() { _root._showWindow(_root.item3) }
    }

    component PipPane: Item {
        id: pane

        required property var viewItem

        width: _root.width
        height: _root._pipItemHeight
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.bottomMargin: _root._pipBottomMargin(viewItem)
        visible: _root._isExpanded && _root._isPip(viewItem)
        clip: true

        MouseArea {
            id: paneMouseArea
            anchors.fill: parent
            enabled: pane.visible
            preventStealing: true
            hoverEnabled: true
            onClicked: _root._setFullItem(pane.viewItem, true)
        }

        Image {
            id: popupPip
            source: "/qmlimages/PiP.svg"
            mipmap: true
            fillMode: Image.PreserveAspectFit
            anchors.left: parent.left
            anchors.top: parent.top
            visible: pane.visible && !ScreenTools.isMobile
                     && paneMouseArea.containsMouse
                     && pipWindow.contentItem.children.length === 0
            height: ScreenTools.defaultFontPixelHeight * 2.5
            width: height
            sourceSize.height: height

            MouseArea {
                anchors.fill: parent
                onClicked: pane.viewItem.pipState.state
                           = pane.viewItem.pipState.windowState
            }
        }

        Image {
            id: hidePip
            source: "/qmlimages/pipHide.svg"
            mipmap: true
            fillMode: Image.PreserveAspectFit
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            visible: pane.visible
                     && (ScreenTools.isMobile || paneMouseArea.containsMouse)
            height: ScreenTools.defaultFontPixelHeight * 2.5
            width: height
            sourceSize.height: height

            MouseArea {
                anchors.fill: parent
                onClicked: _root._setPipIsExpanded(false)
            }
        }

        Image {
            id: pipResizeIcon
            source: "/qmlimages/pipResize.svg"
            fillMode: Image.PreserveAspectFit
            mipmap: true
            anchors.right: parent.right
            anchors.top: parent.top
            visible: pane.visible
                     && (ScreenTools.isMobile || paneMouseArea.containsMouse)
            height: ScreenTools.defaultFontPixelHeight * 2.5
            width: height
            sourceSize.height: height

            MouseArea {
                id: pipResize
                anchors.fill: parent
                preventStealing: true
                cursorShape: Qt.PointingHandCursor

                property real initialX: 0
                property real initialWidth: 0

                onPressed: function(mouse) {
                    pipResize.anchors.fill = undefined
                    initialX = mouse.x
                    initialWidth = _root.width
                }

                onReleased: pipResize.anchors.fill = pipResizeIcon

                onPositionChanged: function(mouse) {
                    if (!pressed) {
                        return
                    }
                    var parentWidth = _root.parent.width
                    var newWidth = initialWidth + mouse.x - initialX
                    if (newWidth < parentWidth * _root._maxSize
                            && newWidth > parentWidth * _root._minSize) {
                        _root._pipSize = newWidth
                    }
                }
            }
        }
    }

    PipPane {
        id: item1Pane
        viewItem: _root.item1
    }

    PipPane {
        id: item2Pane
        viewItem: _root.item2
    }

    PipPane {
        id: item3Pane
        viewItem: _root.item3
    }

    Rectangle {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        height: ScreenTools.defaultFontPixelHeight * 2
        width: height
        radius: ScreenTools.defaultFontPixelHeight / 3
        visible: !_root._isExpanded
        color: _root._fullItem && _root._fullItem.pipState.isDark
               ? Qt.rgba(0, 0, 0, 0.75) : Qt.rgba(0, 0, 0, 0.5)

        Image {
            width: parent.width * 0.75
            height: parent.height * 0.75
            sourceSize.height: height
            source: "/res/buttonRight.svg"
            mipmap: true
            fillMode: Image.PreserveAspectFit
            anchors.centerIn: parent
        }

        MouseArea {
            anchors.fill: parent
            onClicked: _root._setPipIsExpanded(true)
        }
    }

    Connections {
        target: _root.parent

        function onWidthChanged() {
            if (!_root._componentComplete) {
                return
            }
            var parentWidth = _root.parent.width
            if (_root.width > parentWidth * _root._maxSize) {
                _root._pipSize = parentWidth * _root._maxSize
            } else if (_root.width < parentWidth * _root._minSize) {
                _root._pipSize = parentWidth * _root._minSize
            }
        }
    }
}
