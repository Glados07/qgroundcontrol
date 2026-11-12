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
    background: Item {
        Rectangle {
            anchors.fill: parent
            anchors.topMargin: 3
            anchors.bottomMargin: -3
            radius: ScreenTools.defaultFontPixelHeight * 0.75
            color: "#180e2635"
        }
        Rectangle {
            anchors.fill: parent
            radius: ScreenTools.defaultFontPixelHeight * 0.75
            color: "#edf2f6"
            border.color: "#bbc9d3"
        }
    }
    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 120 } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 90 } }

    component SectionTitle: Item {
        id: sectionRoot
        property string text: ""
        property string iconName: "exposure"
        width: parent.width
        implicitHeight: Math.max(ScreenTools.defaultFontPixelHeight * 1.65,
                                 sectionText.implicitHeight + ScreenTools.defaultFontPixelHeight * 0.3)
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: ScreenTools.defaultFontPixelWidth * 0.65
            anchors.rightMargin: anchors.leftMargin
            spacing: ScreenTools.defaultFontPixelWidth * 0.8
            Rectangle {
                Layout.preferredWidth: ScreenTools.defaultFontPixelHeight * 1.2
                Layout.preferredHeight: ScreenTools.defaultFontPixelHeight * 1.2
                radius: ScreenTools.defaultFontPixelHeight * 0.35
                color: "#e5f0f2"
                CameraControlIcon {
                    anchors.centerIn: parent
                    width: ScreenTools.defaultFontPixelHeight * 0.8
                    height: width
                    kind: sectionRoot.iconName
                    color: "#155b70"
                }
            }
            Text {
                id: sectionText
                Layout.fillWidth: true
                text: sectionRoot.text
                color: "#213b4b"
                font.family: ScreenTools.normalFontFamily
                font.pointSize: ScreenTools.smallFontPointSize
                font.weight: Font.Bold
                wrapMode: Text.Wrap
            }
        }
    }
    component Parameter: RoukeCameraParameter {
        width: parent.width
        manager: root.manager
        resetSerial: root.resetSerial
    }
    component ParameterGroup: Rectangle {
        id: group
        property string title: ""
        property string iconName: "exposure"
        default property alias parameters: parameterColumn.data
        readonly property real innerInset: ScreenTools.defaultFontPixelHeight * 0.25
        width: parent.width
        implicitHeight: groupContent.implicitHeight + innerInset * 2
        radius: ScreenTools.defaultFontPixelHeight * 0.6
        color: "#ffffff"
        border.color: "#d4dfe7"
        data: [
            Column {
                id: groupContent
                x: group.innerInset
                y: group.innerInset
                width: group.width - group.innerInset * 2
                SectionTitle { text: group.title; iconName: group.iconName }
                Column {
                    id: parameterColumn
                    width: parent.width
                }
            }
        ]
    }
    component ParameterScrollBar: QtControls.ScrollBar {
        id: scrollBar
        policy: QtControls.ScrollBar.AsNeeded
        visible: size < 1
        padding: 1
        contentItem: Rectangle {
            implicitWidth: 5
            implicitHeight: 30
            radius: width / 2
            color: scrollBar.pressed ? "#176b83" : scrollBar.hovered ? "#536777" : "#a5b4c1"
        }
        background: Rectangle {
            implicitWidth: 7
            radius: width / 2
            color: "#e9eff4"
        }
    }
    contentItem: ColumnLayout {
        spacing: ScreenTools.defaultFontPixelHeight * 0.35
        RowLayout {
            Layout.fillWidth: true
            spacing: ScreenTools.defaultFontPixelHeight * 0.5
            Rectangle {
                Layout.preferredWidth: ScreenTools.defaultFontPixelHeight * 1.5
                Layout.preferredHeight: ScreenTools.defaultFontPixelHeight * 1.5
                radius: ScreenTools.defaultFontPixelHeight * 0.45
                color: "#d9e8ee"
                CameraControlIcon {
                    anchors.centerIn: parent
                    width: ScreenTools.defaultFontPixelHeight * 0.95
                    height: width
                    kind: "settings"
                    color: "#153f50"
                }
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("Payload settings")
                color: "#152733"
                font.family: ScreenTools.normalFontFamily
                font.pointSize: ScreenTools.defaultFontPointSize
                font.weight: Font.Bold
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
            radius: ScreenTools.defaultFontPixelHeight * 0.6
            color: "#dfe8ee"
            border.color: "#d2dde5"
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
                QtControls.ScrollBar.vertical: ParameterScrollBar {}
                Column {
                    id: cameraColumn
                    width: parent.width - ScreenTools.defaultFontPixelWidth * 1.3
                    spacing: ScreenTools.defaultFontPixelHeight * 0.45
                    ParameterGroup {
                        title: qsTr("EXPOSURE & COLOR")
                        iconName: "exposure"
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
                    }
                    ParameterGroup {
                        title: qsTr("FOCUS & LENS")
                        iconName: "lens"
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
                    }
                    ParameterGroup {
                        title: qsTr("IMAGE OUTPUT")
                        iconName: "image"
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
                    }
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
                QtControls.ScrollBar.vertical: ParameterScrollBar {}
                Column {
                    id: rangeColumn
                    width: parent.width - ScreenTools.defaultFontPixelWidth * 1.3
                    spacing: ScreenTools.defaultFontPixelHeight * 0.45
                    ParameterGroup {
                        title: qsTr("TRIGGER CONTROL")
                        iconName: "camera"
                        Parameter {
                            parameterKey: "triggerPhoto"
                            showSeparator: false
                            title: qsTr("Trigger capture")
                            minimum: 0; maximum: 1; defaultDraft: 0
                            options: [ { text: "0 · " + qsTr("Off"), value: 0 }, { text: "1 · " + qsTr("On"), value: 1 } ]
                            hint: qsTr("0 = Off, 1 = On. Off by default; apply to confirm.")
                        }
                    }
                    ParameterGroup {
                        title: qsTr("DISTANCE PARAMETERS")
                        iconName: "range"
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
                    }
                }
            }
        }
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: "#dce3ea" }
        RowLayout {
            Layout.fillWidth: true
            spacing: ScreenTools.defaultFontPixelWidth
            Rectangle {
                implicitWidth: 7; implicitHeight: 7; radius: 4
                color: root.manager && root.manager.pending ? "#96621d" : root.connected ? "#23835f" : "#8795a3"
            }
            Text {
                Layout.fillWidth: true
                text: root.manager && root.manager.lastError ? root.manager.lastError
                      : root.manager && root.manager.pending ? qsTr("Waiting for payload…")
                      : root.connected ? qsTr("Payload connected") : qsTr("Awaiting payload response")
                color: root.manager && root.manager.lastError ? "#855313" : "#3d5262"
                font.family: ScreenTools.normalFontFamily
                font.pointSize: ScreenTools.smallFontPointSize
                font.weight: Font.Medium
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }
        }
    }
}
