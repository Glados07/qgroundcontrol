"""Qt 6 regression of toolbar bindings AND click/ownership dispatch, not an APK.

Run with PySide6: python custom/test/Gimbal/GimbalModeUiTest.py
Production callbacks and ownership functions are extracted unchanged. Only the
application, timers, native controller and C++ mode service are test doubles.
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
enabled = re.search(r'enabled: (\(modelData.id !== "yawLock"[^\n]+)', source).group(1)
enabled = enabled.replace("modelData.id", '"yawLock"')
actions = "// Keep only the latest toolbar posture action" + source.split("    // Keep only the latest toolbar posture action", 1)[1].split(
    "    onActiveVehicleChanged:", 1)[0]
callbacks = "property var callbackList:" + source.split("property var callbackList:", 1)[1].split(
    "                        Layout.preferredWidth:", 1)[0]
click = source.split("                        onClicked: {", 1)[1].split(
    "\n                        }", 1)[0]
mode_connection = re.search(
    r"    Connections \{\s+id: modeControllerConnection\b.*?\n    \}", source, re.S
).group(0).strip()
qml = """
import QtQml
QtObject {
    id: control
    property QtObject actualGimbal: QtObject {
        property bool retracted: false
        property bool yawLock: false
        property bool gimbalHaveControl: true
        property bool gimbalOthersHaveControl: false
        property QtObject managerCompid: QtObject { property int rawValue: 1 }
        property QtObject deviceId: QtObject { property int rawValue: 154 }
    }
    property QtObject otherGimbal: QtObject { property bool retracted: false; property bool yawLock: false }
    property var activeGimbal: actualGimbal
    property bool sourceKnown: false
    property bool sourceLocked: false
    property int sourceMode: 0
    property int modeRequests: 0
    property bool lastTarget: true
    property int lastRequestedSession: -1
    property int acquireRequests: 0
    property var _gimbalCenterCoordinator: null
    property QtObject activeVehicle: QtObject {
        property int messagesSent: 0
        property int id: 1
    }
    property QtObject gimbalController: QtObject {
        function acquireGimbalControl() { control.acquireRequests++ }
        function toggleGimbalYawLock(value) { throw new Error("Old yaw command path used") }
    }
    property QtObject pendingOwnershipTimeout: QtObject { function stop() {} function restart() {} }
    property QtObject centerReplayDelay: QtObject { function stop() {} function restart() {} }
    property QtObject centerFinalAckTimeout: QtObject { function stop() {} function restart() {} }
    property QtObject modeSource: QtObject {
        property bool known: control.sourceKnown
        property bool yawLocked: control.sourceLocked
        property int mode: control.sourceMode
        property var gimbal: control.actualGimbal
        property bool commandPending: false
        property int sessionRevision: 1
        signal sessionChanged()
        signal commandFailed(string reason)
        function invalidateSession() {
            sessionRevision++
            control.sourceKnown = false
            sessionChanged()
        }
        function requestYawLock(locked, revision) {
            if (revision !== sessionRevision) throw new Error("Stale action reached dispatch")
            control.modeRequests++
            control.lastTarget = locked
            control.lastRequestedSession = revision
            control.activeVehicle.messagesSent++
            commandPending = true
            return true
        }
    }
    property QtObject app: QtObject {
        property QtObject corePlugin: QtObject { property var gimbalModeController: control.modeSource }
    }
    BINDINGS
    ACTIONS
    CALLBACKS
    property Connections modeConnection: MODE_CONNECTION
    property var modelData: ({id: "yawLock"})
    function click() { CLICK }
    property string statusText: STATUS
    property string buttonText: BUTTON
    property bool buttonEnabled: ENABLED
}
""".replace("BINDINGS", bindings).replace("ACTIONS", actions).replace("CALLBACKS", callbacks).replace(
    "MODE_CONNECTION", mode_connection).replace("CLICK", click).replace("STATUS", status).replace(
    "BUTTON", button).replace("ENABLED", enabled)

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

# Invoke the production button callback, not an invented request function.
item.setProperty("activeGimbal", item.property("actualGimbal"))
item.setProperty("sourceMode", 2)
item.setProperty("sourceLocked", True)
item.click()
assert item.property("modeRequests") == 1
assert item.property("lastTarget") is False
assert item.property("lastRequestedSession") == item.property("modeSource").property("sessionRevision")
check("Yaw locked", "Switching <br> mode", False)
item.click()
assert item.property("modeRequests") == 1
item.setProperty("sourceMode", 1)
item.setProperty("sourceLocked", False)
item.property("modeSource").setProperty("commandPending", False)
check("Yaw follow", "Yaw <br> Lock", True)
item.click()
assert item.property("modeRequests") == 2
assert item.property("lastTarget") is True
item.setProperty("sourceMode", 2)
item.setProperty("sourceLocked", True)
item.property("modeSource").setProperty("commandPending", False)
check("Yaw locked", "Yaw <br> Follow", True)

# Reproduce the old silent-drop condition: Follow clicked while locked, SDK
# sample expires while CONFIGURE is in flight, then ownership is granted.
item.property("actualGimbal").setProperty("gimbalHaveControl", False)
item.click()
application.processEvents()
assert item.property("acquireRequests") == 1
assert item.property("modeRequests") == 2
check("Yaw locked", "Switching <br> mode", False)
item.setProperty("sourceKnown", False)
item.property("actualGimbal").setProperty("gimbalHaveControl", True)
item._reviewPendingOwnership()
assert item.property("modeRequests") == 3, "Follow click silently lost after mode freshness expiry"
assert item.property("lastTarget") is False
item.setProperty("sourceKnown", True)
item.setProperty("sourceLocked", False)
item.setProperty("sourceMode", 1)
item.property("modeSource").setProperty("commandPending", False)
check("Yaw follow", "Yaw <br> Lock", True)

# Never replay a queued click onto another gimbal.
item.property("actualGimbal").setProperty("gimbalHaveControl", False)
item.click()
item.setProperty("activeGimbal", item.property("otherGimbal"))
item._reviewPendingOwnership()
assert item.property("modeRequests") == 3
assert item.property("_pendingOwnershipAction") is None

def pending_follow():
    candidate = component.create()
    assert candidate, [error.toString() for error in component.errors()]
    candidate.setProperty("sourceKnown", True)
    candidate.setProperty("sourceLocked", True)
    candidate.setProperty("sourceMode", 2)
    candidate.property("actualGimbal").setProperty("gimbalHaveControl", False)
    candidate.click()
    application.processEvents()
    assert candidate.property("acquireRequests") == 1
    assert candidate.property("modeRequests") == 0
    return candidate


# The production sessionChanged Connection must cancel the click immediately,
# including an already queued Qt.callLater ownership callback. Reusing the
# same Vehicle/controller/Gimbal objects must not make the old click valid.
for reset_kind in ("reconnect", "sdk_endpoint", "device_route", "manager_route"):
    candidate = pending_follow()
    candidate.property("actualGimbal").setProperty("gimbalHaveControl", True)
    candidate._schedulePendingOwnershipCheck()
    if reset_kind == "device_route":
        candidate.property("actualGimbal").property("deviceId").setProperty("rawValue", 155)
    if reset_kind == "manager_route":
        candidate.property("actualGimbal").property("managerCompid").setProperty("rawValue", 2)
    # Real C++ reset triggers are exercised in GimbalModeControllerTest.
    candidate.property("modeSource").invalidateSession()
    assert candidate.property("_pendingOwnershipAction") is None, reset_kind
    candidate.setProperty("sourceKnown", True)
    application.processEvents()
    candidate._reviewPendingOwnership()
    assert candidate.property("modeRequests") == 0, reset_kind
    assert candidate.property("acquireRequests") == 1, reset_kind
    candidate.click()  # a new deliberate action in the new session is allowed
    assert candidate.property("modeRequests") == 1, reset_kind
    assert candidate.property("lastTarget") is False
    assert candidate.property("lastRequestedSession") == 2

# Even if delivery of sessionChanged is delayed, the deferred context check
# must fail closed rather than rewrite the old action with a new revision.
candidate = pending_follow()
candidate.property("modeSource").setProperty("sessionRevision", 2)
candidate.property("actualGimbal").setProperty("gimbalHaveControl", True)
candidate._reviewPendingOwnership()
assert candidate.property("modeRequests") == 0
assert candidate.property("_pendingOwnershipAction") is None
print("PASS: mode bindings, bidirectional clicks, expiry within session, reset cancellation, stale deferred callbacks and route isolation")
