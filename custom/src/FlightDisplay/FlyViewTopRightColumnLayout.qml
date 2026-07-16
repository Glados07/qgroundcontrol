/****************************************************************************
 *
 * Fly View 右侧相机控制区域覆盖。
 * 启用思翼私有 SDK 缩放模块时，用 GimbalZoomControl 替换 QGC 原生
 * PhotoVideoControl；关闭模块时自动回退原生相机控件。
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

    width: _rightPanelWidth

    property var _activeVehicle: globals.activeVehicle
    property var _gimbalSettings: QGroundControl.corePlugin ? QGroundControl.corePlugin.gimbalControlSettings : null
    property bool _usePrivateZoomControl: Boolean(_activeVehicle &&
                                                  _gimbalSettings &&
                                                  _gimbalSettings.enabled &&
                                                  _gimbalSettings.enabled.rawValue)

    TerrainProgress {
        Layout.alignment:       Qt.AlignTop
        Layout.preferredWidth:  _rightPanelWidth
    }

    // 新增 QML 未注册到原生 FlightDisplay 模块，必须通过 custom QRC 地址显式加载。
    // preferredWidth/Height 跟随实际控件，避免 Loader 在布局中形成零尺寸透明占位。
    Loader {
        id:                     gimbalZoomControlLoader
        active:                 root._usePrivateZoomControl
        visible:                active
        source:                 "qrc:/Custom/qml/QGroundControl/FlightDisplay/GimbalZoomControl.qml"
        Layout.alignment:       Qt.AlignTop | Qt.AlignRight
        Layout.preferredWidth:  item ? item.implicitWidth : 0
        Layout.preferredHeight: item ? item.implicitHeight : 0

        property real rightEdgeCenterInset: visible ? parent.width - x : 0

        onStatusChanged: {
            if (status === Loader.Error) {
                console.warn("Gimbal zoom control failed to load:", source)
            }
        }
    }

    // 模块关闭时保留 QGC 原生拍照/录像功能，避免影响其他相机使用场景。
    Loader {
        id:                     photoVideoControlLoader
        active:                 Boolean(root._activeVehicle && !root._usePrivateZoomControl)
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
