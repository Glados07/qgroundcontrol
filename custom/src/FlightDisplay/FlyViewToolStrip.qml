/****************************************************************************
 *
 * Custom Fly View tool strip for SecDev Viewer3D build.
 * 该文件通过 custom.qrc 覆盖为 QGroundControl.FlightDisplay/FlyViewToolStrip.qml。
 * 内部继续使用 QGC 已注册的 FlyViewToolStripActionList 类型，由拦截器加载 custom 覆盖文件。
 *
****************************************************************************/

import QtQuick

import QGroundControl.Controls

ToolStrip {
    id: _root

    signal displayPreFlightChecklist

    FlyViewToolStripActionList {
        id: actionList

        onDisplayPreFlightChecklist: _root.displayPreFlightChecklist()
    }

    model: actionList.model
}
