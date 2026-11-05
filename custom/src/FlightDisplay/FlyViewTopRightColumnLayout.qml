/****************************************************************************
 *
 * Fly View 右侧相机控制区域覆盖。
 * 启用 A8 Mini 或柔克负载时，加载对应的相机控制栏；
 * 两个模块均关闭时自动回退 QGC 原生 PhotoVideoControl。
 *
 ****************************************************************************/

import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FlightDisplay
import QGroundControl.FlightMap
import QGroundControl.ScreenTools
import Custom.FlightDisplay as CustomFlightDisplay

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
    property var _roukeManager: QGroundControl.corePlugin
                              ? QGroundControl.corePlugin.roukeControlManager
                              : null
    property bool _a8Enabled: Boolean(_gimbalManager && _gimbalManager.enabled)
    property bool _roukeEnabled: Boolean(_roukeManager && _roukeManager.enabled)
    property bool _usePrivateCameraControl: _a8Enabled || _roukeEnabled
    property int _selectedCamera: 0 // 0: A8 Mini, 1: Rouke
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
        if (_selectedCamera === 0 && !_a8Enabled && _roukeEnabled) {
            selectCamera(1)
        } else if (_selectedCamera === 1 && !_roukeEnabled && _a8Enabled) {
            selectCamera(0)
        }
    }

    on_A8EnabledChanged: normalizeSelectedCamera()
    on_RoukeEnabledChanged: normalizeSelectedCamera()
    Component.onCompleted: normalizeSelectedCamera()

    TerrainProgress {
        Layout.alignment:       Qt.AlignTop | Qt.AlignRight
        Layout.preferredWidth:  _rightPanelWidth
    }

    // 选择器通过 Custom.FlightDisplay 导出；相机面板继续用显式 custom QRC 加载。
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

        CustomFlightDisplay.CameraPayloadSelector {
            id: cameraSelector
            visible: root._a8Enabled && root._roukeEnabled
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: implicitWidth
            Layout.preferredHeight: implicitHeight
            selectedCamera: root._selectedCamera
            a8Online: Boolean(root._gimbalManager && root._gimbalManager.sdkResponding)
            roukeOnline: Boolean(root._roukeManager && root._roukeManager.sdkResponding)
            onCameraSelected: function(cameraIndex) { root.selectCamera(cameraIndex) }
        }

        Loader {
            id: cameraControlLoader

            active: root._usePrivateCameraControl
            visible: Boolean(active && item)
            source: root._selectedCamera === 1
                    ? "qrc:/Custom/qml/QGroundControl/FlightDisplay/RoukeCameraControl.qml"
                    : "qrc:/Custom/qml/QGroundControl/FlightDisplay/GimbalCameraControl.qml"
            Layout.alignment: Qt.AlignTop | Qt.AlignHCenter
            Layout.preferredWidth: item ? item.implicitWidth : 0
            Layout.preferredHeight: item ? item.implicitHeight : 0
            Layout.minimumWidth: Layout.preferredWidth
            Layout.minimumHeight: Layout.preferredHeight

            onLoaded: {
                // Keep the old item's manager stable while Loader destroys it.
                // Its teardown can still cancel A8 continuous zoom after the
                // selected camera has already changed to Rouke.
                const loadedCameraIndex = root._selectedCamera
                item.manager = Qt.binding(function() {
                    return loadedCameraIndex === 1 ? root._roukeManager : root._gimbalManager
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
