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
    readonly property real edgeMargin: ScreenTools.defaultFontPixelHeight * 0.8
    readonly property bool narrowLayout: width < ScreenTools.defaultFontPixelWidth * 48
    readonly property bool compactLayout: height < ScreenTools.defaultFontPixelHeight * 27
    readonly property real inset: ScreenTools.defaultFontPixelHeight * (compactLayout ? 0.7 : 1.1)
    readonly property bool connected: Boolean(manager && manager.sdkResponding)
    objectName: "settingsPopup"
    parent: QtControls.Overlay.overlay
    width: Math.min(ScreenTools.defaultFontPixelWidth * 66, parent ? parent.width - edgeMargin * 2 : 560)
    height: Math.min(ScreenTools.defaultFontPixelHeight * 39, parent ? parent.height - edgeMargin * 2 : 720)
    padding: inset
    z: 100
    modal: false
    dim: false
    focus: true
    closePolicy: QtControls.Popup.CloseOnEscape | QtControls.Popup.CloseOnPressOutside

    function reposition() {
        if (!parent) return
        const p = anchorItem ? anchorItem.mapToItem(parent, 0, 0) : Qt.point(parent.width, 0)
        x = Math.max(edgeMargin, Math.min(parent.width - width - edgeMargin, p.x - width - edgeMargin))
        y = Math.max(edgeMargin, Math.min(parent.height - height - edgeMargin,
                                          p.y + (anchorItem ? anchorItem.height / 2 : 0) - height / 2))
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
    onWidthChanged: if (visible) reposition()
    onHeightChanged: if (visible) reposition()
    onOpened: {
        reposition()
        if (manager) manager.refreshCameraParameters()
    }
    onAboutToHide: if (manager) manager.cancelCameraParameterRefresh()
    onClosed: {
        // Prevent a child combo popup outliving its parent page.
        root.resetSerial++
    }
    Component.onDestruction: if (manager) manager.cancelCameraParameterRefresh()
    background: Rectangle {
        radius: ScreenTools.defaultFontPixelHeight * 0.85
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
        implicitHeight: Math.max(ScreenTools.defaultFontPixelHeight * 2,
                                 sectionText.implicitHeight + ScreenTools.defaultFontPixelHeight * 0.65)
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: ScreenTools.defaultFontPixelWidth * 0.65
            anchors.rightMargin: anchors.leftMargin
            spacing: ScreenTools.defaultFontPixelWidth * 1.1
            CameraControlIcon {
                Layout.preferredWidth: ScreenTools.defaultFontPixelHeight * 1.05
                Layout.preferredHeight: ScreenTools.defaultFontPixelHeight * 1.05
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
        spacing: ScreenTools.defaultFontPixelHeight * (root.compactLayout ? 0.45 : 0.85)
        RowLayout {
            Layout.fillWidth: true
            spacing: ScreenTools.defaultFontPixelHeight * 0.75
            Rectangle {
                visible: !root.compactLayout
                Layout.preferredWidth: ScreenTools.defaultFontPixelHeight * 2.4
                Layout.preferredHeight: ScreenTools.defaultFontPixelHeight * 2.4
                radius: ScreenTools.defaultFontPixelHeight * 0.75
                color: "#233e4c"
                CameraControlIcon {
                    anchors.centerIn: parent
                    width: ScreenTools.defaultFontPixelHeight * 1.3
                    height: width
                    kind: "settings"
                    color: "#8fe4f3"
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: ScreenTools.defaultFontPixelHeight * 0.25
                Text {
                    visible: !root.compactLayout
                    text: "ROUKE  /  LR1"
                    color: "#86adbd"
                    font.family: ScreenTools.normalFontFamily
                    font.pointSize: ScreenTools.smallFontPointSize
                    font.letterSpacing: 1.4
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Payload settings")
                    color: "#f1f7fa"
                    font.family: ScreenTools.normalFontFamily
                    font.pointSize: ScreenTools.defaultFontPointSize * 1.3
                    font.weight: Font.DemiBold
                    wrapMode: Text.WordWrap
                }
            }
            RoukeCameraButton {
                objectName: "closeSettingsButton"
                text: qsTr("Close settings")
                iconName: "close"
                iconOnly: true
                subtle: true
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
                    iconName: "camera"
                    iconAbove: root.narrowLayout
                    tabStyle: true
                    leftPadding: ScreenTools.defaultFontPixelWidth * (root.compactLayout ? 0.5 : 1.4)
                    rightPadding: leftPadding
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredWidth: 1
                    implicitHeight: Math.max(ScreenTools.defaultFontPixelHeight * (root.compactLayout ? 2.5 : 3),
                                            ScreenTools.isMobile ? ScreenTools.minTouchPixels : 0,
                                            contentItem.implicitHeight + topPadding + bottomPadding)
                    text: root.narrowLayout ? qsTr("Camera") : qsTr("Camera parameters")
                    Accessible.name: qsTr("Camera parameters")
                    checked: root.selectedPage === 0
                    onClicked: root.selectedPage = 0
                }
                RoukeCameraButton {
                    objectName: "rangeTab"
                    iconName: "range"
                    iconAbove: root.narrowLayout
                    tabStyle: true
                    leftPadding: ScreenTools.defaultFontPixelWidth * (root.compactLayout ? 0.5 : 1.4)
                    rightPadding: leftPadding
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredWidth: 1
                    implicitHeight: Math.max(ScreenTools.defaultFontPixelHeight * (root.compactLayout ? 2.5 : 3),
                                            ScreenTools.isMobile ? ScreenTools.minTouchPixels : 0,
                                            contentItem.implicitHeight + topPadding + bottomPadding)
                    text: root.narrowLayout ? qsTr("Range") : qsTr("Range capture")
                    Accessible.name: qsTr("Range capture")
                    checked: root.selectedPage === 1
                    onClicked: root.selectedPage = 1
                }
            }
        }
        Text {
            Layout.fillWidth: true
            visible: !root.compactLayout
            text: root.selectedPage === 0 ? qsTr("Adjust the camera, then apply each parameter.")
                                         : qsTr("Set capture distance and trigger conditions.")
            color: "#a9bfcc"
            font.family: ScreenTools.normalFontFamily
            font.pointSize: ScreenTools.smallFontPointSize
            wrapMode: Text.WordWrap
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
                    spacing: ScreenTools.defaultFontPixelHeight * 0.15
                    SectionTitle { text: qsTr("EXPOSURE & COLOR"); iconName: "exposure" }
                    Parameter {
                        parameterKey: "iso"
                        title: qsTr("ISO")
                        minimum: 0; maximum: 34; readable: true
                        hint: qsTr("Index 0–34")
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
                        hint: qsTr("Camera index")
                    }
                    Parameter {
                        parameterKey: "aperture"
                        showSeparator: false
                        title: qsTr("Aperture")
                        hint: qsTr("Camera index")
                    }
                    SectionTitle { text: qsTr("FOCUS & LENS"); iconName: "lens" }
                    Parameter {
                        parameterKey: "focusMode"
                        title: qsTr("Focus mode")
                        minimum: 0; maximum: 4
                        options: [ { text: "AF-S", value: 0 }, { text: "AF-A", value: 1 },
                                   { text: "AF-C", value: 2 }, { text: "DMF", value: 3 }, { text: "MF", value: 4 } ]
                    }
                    Parameter {
                        parameterKey: "focusValue"
                        showSeparator: false
                        title: qsTr("Focus value")
                        readable: true
                        hint: qsTr("Raw value · decimal")
                    }
                    SectionTitle { text: qsTr("IMAGE OUTPUT"); iconName: "image" }
                    Parameter {
                        parameterKey: "imageQuality"
                        title: qsTr("Image quality")
                        minimum: 1; maximum: 4
                        options: [ { text: qsTr("Light"), value: 1 }, { text: qsTr("Standard"), value: 2 },
                                   { text: qsTr("Fine"), value: 3 }, { text: qsTr("Extra fine"), value: 4 } ]
                    }
                    Parameter {
                        parameterKey: "imageSize"
                        showSeparator: false
                        title: qsTr("Image size")
                        minimum: 0; maximum: 3; readable: true
                        options: [ { text: qsTr("L · Large"), value: 0 }, { text: qsTr("M · Medium"), value: 1 },
                                   { text: qsTr("S · Small"), value: 2 }, { text: "VGA", value: 3 } ]
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
                    spacing: ScreenTools.defaultFontPixelHeight * 0.15
                    SectionTitle { text: qsTr("TRIGGER CONTROL"); iconName: "camera" }
                    Parameter {
                        parameterKey: "triggerPhoto"
                        showSeparator: false
                        title: qsTr("Trigger capture")
                        minimum: 0; maximum: 1; defaultDraft: 0
                        options: [ { text: qsTr("Off"), value: 0 }, { text: qsTr("On"), value: 1 } ]
                        hint: qsTr("Off by default. Apply to confirm the device setting.")
                    }
                    SectionTitle { text: qsTr("DISTANCE PARAMETERS"); iconName: "range" }
                    Parameter {
                        parameterKey: "shootingDistance"
                        title: qsTr("Shooting distance")
                        minimum: 20; maximum: 40; unit: "m"
                        hint: qsTr("20–40 m")
                    }
                    Parameter {
                        parameterKey: "distanceThreshold"
                        showSeparator: false
                        title: qsTr("Trigger distance threshold")
                        minimum: 30; maximum: 15000; unit: "cm"
                        hint: qsTr("30–15000 cm")
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
                maximumLineCount: 3
                elide: Text.ElideRight
            }
        }
    }
}
