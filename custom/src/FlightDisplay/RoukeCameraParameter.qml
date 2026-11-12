/****************************************************************************
 * One editable parameter. Draft values never stand in for device state.
 ****************************************************************************/
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QtControls
import QGroundControl.ScreenTools

Rectangle {
    id: root
    required property var manager
    required property string parameterKey
    required property string title
    property string hint: ""
    property string inputPlaceholder: qsTr("Enter value")
    property string unit: ""
    property int minimum: 0
    property int maximum: 2147483647
    property var options: []
    property bool readable: false
    // Some commands report a physical value but accept a separate setting code.
    property bool syncReportedValueToDraft: true
    property int defaultDraft: -1
    property int resetSerial: 0
    property bool dirty: false
    property bool showSeparator: true
    readonly property bool known: Boolean(manager && manager.cameraParameters
                                          && manager.cameraParameters[parameterKey] !== undefined)
    readonly property var confirmedValue: known ? manager.cameraParameters[parameterKey] : undefined
    readonly property bool online: Boolean(manager && manager.enabled && manager.sdkResponding)
    readonly property string verification: manager && manager.cameraParameterVerification
                                           ? (manager.cameraParameterVerification[parameterKey] || "") : ""
    readonly property bool verifying: verification === "applying" || verification === "verifying"
    readonly property bool verificationFailed: verification === "mismatch" || verification === "unverified"
    readonly property string statusText: verification === "applying" ? qsTr("Applying…")
                                        : verification === "verifying" ? qsTr("Verifying…")
                                        : verification === "mismatch" ? qsTr("Value differs")
                                        : verification === "unverified" ? qsTr("Not verified")
                                        : verification === "reported" && !pending ? qsTr("Read back")
                                        : pending ? qsTr("Waiting for payload…") : ""
    readonly property string valueStatusText: !known ? qsTr("Not confirmed")
        : !online || verifying || verification === "unverified"
          ? qsTr("Last confirmed: %1").arg(valueText(confirmedValue))
          : qsTr("Confirmed: %1").arg(valueText(confirmedValue))
    readonly property bool busy: Boolean(manager && manager.pending)
    readonly property bool pending: Boolean(busy && manager.pendingParameter === parameterKey)
    readonly property bool showRead: readable && (!known || !online || pending || verifying || verificationFailed)
    // A read can establish the first payload response. Writes still require
    // a responding endpoint, while offline drafts remain editable.
    readonly property bool canRead: Boolean(manager && manager.enabled
                                            && manager.controlAvailable && !busy)
    readonly property bool canControl: Boolean(manager && manager.enabled && manager.sdkResponding
                                               && manager.controlAvailable && !busy)
    readonly property bool hasOptions: options.length > 0
    readonly property var draftValue: hasOptions
                                     ? (choices.currentIndex >= 0 ? options[choices.currentIndex].value : undefined)
                                     : (numeric.text.length && numeric.acceptableInput ? Number(numeric.text) : undefined)
    readonly property bool valid: draftValue !== undefined && isFinite(draftValue)
                                 && Math.floor(draftValue) === draftValue
                                 && draftValue >= minimum && draftValue <= maximum
    readonly property bool wideLayout: width >= ScreenTools.defaultFontPixelWidth * 39
    // Keep the edit group compact while reserving enough room for the full
    // protocol integer and unit at the current application font size.
    readonly property real numericEditorWidth: Math.max(ScreenTools.defaultFontPixelWidth * 13,
                                                        numericMetrics.advanceWidth
                                                        + numeric.leftPadding
                                                        + Math.max(numeric.rightPadding,
                                                                   unitMetrics.advanceWidth + ScreenTools.defaultFontPixelWidth * 2.0)
                                                        + 4)
    readonly property real preferredEditorWidth: Math.max(ScreenTools.defaultFontPixelWidth * 18,
                                                           numericEditorWidth)
    readonly property real editorFontPointSize: ScreenTools.defaultFontPointSize * 1.08
    readonly property bool inlineEditorLayout: wideLayout
            && layout.width >= preferredEditorWidth + actionRow.implicitWidth + ScreenTools.defaultFontPixelWidth * 0.65
    readonly property real inset: ScreenTools.defaultFontPixelHeight * 0.32
    readonly property real horizontalInset: ScreenTools.defaultFontPixelWidth * 0.65
    objectName: "parameter_" + parameterKey
    implicitHeight: layout.implicitHeight + inset * 2
    radius: ScreenTools.defaultFontPixelHeight * 0.4
    color: pending ? "#e9f2f5" : "transparent"

    CameraControlFont { id: controlFont }
    // Property-based metrics update after the resource font loads and whenever
    // application font scaling changes, so long values remain fully visible.
    TextMetrics { id: numericMetrics; font: numeric.font; text: "2147483647" }
    TextMetrics { id: unitMetrics; font: unitLabel.font; text: "cm" }
    component EditorSurface: Rectangle {
        property bool controlEnabled: true
        property bool focused: false
        property bool pointerOver: false
        radius: ScreenTools.defaultFontPixelHeight * 0.35
        color: controlEnabled ? "#dfe8ee" : "#f3f5f7"
        border.width: 1
        border.color: !controlEnabled ? "#dce3e9" : focused ? "#6f9eaf"
                      : pointerOver ? "#a7bfce" : "#c5d3df"
        Behavior on border.color { ColorAnimation { duration: 100 } }
    }
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: root.horizontalInset
        anchors.rightMargin: root.horizontalInset
        height: 1
        visible: root.showSeparator
        color: "#dce3ea"
    }

    function valueText(value) {
        for (let i = 0; i < options.length; ++i) {
            if (options[i].value === Number(value)) return options[i].text
        }
        return String(value) + (unit.length ? " " + unit : "")
    }
    function syncDraft() {
        if (!syncReportedValueToDraft) return
        // A read updates the report beside the editor, never an unsent edit.
        // A successful confirmation only settles a draft that still matches it.
        if (dirty) {
            if (!known || draftValue !== confirmedValue || verifying || verificationFailed) return
            dirty = false
        }
        const value = known ? confirmedValue : defaultDraft >= 0 ? defaultDraft : undefined
        numeric.text = value === undefined ? "" : String(value)
        choices.currentIndex = -1
        for (let i = 0; i < options.length; ++i) {
            if (options[i].value === Number(value)) choices.currentIndex = i
        }
    }
    onConfirmedValueChanged: syncDraft()
    onVerificationChanged: syncDraft()
    onPendingChanged: {
        // A successful reply may report the same device value as before,
        // including when the requested value was adjusted by the camera.
        // Failed writes retain the draft so the operator can retry it.
        if (!pending && manager && !manager.lastError) syncDraft()
    }
    onVisibleChanged: if (!visible) choices.popup.close()
    onResetSerialChanged: choices.popup.close()
    Component.onCompleted: syncDraft()

    ColumnLayout {
        id: layout
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: root.horizontalInset
        anchors.rightMargin: root.horizontalInset
        anchors.topMargin: root.inset
        spacing: ScreenTools.defaultFontPixelHeight * 0.22
        GridLayout {
            Layout.fillWidth: true
            columns: root.wideLayout ? 2 : 1
            columnSpacing: ScreenTools.defaultFontPixelWidth * 0.75
            rowSpacing: ScreenTools.defaultFontPixelHeight * 0.22
            Text {
                Layout.fillWidth: true
                text: root.title
                color: "#152733"
                font.family: controlFont.status === FontLoader.Ready ? controlFont.name : ScreenTools.normalFontFamily
                font.styleName: ""
                font.pointSize: ScreenTools.defaultFontPointSize
                font.weight: Font.DemiBold
                wrapMode: Text.Wrap
            }
            Rectangle {
                Layout.maximumWidth: root.wideLayout ? root.width * 0.58 : layout.width
                Layout.preferredWidth: Math.min(implicitWidth, root.wideLayout ? root.width * 0.58 : layout.width)
                implicitWidth: badgeText.implicitWidth + ScreenTools.defaultFontPixelWidth * 2.6
                implicitHeight: badgeText.implicitHeight + ScreenTools.defaultFontPixelHeight * 0.25
                radius: implicitHeight * 0.5
                color: root.verificationFailed ? "#fff1d9" : root.pending || root.verifying ? "#dcecf1"
                       : root.known && root.online ? "#e1efe9" : "#e9eff4"
                Rectangle {
                    x: ScreenTools.defaultFontPixelWidth * 0.65
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.max(4, ScreenTools.defaultFontPixelHeight * 0.22)
                    height: width
                    radius: width * 0.5
                    color: root.verificationFailed ? "#96621d" : root.pending || root.verifying ? "#176b83"
                           : root.known && root.online ? "#23835f" : "#8b98a3"
                }
                Text {
                    id: badgeText
                    objectName: "confirmed_" + root.parameterKey
                    x: ScreenTools.defaultFontPixelWidth * 1.65
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - x - ScreenTools.defaultFontPixelWidth * 0.65
                    text: (root.statusText.length ? root.statusText + "\n" : "") + root.valueStatusText
                    color: root.verificationFailed ? "#96621d" : root.pending || root.verifying ? "#176b83"
                           : root.known && root.online ? "#236548" : "#3d5262"
                    font.family: ScreenTools.normalFontFamily
                    font.pointSize: ScreenTools.smallFontPointSize
                    font.weight: Font.Medium
                    wrapMode: Text.Wrap
                }
            }
        }
        GridLayout {
            Layout.alignment: Qt.AlignLeft
            Layout.fillWidth: false
            Layout.maximumWidth: layout.width
            columns: root.inlineEditorLayout ? 2 : 1
            columnSpacing: ScreenTools.defaultFontPixelWidth * 0.65
            rowSpacing: ScreenTools.defaultFontPixelHeight * 0.22
            Item {
                Layout.alignment: Qt.AlignLeft
                Layout.preferredWidth: Math.min(root.preferredEditorWidth, layout.width)
                Layout.minimumWidth: Layout.preferredWidth
                Layout.maximumWidth: Layout.preferredWidth
                Layout.preferredHeight: applyButton.implicitHeight
                QtControls.TextField {
                    id: numeric
                    objectName: "editor_" + root.parameterKey
                    Accessible.name: root.title
                    anchors.fill: parent
                    visible: !root.hasOptions
                    enabled: !root.busy
                    placeholderText: root.inputPlaceholder
                    color: enabled ? "#152733" : "#8b98a3"
                    placeholderTextColor: "#3d5262"
                    selectionColor: "#c5e1e9"
                    selectedTextColor: "#152733"
                    font.family: controlFont.status === FontLoader.Ready ? controlFont.name : ScreenTools.normalFontFamily
                    font.styleName: ""
                    font.pointSize: root.editorFontPointSize
                    font.weight: Font.DemiBold
                    hoverEnabled: true
                    leftPadding: ScreenTools.defaultFontPixelWidth * 1.2
                    rightPadding: root.unit.length ? unitLabel.implicitWidth + ScreenTools.defaultFontPixelWidth * 2.0
                                                  : ScreenTools.defaultFontPixelWidth
                    Text {
                        id: unitLabel
                        anchors.right: parent.right
                        anchors.rightMargin: ScreenTools.defaultFontPixelWidth * 1.2
                        anchors.verticalCenter: parent.verticalCenter
                        visible: root.unit.length > 0
                        text: root.unit
                        color: "#3d5262"
                        font.family: ScreenTools.normalFontFamily
                        font.pointSize: ScreenTools.smallFontPointSize
                        font.weight: Font.Medium
                    }
                    inputMethodHints: Qt.ImhDigitsOnly
                    validator: IntValidator { bottom: root.minimum; top: root.maximum }
                    onTextEdited: root.dirty = true
                    background: EditorSurface {
                        controlEnabled: numeric.enabled
                        focused: numeric.activeFocus
                        pointerOver: numeric.hovered
                    }
                }
                QtControls.ComboBox {
                    id: choices
                    objectName: "choice_" + root.parameterKey
                    Accessible.name: root.title
                    anchors.fill: parent
                    visible: root.hasOptions
                    enabled: !root.busy
                    model: root.options
                    textRole: "text"
                    currentIndex: -1
                    onActivated: root.dirty = true
                    font.family: controlFont.status === FontLoader.Ready ? controlFont.name : ScreenTools.normalFontFamily
                    font.styleName: ""
                    font.pointSize: root.editorFontPointSize
                    font.weight: Font.DemiBold
                    hoverEnabled: true
                    contentItem: Text {
                        leftPadding: ScreenTools.defaultFontPixelWidth * 1.2
                        rightPadding: ScreenTools.defaultFontPixelHeight * 1.4
                        text: choices.currentIndex < 0 ? qsTr("Select…") : choices.displayText
                        color: !choices.enabled ? "#8b98a3" : choices.currentIndex < 0 ? "#3d5262" : "#152733"
                        font: choices.font
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                    indicator: CameraControlIcon {
                        width: ScreenTools.defaultFontPixelHeight * 0.8
                        height: width
                        x: choices.width - width - ScreenTools.defaultFontPixelWidth
                        anchors.verticalCenter: parent.verticalCenter
                        kind: "chevronDown"
                        color: choices.enabled ? "#3d5262" : "#8b98a3"
                    }
                    background: EditorSurface {
                        controlEnabled: choices.enabled
                        focused: choices.activeFocus
                        pointerOver: choices.hovered
                    }
                    delegate: QtControls.ItemDelegate {
                        required property var modelData
                        required property int index
                        width: choices.width
                        height: applyButton.implicitHeight
                        highlighted: choices.highlightedIndex === index
                        contentItem: Text {
                            text: modelData.text
                            color: "#152733"
                            font: choices.font
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }
                        background: Rectangle {
                            radius: ScreenTools.defaultFontPixelHeight * 0.25
                            color: parent.highlighted ? "#dcecf1" : "#ffffff"
                        }
                    }
                    popup: QtControls.Popup {
                        y: choices.height + 4
                        width: choices.width
                        padding: 4
                        implicitHeight: Math.min(contentItem.implicitHeight + 8, ScreenTools.defaultFontPixelHeight * 13)
                        contentItem: ListView {
                            clip: true
                            implicitHeight: contentHeight
                            model: choices.popup.visible ? choices.delegateModel : null
                            currentIndex: choices.highlightedIndex
                            QtControls.ScrollIndicator.vertical: QtControls.ScrollIndicator {
                                contentItem: Rectangle {
                                    implicitWidth: 4
                                    implicitHeight: 20
                                    radius: 2
                                    color: "#8b98a3"
                                }
                            }
                        }
                        background: Rectangle { color: "#ffffff"; radius: 6; border.color: "#176b83" }
                    }
                }
            }
            RowLayout {
                id: actionRow
                Layout.alignment: Qt.AlignLeft
                spacing: ScreenTools.defaultFontPixelWidth * 0.65
                // Keep Apply aligned when a confirmed or write-only parameter
                // has no read action, without stretching the field or buttons.
                Item {
                    Layout.preferredWidth: applyButton.implicitHeight
                    Layout.preferredHeight: applyButton.implicitHeight
                    RoukeCameraButton {
                        objectName: "read_" + root.parameterKey
                        anchors.fill: parent
                        visible: root.showRead
                        compact: true
                        text: qsTr("Read")
                        iconName: "read"
                        iconOnly: true
                        subtle: true
                        enabled: root.canRead
                        onClicked: {
                            root.manager.requestCameraParameter(root.parameterKey)
                        }
                    }
                }
                RoukeCameraButton {
                    id: applyButton
                    objectName: "apply_" + root.parameterKey
                    compact: true
                    text: qsTr("Apply")
                    iconName: "apply"
                    primary: root.dirty || !root.known
                    enabled: root.canControl && root.valid
                    onClicked: {
                        root.manager.setCameraParameter(root.parameterKey, root.draftValue)
                    }
                }
            }
        }
        Text {
            objectName: "hint_" + root.parameterKey
            Layout.fillWidth: true
            visible: root.hint.length > 0 || !root.readable
            text: root.hint + (!root.readable
                              ? (root.hint.length ? " · " : "") + qsTr("Set only; no read command.")
                              : "")
            color: "#3d5262"
            font.family: ScreenTools.normalFontFamily
            font.pointSize: ScreenTools.smallFontPointSize
            font.weight: Font.Medium
            wrapMode: Text.WordWrap
        }
    }
}
