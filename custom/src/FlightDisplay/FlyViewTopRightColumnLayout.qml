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

    function normalizeSelectedCamera() {
        if (_selectedCamera === 0 && !_a8Enabled && _mt11Enabled) {
            _selectedCamera = 1
        } else if (_selectedCamera === 1 && !_mt11Enabled && _a8Enabled) {
            _selectedCamera = 0
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

        RowLayout {
            visible: root._a8Enabled && root._mt11Enabled
            Layout.alignment: Qt.AlignHCenter
            spacing: ScreenTools.defaultFontPixelWidth * 0.35

            QGCButton {
                text: qsTr("A8 Mini")
                primary: root._selectedCamera === 0
                heightFactor: 0.25
                leftPadding: ScreenTools.defaultFontPixelWidth
                rightPadding: ScreenTools.defaultFontPixelWidth
                pointSize: ScreenTools.smallFontPointSize
                onClicked: root._selectedCamera = 0
            }

            QGCButton {
                text: qsTr("MT11")
                primary: root._selectedCamera === 1
                heightFactor: 0.25
                leftPadding: ScreenTools.defaultFontPixelWidth
                rightPadding: ScreenTools.defaultFontPixelWidth
                pointSize: ScreenTools.smallFontPointSize
                onClicked: root._selectedCamera = 1
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
