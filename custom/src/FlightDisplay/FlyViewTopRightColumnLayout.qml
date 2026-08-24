/****************************************************************************
 *
 * Fly View 右侧相机控制区域覆盖。
 * 启用思翼私有 SDK 相机模块时，加载合并后的缩放/拍照/录像控制栏；
 * 关闭模块时自动回退 QGC 原生 PhotoVideoControl。
 *
 ****************************************************************************/

import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FlightDisplay
import QGroundControl.FlightMap
import QGroundControl.ScreenTools

ColumnLayout {
    id: root

    width: Math.max(_rightPanelWidth,
                    privateCameraControlLayout.visible
                    ? privateCameraControlLayout.implicitWidth
                    : 0)

    property var _activeVehicle: globals.activeVehicle
    property var _gimbalManager: QGroundControl.corePlugin
                                 ? QGroundControl.corePlugin.gimbalControlManager
                                 : null
    property var _mt11Manager: QGroundControl.corePlugin
                              ? QGroundControl.corePlugin.mt11ControlManager
                              : null
    property bool _a8Enabled: Boolean(_gimbalManager && _gimbalManager.enabled)
    property bool _mt11Enabled: Boolean(_mt11Manager && _mt11Manager.enabled)
    property bool _usePrivateCameraControl: _a8Enabled || _mt11Enabled
    property int _selectedCamera: 0 // 0: A8 Mini, 1: MT11
    readonly property real _cameraSelectorHeight: Math.max(
                                                      ScreenTools.defaultFontPixelHeight * 2.25,
                                                      ScreenTools.isMobile
                                                      ? ScreenTools.minTouchPixels
                                                      : 0)
    readonly property real _cameraSelectorTabWidth: Math.max(
                                                        _cameraSelectorHeight * 1.55,
                                                        ScreenTools.defaultFontPixelWidth * 9.2)
    readonly property color _cameraAccentColor: "#65d9f4"

    function selectCamera(cameraIndex) {
        if (_selectedCamera === cameraIndex) {
            return
        }

        if (cameraControlLoader.item
                && typeof cameraControlLoader.item.closeTransientUi === "function") {
            cameraControlLoader.item.closeTransientUi()
        }
        _selectedCamera = cameraIndex
    }

    function normalizeSelectedCamera() {
        if (_selectedCamera === 0 && !_a8Enabled && _mt11Enabled) {
            selectCamera(1)
        } else if (_selectedCamera === 1 && !_mt11Enabled && _a8Enabled) {
            selectCamera(0)
        }
    }

    on_A8EnabledChanged: normalizeSelectedCamera()
    on_Mt11EnabledChanged: normalizeSelectedCamera()
    Component.onCompleted: normalizeSelectedCamera()

    TerrainProgress {
        Layout.alignment:       Qt.AlignTop | Qt.AlignRight
        Layout.preferredWidth:  _rightPanelWidth
    }

    // 新增 QML 未注册到原生 FlightDisplay 模块，必须通过 custom QRC 地址显式加载。
    // 启用私有相机模块后始终显示控制栏；SDK在线状态只控制按钮可用性，不再控制可见性。
    // preferredWidth/Height 跟随纵向控件的完整隐式尺寸，避免 Loader 在布局中
    // 形成零尺寸透明占位或把整栏压缩成单个按钮高度。
    ColumnLayout {
        id: privateCameraControlLayout

        visible: root._usePrivateCameraControl
        Layout.alignment: Qt.AlignTop | Qt.AlignRight
        Layout.preferredWidth: implicitWidth
        Layout.preferredHeight: implicitHeight
        spacing: ScreenTools.defaultFontPixelHeight * 0.35

        property real rightEdgeCenterInset: visible ? parent.width - x : 0

        Rectangle {
            id: cameraSelector

            visible: root._a8Enabled && root._mt11Enabled
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: root._cameraSelectorTabWidth * 2
                                   + cameraSelector.innerMargin * 2
                                   + cameraSelector.tabSpacing
            Layout.preferredHeight: root._cameraSelectorHeight
                                    + cameraSelector.innerMargin * 2
            radius: height / 2
            color: "#dc121a24"
            border.color: "#566f8290"
            border.width: 1

            readonly property real innerMargin: Math.max(
                                                    2,
                                                    root._cameraSelectorHeight * 0.055)
            readonly property real tabSpacing: Math.max(2, innerMargin * 0.65)

            Rectangle {
                anchors.fill: parent
                anchors.margins: 1
                radius: Math.max(0, parent.radius - 1)
                color: "transparent"
                border.color: "#1effffff"
                border.width: 1
            }

            Row {
                anchors.fill: parent
                anchors.margins: cameraSelector.innerMargin
                spacing: cameraSelector.tabSpacing

                Repeater {
                    model: [0, 1]

                    Rectangle {
                        id: cameraTab

                        required property int modelData
                        readonly property int cameraIndex: modelData
                        readonly property bool selected: root._selectedCamera === cameraIndex
                        readonly property var cameraManager: cameraIndex === 0
                                                                     ? root._gimbalManager
                                                                     : root._mt11Manager
                        readonly property bool cameraOnline: Boolean(cameraManager
                                                                     && cameraManager.sdkResponding)

                        width: root._cameraSelectorTabWidth
                        height: root._cameraSelectorHeight
                        radius: height / 2
                        color: selected
                               ? "#40327182"
                               : (cameraTabMouse.pressed
                                  ? "#e8f2f7fa"
                                  : (cameraTabMouse.containsMouse ? "#30364854" : "transparent"))
                        border.color: selected
                                      ? root._cameraAccentColor
                                      : (cameraTabMouse.containsMouse ? "#7597a9b5" : "transparent")
                        border.width: selected ? 2 : 1
                        scale: cameraTabMouse.pressed ? 0.97 : 1.0

                        Behavior on color { ColorAnimation { duration: 110 } }
                        Behavior on scale { NumberAnimation { duration: 85 } }

                        Row {
                            anchors.centerIn: parent
                            spacing: ScreenTools.defaultFontPixelWidth * 0.55

                            Rectangle {
                                anchors.verticalCenter: parent.verticalCenter
                                width: Math.max(6, root._cameraSelectorHeight * 0.14)
                                height: width
                                radius: width / 2
                                color: cameraTab.cameraOnline ? "#58df8b" : "#7e8b95"
                                border.color: "#b8ffffff"
                                border.width: 1
                            }

                            QGCLabel {
                                anchors.verticalCenter: parent.verticalCenter
                                text: cameraTab.cameraIndex === 0 ? qsTr("A8 Mini") : qsTr("MT11")
                                color: cameraTabMouse.pressed && !cameraTab.selected
                                       ? "#15212a"
                                       : (cameraTab.selected ? "white" : "#d4e0e6ea")
                                font.bold: cameraTab.selected
                                font.pointSize: ScreenTools.smallFontPointSize
                            }
                        }

                        MouseArea {
                            id: cameraTabMouse

                            anchors.fill: parent
                            hoverEnabled: true
                            preventStealing: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.selectCamera(cameraTab.cameraIndex)
                        }
                    }
                }
            }
        }

        Loader {
            id: cameraControlLoader

            active: root._usePrivateCameraControl
            visible: Boolean(active && item)
            source: root._selectedCamera === 1
                    ? "qrc:/Custom/qml/QGroundControl/FlightDisplay/MT11CameraControl.qml"
                    : "qrc:/Custom/qml/QGroundControl/FlightDisplay/GimbalCameraControl.qml"
            Layout.alignment: Qt.AlignTop | Qt.AlignHCenter
            Layout.preferredWidth: item ? item.implicitWidth : 0
            Layout.preferredHeight: item ? item.implicitHeight : 0
            Layout.minimumWidth: Layout.preferredWidth
            Layout.minimumHeight: Layout.preferredHeight

            readonly property var selectedManager: root._selectedCamera === 1
                                                   ? root._mt11Manager
                                                   : root._gimbalManager

            onLoaded: {
                item.manager = Qt.binding(function() {
                    return cameraControlLoader.selectedManager
                })
            }

            onStatusChanged: {
                if (status === Loader.Error) {
                    console.warn("Camera control failed to load:", source)
                }
            }
        }
    }

    // 模块关闭时保留 QGC 原生拍照/录像功能，避免影响其他相机使用场景。
    Loader {
        id:                     photoVideoControlLoader
        active:                 Boolean(root._activeVehicle && !root._usePrivateCameraControl)
        visible:                active
        sourceComponent:        active ? photoVideoControlComponent : undefined
        Layout.alignment:       Qt.AlignTop | Qt.AlignRight

        property real rightEdgeCenterInset: visible ? parent.width - x : 0

        Component {
            id: photoVideoControlComponent

            PhotoVideoControl {
            }
        }
    }
}
