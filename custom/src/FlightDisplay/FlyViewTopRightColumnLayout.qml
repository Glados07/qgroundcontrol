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

    // 静态创建缩放控件，避免 Ubuntu/Qt 6 下 Loader 初始尺寸为 0 时只留下透明占位。
    GimbalZoomControl {
        id:                     gimbalZoomControl
        visible:                root._usePrivateZoomControl
        Layout.alignment:       Qt.AlignTop | Qt.AlignRight
        Layout.preferredWidth:  implicitWidth
        Layout.preferredHeight: implicitHeight

        property real rightEdgeCenterInset: visible ? parent.width - x : 0
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
