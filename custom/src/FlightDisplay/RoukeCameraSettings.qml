/****************************************************************************
 * Rouke payload settings: two independently scrollable parameter pages.
 ****************************************************************************/
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QtControls
import QGroundControl.ScreenTools

QtControls.Popup {
    id: root
    required property var manager
    property Item anchorItem
    property int selectedPage: 0
    property int resetSerial: 0
    readonly property real edgeMargin: ScreenTools.defaultFontPixelHeight * 0.6
    readonly property bool narrowLayout: width < ScreenTools.defaultFontPixelWidth * 48
    readonly property bool compactLayout: height < ScreenTools.defaultFontPixelHeight * 27
    readonly property real inset: ScreenTools.defaultFontPixelHeight * (compactLayout ? 0.5 : 0.65)
    property point anchorPosition: Qt.point(0, 0)
    readonly property real minimumUsableHeight: Math.min(ScreenTools.defaultFontPixelHeight * 18,
                                                        parent ? Math.max(0, parent.height - edgeMargin * 2) : 360)
    readonly property real popupTop: Math.max(edgeMargin, Math.min(anchorPosition.y,
                                            parent ? parent.height - minimumUsableHeight - edgeMargin : edgeMargin))
    readonly property bool connected: Boolean(manager && manager.sdkResponding)
    objectName: "settingsPopup"
    parent: QtControls.Overlay.overlay
    width: Math.min(ScreenTools.defaultFontPixelWidth * 60, parent ? Math.max(0, parent.width - edgeMargin * 2) : 480)
    // Prefer the rail's top edge. Use the remaining height below it and scroll
    // the pages; shift upward only when that would leave an unusable viewport.
    y: popupTop
    height: Math.min(ScreenTools.defaultFontPixelHeight * 30,
                     parent ? Math.max(0, parent.height - popupTop - edgeMargin) : 600)
    x: Math.max(edgeMargin, Math.min(parent ? parent.width - width - edgeMargin : edgeMargin,
                                    anchorPosition.x - width - edgeMargin))
    padding: inset
    z: 100
    modal: false
    dim: false
    focus: true
    closePolicy: QtControls.Popup.CloseOnEscape | QtControls.Popup.CloseOnPressOutside

    function reposition() {
        if (!parent) return
        anchorPosition = anchorItem ? anchorItem.mapToItem(parent, 0, 0) : Qt.point(parent.width, edgeMargin)
    }
    function showSettings() {
        selectedPage = 0
        resetSerial++
        cameraScroll.contentY = 0
        rangeScroll.contentY = 0
        reposition()
        open()
    }
    Connections {
        target: root.parent
        function onWidthChanged() { if (root.visible) root.reposition() }
        function onHeightChanged() { if (root.visible) root.reposition() }
    }
    Connections {
        target: root.anchorItem
        function onXChanged() { if (root.visible) root.reposition() }
        function onYChanged() { if (root.visible) root.reposition() }
        function onHeightChanged() { if (root.visible) root.reposition() }
    }
    onAnchorItemChanged: if (visible) reposition()
    onOpened: reposition()
    onClosed: {
        // Prevent a child combo popup outliving its parent page.
        root.resetSerial++
    }
    background: Rectangle {
        radius: ScreenTools.defaultFontPixelHeight * 0.6
        color: "#172630"
        border.color: "#48616e"
        Rectangle {
            anchors.fill: parent
            anchors.margins: 2
            radius: parent.radius - 2
            color: "transparent"
            border.color: "#223844"
        }
    }
    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 120 } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 90 } }

    component SectionTitle: Item {
        id: sectionRoot
        property string text: ""
        property string iconName: "exposure"
        width: parent.width
        implicitHeight: Math.max(ScreenTools.defaultFontPixelHeight * 1.5,
                                 sectionText.implicitHeight + ScreenTools.defaultFontPixelHeight * 0.3)
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: ScreenTools.defaultFontPixelWidth * 0.65
            anchors.rightMargin: anchors.leftMargin
            spacing: ScreenTools.defaultFontPixelWidth * 0.8
            CameraControlIcon {
                Layout.preferredWidth: ScreenTools.defaultFontPixelHeight * 0.9
                Layout.preferredHeight: ScreenTools.defaultFontPixelHeight * 0.9
                kind: sectionRoot.iconName
                color: "#72d9ef"
            }
            Text {
                id: sectionText
                Layout.fillWidth: true
                text: sectionRoot.text
                color: "#8bc4d4"
                font.family: ScreenTools.normalFontFamily
                font.pointSize: ScreenTools.smallFontPointSize
                font.weight: Font.DemiBold
                font.letterSpacing: 0.4
                wrapMode: Text.Wrap
            }
        }
    }
    component Parameter: RoukeCameraParameter {
        width: parent.width
        manager: root.manager
        resetSerial: root.resetSerial
    }
    contentItem: ColumnLayout {
        spacing: ScreenTools.defaultFontPixelHeight * 0.35
        RowLayout {
            Layout.fillWidth: true
            spacing: ScreenTools.defaultFontPixelHeight * 0.5
            CameraControlIcon {
                Layout.preferredWidth: ScreenTools.defaultFontPixelHeight * 1.05
                Layout.preferredHeight: ScreenTools.defaultFontPixelHeight * 1.05
                kind: "settings"
                color: "#8fe4f3"
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("Payload settings")
                color: "#f1f7fa"
                font.family: ScreenTools.normalFontFamily
                font.pointSize: ScreenTools.defaultFontPointSize
                font.weight: Font.DemiBold
                wrapMode: Text.WordWrap
            }
            RoukeCameraButton {
                objectName: "closeSettingsButton"
                text: qsTr("Close settings")
                iconName: "close"
                iconOnly: true
                subtle: true
                compact: true
                onClicked: root.close()
            }
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: tabs.implicitHeight + 8
            radius: ScreenTools.defaultFontPixelHeight * 0.55
            color: "#111f29"
            RowLayout {
                id: tabs
                anchors.fill: parent
                anchors.margins: 4
                spacing: 4
                RoukeCameraButton {
                    objectName: "cameraTab"
                    iconName: root.width < ScreenTools.defaultFontPixelWidth * 30 ? "" : "camera"
                    iconAbove: false
                    tabStyle: true
                    leftPadding: ScreenTools.defaultFontPixelWidth * 0.65
                    rightPadding: leftPadding
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredWidth: 1
                    implicitHeight: Math.max(ScreenTools.defaultFontPixelHeight * 2.2,
                                            ScreenTools.isMobile ? ScreenTools.minTouchPixels : 0,
                                            contentItem.implicitHeight + topPadding + bottomPadding)
                    text: root.narrowLayout ? qsTr("Camera") : qsTr("Camera parameters")
                    Accessible.name: qsTr("Camera parameters")
                    checked: root.selectedPage === 0
                    onClicked: root.selectedPage = 0
                }
                RoukeCameraButton {
                    objectName: "rangeTab"
                    iconName: root.width < ScreenTools.defaultFontPixelWidth * 30 ? "" : "range"
                    iconAbove: false
                    tabStyle: true
                    leftPadding: ScreenTools.defaultFontPixelWidth * 0.65
                    rightPadding: leftPadding
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredWidth: 1
                    implicitHeight: Math.max(ScreenTools.defaultFontPixelHeight * 2.2,
                                            ScreenTools.isMobile ? ScreenTools.minTouchPixels : 0,
                                            contentItem.implicitHeight + topPadding + bottomPadding)
                    text: root.narrowLayout ? qsTr("Range") : qsTr("Range capture")
                    Accessible.name: qsTr("Range capture")
                    checked: root.selectedPage === 1
                    onClicked: root.selectedPage = 1
                }
            }
        }
        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: root.selectedPage
            Flickable {
                id: cameraScroll
                objectName: "cameraSettingsScroll"
                WheelHandler {
                    target: null
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                    onWheel: function(event) {
                        const delta = event.pixelDelta.y || event.angleDelta.y / 120 * ScreenTools.defaultFontPixelHeight * 3
                        cameraScroll.contentY = Math.max(0, Math.min(cameraScroll.contentHeight - cameraScroll.height, cameraScroll.contentY - delta))
                        event.accepted = true
                    }
                }
                clip: true
                contentWidth: width
                contentHeight: cameraColumn.implicitHeight
                boundsBehavior: Flickable.StopAtBounds
                flickableDirection: Flickable.VerticalFlick
                QtControls.ScrollBar.vertical: QtControls.ScrollBar { policy: QtControls.ScrollBar.AsNeeded }
                Column {
                    id: cameraColumn
                    width: parent.width - ScreenTools.defaultFontPixelWidth * 1.3
                    spacing: ScreenTools.defaultFontPixelHeight * 0.05
                    SectionTitle { text: qsTr("EXPOSURE & COLOR"); iconName: "exposure" }
                    Parameter {
                        parameterKey: "iso"
                        title: qsTr("ISO")
                        minimum: 0; maximum: 34; readable: true
                        syncReportedValueToDraft: false
                        hint: qsTr("Set code 0–34; reported ISO is shown separately.")
                    }
                    Parameter {
                        parameterKey: "whiteBalance"
                        title: qsTr("White balance")
                        minimum: 0; maximum: 14; readable: true
                        hint: qsTr("Index 0–14")
                    }
                    Parameter {
                        parameterKey: "shutterSpeed"
                        title: qsTr("Shutter speed")
                        inputPlaceholder: qsTr("Vendor index")
                        hint: qsTr("Vendor index; valid range awaits vendor confirmation.")
                    }
                    Parameter {
                        parameterKey: "aperture"
                        showSeparator: false
                        title: qsTr("Aperture")
                        inputPlaceholder: qsTr("Vendor index")
                        hint: qsTr("Vendor index; valid range awaits vendor confirmation.")
                    }
                    SectionTitle { text: qsTr("FOCUS & LENS"); iconName: "lens" }
                    Parameter {
                        parameterKey: "focusMode"
                        title: qsTr("Focus mode")
                        minimum: 0; maximum: 4
                        options: [ { text: "0 · AF-S", value: 0 }, { text: "1 · AF-A", value: 1 },
                                   { text: "2 · AF-C", value: 2 }, { text: "3 · DMF", value: 3 }, { text: "4 · MF", value: 4 } ]
                        hint: qsTr("Codes 0–4; select a focus mode.")
                    }
                    Parameter {
                        parameterKey: "focusValue"
                        showSeparator: false
                        title: qsTr("Focus value")
                        readable: true
                        hint: qsTr("Decimal raw value, e.g. 1200; device range not specified.")
                    }
                    SectionTitle { text: qsTr("IMAGE OUTPUT"); iconName: "image" }
                    Parameter {
                        parameterKey: "imageQuality"
                        title: qsTr("Image quality")
                        minimum: 1; maximum: 4
                        options: [ { text: "1 · " + qsTr("Light"), value: 1 }, { text: "2 · " + qsTr("Standard"), value: 2 },
                                   { text: "3 · " + qsTr("Fine"), value: 3 }, { text: "4 · " + qsTr("Extra fine"), value: 4 } ]
                        hint: qsTr("Codes 1–4; select an image quality.")
                    }
                    Parameter {
                        parameterKey: "imageSize"
                        showSeparator: false
                        title: qsTr("Image size")
                        minimum: 0; maximum: 3; readable: true
                        options: [ { text: "0 · " + qsTr("L · Large"), value: 0 }, { text: "1 · " + qsTr("M · Medium"), value: 1 },
                                   { text: "2 · " + qsTr("S · Small"), value: 2 }, { text: "3 · VGA", value: 3 } ]
                        hint: qsTr("Codes 0–3; select an image size.")
                    }
                    Item { width: 1; height: ScreenTools.defaultFontPixelHeight * 0.4 }
                }
            }
            Flickable {
                id: rangeScroll
                objectName: "rangeSettingsScroll"
                WheelHandler {
                    target: null
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                    onWheel: function(event) {
                        const delta = event.pixelDelta.y || event.angleDelta.y / 120 * ScreenTools.defaultFontPixelHeight * 3
                        rangeScroll.contentY = Math.max(0, Math.min(rangeScroll.contentHeight - rangeScroll.height, rangeScroll.contentY - delta))
                        event.accepted = true
                    }
                }
                clip: true
                contentWidth: width
                contentHeight: rangeColumn.implicitHeight
                boundsBehavior: Flickable.StopAtBounds
                flickableDirection: Flickable.VerticalFlick
                QtControls.ScrollBar.vertical: QtControls.ScrollBar { policy: QtControls.ScrollBar.AsNeeded }
                Column {
                    id: rangeColumn
                    width: parent.width - ScreenTools.defaultFontPixelWidth * 1.3
                    spacing: ScreenTools.defaultFontPixelHeight * 0.05
                    SectionTitle { text: qsTr("TRIGGER CONTROL"); iconName: "camera" }
                    Parameter {
                        parameterKey: "triggerPhoto"
                        showSeparator: false
                        title: qsTr("Trigger capture")
                        minimum: 0; maximum: 1; defaultDraft: 0
                        options: [ { text: "0 · " + qsTr("Off"), value: 0 }, { text: "1 · " + qsTr("On"), value: 1 } ]
                        hint: qsTr("0 = Off, 1 = On. Off by default; apply to confirm.")
                    }
                    SectionTitle { text: qsTr("DISTANCE PARAMETERS"); iconName: "range" }
                    Parameter {
                        parameterKey: "shootingDistance"
                        title: qsTr("Shooting distance")
                        minimum: 20; maximum: 40; unit: "m"
                        hint: qsTr("Integer: 20–40 m")
                    }
                    Parameter {
                        parameterKey: "distanceThreshold"
                        showSeparator: false
                        title: qsTr("Trigger distance threshold")
                        minimum: 30; maximum: 15000; unit: "cm"
                        hint: qsTr("Integer: 30–15000 cm")
                    }
                    Item { width: 1; height: ScreenTools.defaultFontPixelHeight * 0.4 }
                }
            }
        }
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: "#30424d" }
        RowLayout {
            Layout.fillWidth: true
            spacing: ScreenTools.defaultFontPixelWidth
            Rectangle {
                implicitWidth: 7; implicitHeight: 7; radius: 4
                color: root.manager && root.manager.pending ? "#ffc77c" : root.connected ? "#65d9f4" : "#78909f"
            }
            Text {
                Layout.fillWidth: true
                text: root.manager && root.manager.lastError ? root.manager.lastError
                      : root.manager && root.manager.pending ? qsTr("Waiting for payload…")
                      : root.connected ? qsTr("Payload connected") : qsTr("Awaiting payload response")
                color: root.manager && root.manager.lastError ? "#ffd098" : "#a9bfcc"
                font.family: ScreenTools.normalFontFamily
                font.pointSize: ScreenTools.smallFontPointSize
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }
        }
    }
}
