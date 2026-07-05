/****************************************************************************
 *
 * Custom Fly View tool strip for SecDev Viewer3D build.
 * 该文件显式加载 custom 的 FlyViewToolStripActionList，避免 Qt6 模块解析回到 src 旧文件。
 *
****************************************************************************/

import QtQuick

import QGroundControl.Controls

ToolStrip {
    id: _root

    signal displayPreFlightChecklist

    Viewer3DToolStripActionList {
        id: actionList

        onDisplayPreFlightChecklist: _root.displayPreFlightChecklist()
    }

    model: actionList.model
}
