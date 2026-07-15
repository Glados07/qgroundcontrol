/****************************************************************************
 *
 * (c) 2009-2020 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

import QtQml.Models

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FlightDisplay

ToolStripActionList {
    id: _root

    signal displayPreFlightChecklist

    model: [
        // Viewer3D 入口按钮：这是本次迁移新增项；其余 GuidedAction 保持目标项目原有顺序和行为。
        ToolStripAction {
            property bool _is3DViewOpen:      viewer3DWindow.isOpen
            property bool _viewer3DEnabled:   QGroundControl.corePlugin.viewer3DSettings.enabled.rawValue

            id:             view3DIcon
            visible:        _viewer3DEnabled
            text:           _is3DViewOpen ? qsTr("Fly") : qsTr("3D View")
            iconSource:     _is3DViewOpen ? "qrc:/qmlimages/PaperPlane.svg" : "qrc:/Custom/qmlimages/Viewer3D/City3DMapIcon.svg"
            fullColorIcon:  !_is3DViewOpen

            onTriggered: {
                if (_is3DViewOpen) {
                    viewer3DWindow.close()
                } else {
                    viewer3DWindow.open()
                }
            }
        },
        PreFlightCheckListShowAction { onTriggered: displayPreFlightChecklist() },
        GuidedActionTakeoff { },
        GuidedActionLand { },
        GuidedActionRTL { },
        GuidedActionPause { },
        FlyViewAdditionalActionsButton { },
        GuidedActionGripper { }
    ]
}
