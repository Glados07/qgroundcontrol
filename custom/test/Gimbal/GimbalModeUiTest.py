"""Qt 6 regression of the production toolbar's mode bindings, not a full APK.

Run with PySide6: python custom/test/Gimbal/GimbalModeUiTest.py
The exact status/button expressions are extracted from GimbalIndicator.qml;
only the application singleton and its state objects are replaced by doubles.
"""
import os
from pathlib import Path
import re

import PySide6

os.environ["QT_PLUGIN_PATH"] = str(Path(PySide6.__file__).parent / "plugins")
from PySide6.QtCore import QCoreApplication, QUrl
from PySide6.QtQml import QQmlComponent, QQmlEngine

ROOT = Path(__file__).resolve().parents[3]
source = (ROOT / "custom/src/UI/toolbar/GimbalIndicator.qml").read_text(encoding="utf-8")
bindings = source.split("    readonly property var _gimbalModeController:", 1)[1].split(
    "    readonly property var _gimbalCenterCoordinator:", 1)[0]
bindings = "readonly property var _gimbalModeController:" + bindings
bindings = bindings.replace("QGroundControl.corePlugin", "app.corePlugin")
status = re.search(r"id:\s+statusLabel\s+text:\s+(.+?)\s+Layout.columnSpan:", source, re.S).group(1)
button = re.search(r'\{id: "yawLock",\s+text: (.+), visible: true \}', source).group(1)
enabled = re.search(r'enabled: (modelData.id !== "yawLock"[^\n]+)', source).group(1)
enabled = enabled.replace("modelData.id", '"yawLock"')
qml = """
import QtQml
QtObject {
    id: control
    property QtObject actualGimbal: QtObject { property bool retracted: false; property bool yawLock: false }
    property QtObject otherGimbal: QtObject { property bool retracted: false; property bool yawLock: false }
    property var activeGimbal: actualGimbal
    property bool sourceKnown: false
    property bool sourceLocked: false
    property int sourceMode: 0
    property QtObject modeSource: QtObject {
        property bool known: control.sourceKnown
        property bool yawLocked: control.sourceLocked
        property int mode: control.sourceMode
        property var gimbal: control.actualGimbal
    }
    property QtObject app: QtObject {
        property QtObject corePlugin: QtObject { property var gimbalModeController: control.modeSource }
    }
    BINDINGS
    property string statusText: STATUS
    property string buttonText: BUTTON
    property bool buttonEnabled: ENABLED
}
""".replace("BINDINGS", bindings).replace("STATUS", status).replace("BUTTON", button).replace("ENABLED", enabled)

application = QCoreApplication([])
engine = QQmlEngine()
engine.setImportPathList([str(Path(PySide6.__file__).parent / "qml"), "qrc:/qt/qml"])
component = QQmlComponent(engine)
component.setData(qml.encode(), QUrl("GimbalModeBindings.qml"))
assert not component.isError(), [error.toString() for error in component.errors()]
item = component.create()
assert item, [error.toString() for error in component.errors()]


def check(status_text, button_text, available):
    application.processEvents()
    assert item.property("statusText") == status_text, item.property("statusText")
    assert item.property("buttonText") == button_text, item.property("buttonText")
    assert item.property("buttonEnabled") == available


check("Mode syncing", "Syncing <br> mode", False)
item.setProperty("sourceLocked", True)
item.setProperty("sourceMode", 2)
item.setProperty("sourceKnown", True)
check("Yaw locked", "Yaw <br> Follow", True)  # native yawLock still false
item.setProperty("sourceKnown", False)
check("Mode syncing", "Syncing <br> mode", False)
item.setProperty("sourceKnown", True)
check("Yaw locked", "Yaw <br> Follow", True)
item.setProperty("sourceLocked", False)
item.setProperty("sourceMode", 1)
check("Yaw follow", "Yaw <br> Lock", True)
item.setProperty("sourceMode", 3)
check("FPV", "Yaw <br> Lock", True)
item.setProperty("activeGimbal", item.property("otherGimbal"))
check("Mode syncing", "Syncing <br> mode", False)
item.setProperty("activeGimbal", None)
check("Mode syncing", "Syncing <br> mode", False)
print("PASS: 8 Qt 6 toolbar binding scenarios")
