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
                    gimbalCameraControlLoader.visible && gimbalCameraControlLoader.item
                    ? gimbalCameraControlLoader.item.implicitWidth
                    : 0)

    property var _activeVehicle: globals.activeVehicle
    property var _gimbalManager: QGroundControl.corePlugin ? QGroundControl.corePlugin.gimbalControlManager : null
    property bool _usePrivateCameraControl: Boolean(_gimbalManager && _gimbalManager.enabled)

    TerrainProgress {
        Layout.alignment:       Qt.AlignTop | Qt.AlignRight
        Layout.preferredWidth:  _rightPanelWidth
    }

    // 新增 QML 未注册到原生 FlightDisplay 模块，必须通过 custom QRC 地址显式加载。
    // preferredWidth/Height 跟随实际控件，避免 Loader 在布局中形成零尺寸透明占位。
    Loader {
        id:                     gimbalCameraControlLoader
        active:                 root._usePrivateCameraControl
        visible:                Boolean(active && item && item.online)
        source:                 "qrc:/Custom/qml/QGroundControl/FlightDisplay/GimbalCameraControl.qml"
        Layout.alignment:       Qt.AlignTop | Qt.AlignRight
        Layout.preferredWidth:  visible && item ? item.implicitWidth : 0
        Layout.preferredHeight: visible && item ? item.implicitHeight : 0
        Layout.minimumWidth:    Layout.preferredWidth
        Layout.minimumHeight:   Layout.preferredHeight

        property real rightEdgeCenterInset: visible ? parent.width - x : 0

        onStatusChanged: {
            if (status === Loader.Error) {
                console.warn("Gimbal camera control failed to load:", source)
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
