/****************************************************************************
 *
 * Three-view PIP manager for Map, Video 1 and Video 2.
 * The visual controls intentionally mirror src/QmlControls/PipView.qml.
 *
 ****************************************************************************/

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window

import QGroundControl
import QGroundControl.Controls
import QGroundControl.ScreenTools

Item {
    id: root

    width: _pipSize
    height: _isExpanded && _upperItem
            ? (_pipItemHeight * 2) + _pipSpacing : _pipItemHeight
    visible: show && (_lowerItem || _upperItem)

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
    property var _lowerItem: null
    property var _upperItem: null
    property var _windowItem: null
    property bool _isExpanded: true
    property real _pipSize: parent.width * 0.2
    property real _maxSize: 0.75
    property real _minSize: 0.10
    property real _pipSpacing: ScreenTools.defaultFontPixelHeight * 0.5
    readonly property real _pipItemHeight: _pipSize * (9 / 16)
    property bool _componentComplete: false
    property var _knownItems: []

    Component.onCompleted: {
        _componentComplete = true
        _knownItems = [item1, item2, item3]
        _initializeLayout()
        _setPipIsExpanded(QGroundControl.loadBoolGlobalSetting(
                              _pipExpandedSettingsKey, true))
    }

    onItem1Changed: _itemsChanged()
    onItem2Changed: _itemsChanged()
    onItem3Changed: _itemsChanged()

    function _availableItems() {
        var items = []
        if (item1) {
            items.push(item1)
        }
        if (item2) {
            items.push(item2)
        }
        if (item3) {
            items.push(item3)
        }
        return items
    }

    function _isAvailable(item) {
        return item && _availableItems().indexOf(item) >= 0
    }

    function _itemKey(item) {
        if (item === item1) {
            return "map"
        }
        if (item === item2) {
            return "video1"
        }
        if (item === item3) {
            return "video2"
        }
        return ""
    }

    function _itemForKey(key) {
        if (key === "map") {
            return item1
        }
        if (key === "video1" || key === "a8mini") {
            return item2
        }
        if (key === "video2" || key === "mt11") {
            return item3
        }
        return null
    }

    function _initialFullItem() {
        var selected = _itemForKey(QGroundControl.loadGlobalSetting(
                                       currentItemSettingsKey, ""))
        if (_isAvailable(selected)) {
            return selected
        }

        // Preserve the native two-view selection on first upgrade.
        var mapWasFull = QGroundControl.loadBoolGlobalSetting(
                    "MainFlyWindowIsMap", true)
        return mapWasFull ? item1 : (item2 || item3 || item1)
    }

    function _initializeLayout() {
        _reconcileLayout(_initialFullItem())
    }

    function _itemsChanged() {
        if (!_componentComplete) {
            return
        }

        var currentItems = [item1, item2, item3]
        for (var i = 0; i < _knownItems.length; ++i) {
            var oldItem = _knownItems[i]
            if (!oldItem || currentItems.indexOf(oldItem) >= 0
                    || !oldItem.pipState) {
                continue
            }

            if (oldItem === _windowItem) {
                oldItem.pipState.windowAboutToClose()
                _windowItem = null
                pipWindow.hide()
            }
            oldItem.pipState.state = oldItem.pipState.initState
        }

        _knownItems = currentItems
        Qt.callLater(function() {
            root._reconcileLayout(root._fullItem)
        })
    }

    function _reconcileLayout(preferredFull) {
        var available = _availableItems()
        if (available.length === 0) {
            _fullItem = null
            _lowerItem = null
            _upperItem = null
            return
        }

        var main = available.indexOf(preferredFull) >= 0
                ? preferredFull : available[0]
        var remaining = []
        for (var i = 0; i < available.length; ++i) {
            if (available[i] !== main) {
                remaining.push(available[i])
            }
        }

        var lower = remaining.indexOf(_lowerItem) >= 0 ? _lowerItem : null
        var upper = remaining.indexOf(_upperItem) >= 0
                && _upperItem !== lower ? _upperItem : null

        for (var j = 0; j < remaining.length; ++j) {
            var candidate = remaining[j]
            if (candidate === lower || candidate === upper) {
                continue
            }
            if (!lower) {
                lower = candidate
            } else if (!upper) {
                upper = candidate
            }
        }

        // A single remaining view always occupies the bottom slot. This keeps
        // the PIP anchored at the native lower-left position when one source
        // is disabled or removed.
        if (!lower && upper) {
            lower = upper
            upper = null
        }

        _applyLayout(main, lower, upper, false)
    }

    function _applyLayout(main, lower, upper, persist) {
        if (!_isAvailable(main)) {
            return
        }

        _fullItem = main
        _lowerItem = lower
        _upperItem = upper

        var items = _availableItems()
        for (var i = 0; i < items.length; ++i) {
            var candidate = items[i]
            if (!candidate.pipState) {
                continue
            }
            if (candidate === main) {
                if (candidate === _windowItem) {
                    candidate.pipState.windowAboutToClose()
                    _windowItem = null
                    pipWindow.hide()
                }
                candidate.pipState.state = candidate.pipState.fullState
            } else if (candidate !== _windowItem) {
                candidate.pipState.state = candidate.pipState.pipState
            }
        }

        if (persist && currentItemSettingsKey.length > 0) {
            QGroundControl.saveGlobalSetting(currentItemSettingsKey,
                                             _itemKey(main))
        }
    }

    function _activateSlot(slotIndex) {
        var clickedItem = slotIndex === 1 ? _lowerItem : _upperItem
        var oldMain = _fullItem
        if (!_isAvailable(clickedItem) || !_isAvailable(oldMain)
                || clickedItem === oldMain) {
            return
        }

        // Enter full state before changing the adapter's slot target. Then put
        // the previous main view into exactly the slot which was clicked.
        clickedItem.pipState.state = clickedItem.pipState.fullState
        _fullItem = clickedItem
        if (slotIndex === 1) {
            _lowerItem = oldMain
        } else {
            _upperItem = oldMain
        }
        oldMain.pipState.state = oldMain.pipState.pipState

        if (currentItemSettingsKey.length > 0) {
            QGroundControl.saveGlobalSetting(currentItemSettingsKey,
                                             _itemKey(clickedItem))
        }
    }

    function _setPipIsExpanded(isExpanded) {
        QGroundControl.saveBoolGlobalSetting(_pipExpandedSettingsKey,
                                             isExpanded)
        _isExpanded = isExpanded
    }

    function _showWindow(item) {
        if (!item || _windowItem) {
            return
        }
        _windowItem = item
        pipWindow.width = root.width
        pipWindow.height = root._pipItemHeight
        pipWindow.title = _itemKey(item)
        pipWindow.show()
    }

    Window {
        id: pipWindow
        visible: false

        onClosing: {
            var item = root._windowItem
            root._windowItem = null
            if (item && item.pipState) {
                item.pipState.windowAboutToClose()
                item.pipState.state = item.pipState.pipState
            }
        }
    }

    Item {
        id: item1PipAdapter
        parent: root.parent
        visible: false
        property Item _pipContentItem: root._lowerItem === root.item1
                                       ? lowerPane.contentItem
                                       : upperPane.contentItem
        property Item _windowContentItem: pipWindow.contentItem
        function showWindow() { root._showWindow(root.item1) }
    }

    Item {
        id: item2PipAdapter
        parent: root.parent
        visible: false
        property Item _pipContentItem: root._lowerItem === root.item2
                                       ? lowerPane.contentItem
                                       : upperPane.contentItem
        property Item _windowContentItem: pipWindow.contentItem
        function showWindow() { root._showWindow(root.item2) }
    }

    Item {
        id: item3PipAdapter
        parent: root.parent
        visible: false
        property Item _pipContentItem: root._lowerItem === root.item3
                                       ? lowerPane.contentItem
                                       : upperPane.contentItem
        property Item _windowContentItem: pipWindow.contentItem
        function showWindow() { root._showWindow(root.item3) }
    }

    component PipPane: Item {
        id: pane

        property int slotIndex: 1
        readonly property var viewItem: slotIndex === 1
                                        ? root._lowerItem : root._upperItem
        readonly property Item contentItem: slotContent

        width: root.width
        height: root._pipItemHeight
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.bottomMargin: slotIndex === 1
                              ? 0 : root._pipItemHeight + root._pipSpacing
        visible: root._isExpanded && viewItem && viewItem.pipState
                 && viewItem.pipState.state === viewItem.pipState.pipState
        clip: true

        Item {
            id: slotContent
            anchors.fill: parent
            z: 0
        }

        // Keep the click layer above the reparented map/video item. This is the
        // same ordering used by native PipView.qml.
        MouseArea {
            id: paneMouseArea
            anchors.fill: parent
            z: 1
            enabled: pane.visible
            preventStealing: true
            hoverEnabled: true
            onClicked: root._activateSlot(pane.slotIndex)
        }

        Image {
            id: popupPip
            z: 2
            source: "/qmlimages/PiP.svg"
            mipmap: true
            fillMode: Image.PreserveAspectFit
            anchors.left: parent.left
            anchors.top: parent.top
            visible: pane.visible && !ScreenTools.isMobile
                     && paneMouseArea.containsMouse && !root._windowItem
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
            z: 2
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
                onClicked: root._setPipIsExpanded(false)
            }
        }

        Image {
            id: pipResizeIcon
            z: 2
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
                    initialWidth = root.width
                }

                onReleased: pipResize.anchors.fill = pipResizeIcon

                onPositionChanged: function(mouse) {
                    if (!pressed) {
                        return
                    }
                    var parentWidth = root.parent.width
                    var newWidth = initialWidth + mouse.x - initialX
                    if (newWidth < parentWidth * root._maxSize
                            && newWidth > parentWidth * root._minSize) {
                        root._pipSize = newWidth
                    }
                }
            }
        }
    }

    PipPane {
        id: lowerPane
        slotIndex: 1
    }

    PipPane {
        id: upperPane
        slotIndex: 2
    }

    Rectangle {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        height: ScreenTools.defaultFontPixelHeight * 2
        width: height
        radius: ScreenTools.defaultFontPixelHeight / 3
        visible: !root._isExpanded
        color: root._fullItem && root._fullItem.pipState.isDark
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
            onClicked: root._setPipIsExpanded(true)
        }
    }

    Connections {
        target: root.parent

        function onWidthChanged() {
            if (!root._componentComplete) {
                return
            }
            var parentWidth = root.parent.width
            if (root.width > parentWidth * root._maxSize) {
                root._pipSize = parentWidth * root._maxSize
            } else if (root.width < parentWidth * root._minSize) {
                root._pipSize = parentWidth * root._minSize
            }
        }
    }
}
