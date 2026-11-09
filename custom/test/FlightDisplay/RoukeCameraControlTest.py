"""Qt 6 resource-backed Rouke panel contract test (no network or real settings).

Uses the existing settings-layout UI doubles and the production Rouke panel.
Run directly with PySide6, or pass a prebuilt Qt 6 --resource and --output.
Screenshots render the real controls in a window-sized host, including popups.
"""
import argparse
import os
from pathlib import Path
import subprocess
import sys

sys.dont_write_bytecode = True

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
os.environ.setdefault("QT_QUICK_BACKEND", "software")
os.environ.setdefault("QT_QUICK_CONTROLS_STYLE", "Basic")
ROOT = Path(__file__).resolve().parents[3]
STUBS = ROOT / "custom/test/UI/FlyViewSettingsLayout"
sys.path.insert(0, str(STUBS))
import run as ui
import PySide6
from PySide6.QtCore import QObject, Property, Signal, Slot, QResource, QUrl, QPoint, QPointF, Qt, QTranslator, QMetaObject
from PySide6.QtGui import QGuiApplication, QFontDatabase, QFontMetricsF
from PySide6.QtQml import qmlRegisterSingletonInstance
from PySide6.QtQuick import QQuickItem, QQuickView
from PySide6.QtTest import QTest


class Manager(QObject):
    changed = Signal()
    recordingSessionStateChanged = Signal()

    enabled = Property(bool, lambda self: self.values["enabled"], notify=changed)
    controlAvailable = Property(bool, lambda self: self.values["controlAvailable"], notify=changed)
    sdkResponding = Property(bool, lambda self: self.values["sdkResponding"], notify=changed)
    pending = Property(bool, lambda self: self.values["pending"], notify=changed)
    gimbalCenterPending = Property(bool, lambda self: self.values["gimbalCenterPending"], notify=changed)
    localMediaStorageEnabled = Property(bool, lambda self: self.values["localMediaStorageEnabled"], notify=changed)
    recordingSessionActive = Property(bool, lambda self: self.values["recordingSessionActive"], notify=changed)
    recordingSessionCapturing = Property(bool, lambda self: self.values["recordingSessionCapturing"], notify=changed)
    cameraRecordingPending = Property(bool, lambda self: self.values["cameraRecordingPending"], notify=changed)
    recordingStopRequired = Property(bool, lambda self: self.values["recordingStopRequired"], notify=changed)
    videoRecordingAvailable = Property(bool, lambda self: self.values["enabled"] and (
        self.values["controlAvailable"] or self.values["localMediaStorageEnabled"]
        or self.values["recordingSessionActive"]
        or self.values["localRecording"] or self.values["localRecordingPending"]), notify=changed)
    sdCardStatusKnown = Property(bool, lambda self: self.values["sdCardStatusKnown"], notify=changed)
    sdCardInserted = Property(bool, lambda self: self.values["sdCardInserted"], notify=changed)
    recording = Property(bool, lambda self: self.values["recording"], notify=changed)
    recordingKnown = Property(bool, lambda self: self.values["recordingKnown"], notify=changed)
    localRecording = Property(bool, lambda self: self.values["localRecording"], notify=changed)
    localRecordingPending = Property(bool, lambda self: self.values["localRecordingPending"], notify=changed)
    viewKnown = Property(bool, lambda self: self.values["viewKnown"], notify=changed)
    photoCount = Property(int, lambda self: self.values["photoCount"], notify=changed)
    localPhotoCount = Property(int, lambda self: self.values["localPhotoCount"], notify=changed)
    viewType = Property(int, lambda self: self.values["viewType"], notify=changed)
    lastError = Property(str, lambda self: self.values["lastError"], notify=changed)
    localMediaError = Property(str, lambda self: self.values["localMediaError"], notify=changed)
    cameraParameters = Property("QVariantMap", lambda self: self.values["cameraParameters"], notify=changed)
    pendingParameter = Property(str, lambda self: self.values["pendingParameter"], notify=changed)

    def __init__(self):
        super().__init__()
        self.values = dict(enabled=True, controlAvailable=False, sdkResponding=False,
                           pending=False, gimbalCenterPending=False, localMediaStorageEnabled=True,
                           recordingSessionActive=False, recordingSessionCapturing=False,
                           cameraRecordingPending=False, recordingStopRequired=False,
                           sdCardStatusKnown=False, sdCardInserted=False, recording=False,
                           recordingKnown=False, localRecording=False,
                           localRecordingPending=False, photoCount=0, localPhotoCount=0,
                           viewKnown=False, viewType=4, lastError="", localMediaError="",
                           cameraParameters={}, pendingParameter="")
        self.actions = []
        # UI contract only: the real Manager's four-request serial queue is
        # covered by its C++ fixture, not reimplemented in this service double.
        self.refresh_requests = 0
        self.refresh_cancellations = 0

    @Slot()
    def refreshCameraParameters(self): self.refresh_requests += 1

    @Slot()
    def cancelCameraParameterRefresh(self): self.refresh_cancellations += 1

    def update(self, **values):
        self.values.update(values)
        self.changed.emit()
        self.recordingSessionStateChanged.emit()
        QTest.qWait(30)

    @Slot()
    def takePhoto(self): self.actions.append("photo")

    @Slot()
    def startRecording(self): self.actions.append("start")

    @Slot()
    def stopRecording(self): self.actions.append("stop")

    @Slot(result=bool)
    def toggleVideoRecording(self):
        # Mirror only the public direction contract. Tests explicitly supply
        # ACK/failure state; transport and intent persistence have C++ tests.
        self.actions.append("stop" if self.values["recordingStopRequired"] else "start")
        self.update(recordingStopRequired=True, recordingSessionActive=True,
                    cameraRecordingPending=True, pending=True, lastError="")
        return True

    def finish_recording_stop(self):
        self.update(recordingStopRequired=False, recordingSessionActive=False,
                    recordingSessionCapturing=False, cameraRecordingPending=False,
                    pending=False, recording=False, recordingKnown=True,
                    localRecording=False, localRecordingPending=False, lastError="")

    @Slot()
    def autoFocus(self): self.actions.append("focus")

    @Slot()
    def centerGimbal(self): self.actions.append("center")

    @Slot(int)
    def setView(self, value): self.actions.append(value)

    @Slot(str, "QVariant", result=bool)
    def setCameraParameter(self, key, value):
        self.actions.append(("set", key, value))
        self.update(pending=True, pendingParameter=key)
        return True

    @Slot(str, result=bool)
    def requestCameraParameter(self, key):
        self.actions.append(("get", key))
        self.update(pending=True, pendingParameter=key)
        return True

    def acknowledge(self, key, value):
        parameters = dict(self.values["cameraParameters"])
        parameters[key] = value
        self.update(cameraParameters=parameters, pending=False, pendingParameter="")




class Core(QObject):
    def __init__(self, manager):
        super().__init__()
        self.manager = manager

    corePlugin = Property("QVariantMap", lambda self: {"roukeControlManager": self.manager}, constant=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--resource", help="Prebuilt Qt 6 custom resource; built automatically when omitted")
    parser.add_argument("--output", default=str(ROOT / "debug/rouke-camera-ui"))
    parser.add_argument("--translation")
    args = parser.parse_args()
    output = Path(args.output)
    output.mkdir(parents=True, exist_ok=True)
    resource = args.resource
    if not resource:
        qt_root = Path(PySide6.__file__).parent
        rcc = next(path for path in (qt_root / "rcc.exe", qt_root / "Qt/libexec/rcc")
                   if path.is_file())
        resource = str(output / "custom.rcc")
        subprocess.run([str(rcc), "--binary", str(ROOT / "custom/custom.qrc"),
                        "-o", resource], check=True)
    app = QGuiApplication([])
    for filename in ("C:/Windows/Fonts/msyh.ttc", "C:/Windows/Fonts/consola.ttf"):
        if Path(filename).exists():
            assert QFontDatabase.addApplicationFont(filename) >= 0
    assert QResource.registerResource(resource)
    manager = Manager()
    core = Core(manager)
    style = ui.Style()
    qmlRegisterSingletonInstance(Core, "QGroundControl", 1, 0, "QGroundControl", core)
    ui.register_qml("QGroundControl.ScreenTools", "ScreenTools", STUBS / "ScreenTools.qml", True)
    ui.register_qml("QGroundControl.Palette", "QGCPalette", STUBS / "QGCPalette.qml")
    for name in ("QGCLabel", "DeadMouseArea"):
        ui.register_qml("QGroundControl.Controls", name, ROOT / f"src/QmlControls/{name}.qml")
    ui.register_qml("QGroundControl.Controls", "QGCButton", STUBS / "QGCButton.qml")
    warnings = []
    def log_message(kind, context, message):
        warnings.append(message)
    ui.qInstallMessageHandler(log_message)
    view = QQuickView()
    view.engine().setImportPathList([str(Path(PySide6.__file__).parent / "qml"), "qrc:/qt/qml"])
    view.rootContext().setContextProperty("layoutTestStyle", style)
    harness = output / "rouke-panel-harness.qml"
    harness.write_text("""
import QtQuick
Rectangle {
    width: 1200; height: 850
    color: "#17232d"
    Loader {
        objectName: "cameraLoader"
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 16
        source: "qrc:/Custom/qml/QGroundControl/FlightDisplay/RoukeCameraControl.qml"
    }
}
""", encoding="utf-8")
    view.setResizeMode(QQuickView.SizeRootObjectToView)
    view.resize(1200, 850)
    view.setSource(QUrl.fromLocalFile(str(harness.resolve())))
    assert view.status() != QQuickView.Error, "\n".join(e.toString() for e in view.errors())
    host = view.rootObject()
    loader = host.findChild(QQuickItem, "cameraLoader")
    root = loader.property("item")
    assert root is not None, "\n".join(warnings)
    view.show()
    QTest.qWait(100)

    def button(text):
        return next(item for item in ui.descendants(root)
                    if item.metaObject().indexOfProperty("pressed") >= 0
                    and item.property("text") == text)

    def named(name):
        item = host.findChild(QObject, name)
        if item is None:
            item = next((child for child in ui.descendants(view.contentItem())
                         if child.objectName() == name), None)
        assert item is not None, name
        return item

    def click_item(item):
        assert item.isEnabled(), item.objectName() or item.property("text")
        p = item.mapToScene(QPointF(item.width() / 2, item.height() / 2))
        assert 0 <= p.x() < view.width() and 0 <= p.y() < view.height(), (item.objectName(), p)
        QTest.mouseClick(view, Qt.LeftButton, Qt.NoModifier, QPoint(int(p.x()), int(p.y())))
        QTest.qWait(60)

    def click(text):
        click_item(button(text))

    def invoke(item, method):
        assert QMetaObject.invokeMethod(item, method), method
        QTest.qWait(80)

    def scroll_to(flick, item):
        # Keep the actual editor/action visible before sending mouse events.
        content = flick.property("contentItem")
        y = item.mapToItem(content, QPointF(0, 0)).y()
        flick.setProperty("contentY", max(0, min(y - 24,
                            flick.property("contentHeight") - flick.height())))
        QTest.qWait(60)

    def type_value(item, text):
        click_item(item)
        QTest.keyClick(view, Qt.Key_A, Qt.ControlModifier)
        for character in text:
            QTest.keyClick(view, ord(character.upper()))
        QTest.qWait(40)

    def assert_input_fits(item):
        usable_width = item.width() - item.property("leftPadding") - item.property("rightPadding")
        assert item.property("contentWidth") <= usable_width + 1, (
            "Numeric value must remain fully visible", item.objectName(), item.property("text"), usable_width)
        for child in ui.descendants(item):
            if child.isVisible() and child.property("text") in ("m", "cm"):
                assert child.property("paintedWidth") <= child.width() + 1
                assert child.x() >= 0 and child.x() + child.width() <= item.width() + 1
                assert QFontMetricsF(item.property("font")).horizontalAdvance("2147483647") <= usable_width + 1, (
                    "Shared numeric width reserves ten digits beside the unit", item.objectName(),
                    view.width(), style.scale_value, item.width(), usable_width,
                    QFontMetricsF(item.property("font")).horizontalAdvance("2147483647"))

    assert not button("Focus").isEnabled() and not named("viewButton").isEnabled()
    assert not button("Take Photo").isEnabled(), "Offline UI must not offer local-only photography"
    assert manager.actions == []

    def assert_offline_dispatch_disabled():
        commands = [item for item in host.findChildren(QObject)
                    if item.objectName().startswith(("read_", "apply_"))]
        assert commands, "Parameter command controls must be present"
        for item in commands:
            expected = (item.objectName().startswith("read_")
                        and manager.values["controlAvailable"] and not manager.values["pending"])
            assert item.isEnabled() == expected, ("Offline GET/write gate", item.objectName())

    def click_disabled(item):
        assert not item.isEnabled(), item.objectName()
        point = item.mapToScene(QPointF(item.width() / 2, item.height() / 2))
        QTest.mouseClick(view, Qt.LeftButton, Qt.NoModifier, QPoint(int(point.x()), int(point.y())))
        QTest.qWait(40)

    # Offline navigation and draft editing do not depend on a payload reply.
    click_item(named("settingsButton"))
    QTest.qWait(160)  # Popup.opened follows its entrance transition.
    assert manager.refresh_requests == 1, "Opening settings requests one read-only refresh"
    offline_popup = named("settingsPopup")
    assert offline_popup.property("visible") and offline_popup.property("selectedPage") == 0
    set_only_keys = ("focusMode", "shutterSpeed", "aperture", "imageQuality",
                     "triggerPhoto", "shootingDistance", "distanceThreshold")
    for key in set_only_keys:
        assert "Set only; no read command." in named("hint_" + key).property("text"), key
    for key in ("iso", "whiteBalance", "focusValue", "imageSize"):
        assert "Set only; no read command." not in named("hint_" + key).property("text"), key
    type_value(named("editor_iso"), "12")
    assert named("parameter_iso").property("draftValue") == 12
    assert_offline_dispatch_disabled()
    click_disabled(named("read_iso"))
    click_disabled(named("apply_iso"))
    click_item(named("rangeTab"))
    assert named("parameter_triggerPhoto").property("draftValue") == 0
    assert not named("parameter_triggerPhoto").property("known")
    click_item(named("choice_triggerPhoto"))
    QTest.keyClick(view, Qt.Key_End)
    QTest.keyClick(view, Qt.Key_Return)
    QTest.qWait(60)
    assert named("parameter_triggerPhoto").property("draftValue") == 1
    click_disabled(named("apply_triggerPhoto"))
    range_drafts = named("rangeSettingsScroll")
    scroll_to(range_drafts, named("editor_shootingDistance"))
    type_value(named("editor_shootingDistance"), "30")
    assert named("parameter_shootingDistance").property("draftValue") == 30
    assert_offline_dispatch_disabled()
    assert manager.actions == [], "Offline editing must not read, write, or enable capture"
    click_item(named("cameraTab"))
    assert named("parameter_iso").property("draftValue") == 12
    manager.update(controlAvailable=True, sdkResponding=True)
    assert offline_popup.property("visible") and manager.actions == [], "Reconnect must not apply drafts automatically"
    type_value(named("editor_iso"), "13")
    manager.update(sdkResponding=False)  # Endpoint may remain writable despite no recent payload reply.
    assert offline_popup.property("visible"), "A disconnect must leave the settings page open"
    assert named("parameter_iso").property("draftValue") == 13
    assert named("editor_iso").isEnabled()
    assert_offline_dispatch_disabled()
    type_value(named("editor_iso"), "14")
    assert named("parameter_iso").property("draftValue") == 14
    assert manager.actions == []
    assert manager.refresh_requests == 1, "Tab navigation and draft edits must not refresh again"
    click_item(named("read_iso"))
    assert manager.actions == [("get", "iso")], "Offline configured endpoints allow a read-only probe"
    assert not named("apply_iso").isEnabled() and not named("read_iso").isEnabled()
    manager.update(pending=False, pendingParameter="", lastError="No response to read-only probe")
    type_value(named("editor_iso"), "14")
    manager.update(sdkResponding=True)
    assert named("parameter_iso").property("draftValue") == 14
    assert manager.actions == [("get", "iso")], "A returning endpoint must not apply the draft"
    cancellations_before = manager.refresh_cancellations
    invoke(root, "closeTransientUi")
    assert manager.refresh_cancellations > cancellations_before, "Closing immediately cancels unsent refresh work"
    QTest.qWait(350)
    manager.actions.clear()
    manager.update(sdkResponding=False, lastError="")
    manager.update(localMediaStorageEnabled=False)
    assert not button("Take Photo").isEnabled()
    manager.update(controlAvailable=True, sdkResponding=True)
    click("Take Photo")
    assert manager.actions == ["photo"]
    assert root.findChild(QObject, "stopButton") is None, "Recording has one toggle, not a separate stop control"
    click_item(named("recordButton"))
    assert manager.actions[-1] == "start" and named("recordButton").property("iconName") == "recordStop"
    assert named("recordButton").isEnabled(), "A pending start must remain stoppable"
    click_item(named("recordButton"))
    assert manager.actions[-1] == "stop", "The second click stops even before the start ACK"
    manager.finish_recording_stop()
    click("Focus")
    click("Center")
    click_item(named("viewButton"))
    assert named("viewMenu").property("visible")
    click_item(named("viewButton"))
    assert not named("viewMenu").property("visible"), "Repeating the menu action must close it"
    QTest.qWait(300)
    click_item(named("viewButton"))
    click_item(named("view_5"))
    assert manager.actions == ["photo", "start", "stop", "focus", "center", 5]
    manager.update(viewKnown=True, viewType=5, recordingKnown=True,
                   recording=True, recordingSessionActive=True, recordingSessionCapturing=True,
                   recordingStopRequired=True, localRecording=True)
    QTest.qWait(300)
    click_item(named("viewButton"))
    assert named("view_5").property("checked") and not named("view_4").property("checked")
    invoke(root, "closeTransientUi")
    assert named("recordButton").isEnabled()
    manager.update(pending=True)
    assert not button("Focus").isEnabled() and named("recordButton").isEnabled()
    click_item(named("recordButton"))  # A parameter ACK must not block the recording toggle's stop direction.
    assert manager.actions[-1] == "stop"
    click("Center")  # A camera ACK must not block the independent gimbal action.
    manager.update(gimbalCenterPending=True)
    assert not button("Center").isEnabled(), "A pending center must not be sent twice"
    manager.finish_recording_stop()
    assert button("Focus").isEnabled(), "Gimbal centering must not block camera actions"
    manager.update(gimbalCenterPending=False)
    manager.update(pending=False, sdkResponding=True)
    assert named("focusButton").mapToScene(QPointF(0, 0)).y() < named("photoButton").mapToScene(QPointF(0, 0)).y(), "Focus belongs above the shutter"

    # Every open requests the Manager's read-only refresh once. Page changes
    # never dispatch writes, and the trigger draft remains Off until applied.
    previous_actions = list(manager.actions)
    refreshes_before = manager.refresh_requests
    click_item(named("settingsButton"))
    QTest.qWait(150)
    popup = named("settingsPopup")
    camera_scroll = named("cameraSettingsScroll")
    range_scroll = named("rangeSettingsScroll")
    assert popup.property("visible") and popup.property("selectedPage") == 0
    click_item(named("settingsButton"))
    QTest.qWait(150)
    assert not popup.property("visible"), "Repeating the settings action must close it"
    QTest.qWait(300)
    click_item(named("settingsButton"))
    QTest.qWait(150)
    assert not named("parameter_iso").property("known")
    assert named("parameter_triggerPhoto").property("draftValue") == 0
    assert not named("parameter_triggerPhoto").property("known")
    assert manager.actions == previous_actions
    assert manager.refresh_requests == refreshes_before + 2, "Close/reopen starts exactly one fresh refresh"
    iso_editor = named("editor_iso")
    assert iso_editor.width() < camera_scroll.width() * 0.65, "Desktop inputs should not fill the entire row"
    input_edge = iso_editor.mapToScene(QPointF(iso_editor.width(), 0)).x()
    read_edge = named("read_iso").mapToScene(QPointF(0, 0)).x()
    assert 0 <= read_edge - input_edge <= 20, "Read/apply actions should sit next to their input"

    # Real keyboard edits do not overwrite confirmed state. Applying a draft
    # waits for an ACK, and the device's returned value is authoritative.
    type_value(named("editor_iso"), "12")
    assert manager.actions == previous_actions
    click_item(named("apply_iso"))
    assert manager.actions[-1] == ("set", "iso", 12)
    assert not named("parameter_iso").property("known")
    assert not named("apply_iso").isEnabled()
    manager.acknowledge("iso", 10)
    assert named("parameter_iso").property("confirmedValue") == 10
    assert named("parameter_iso").property("draftValue") == 10
    type_value(named("editor_iso"), "14")
    manager.update(photoCount=3)
    assert named("parameter_iso").property("draftValue") == 14
    assert named("parameter_iso").property("confirmedValue") == 10
    click_item(named("read_iso"))
    assert manager.actions[-1] == ("get", "iso")
    manager.acknowledge("iso", 6)
    assert named("parameter_iso").property("draftValue") == 6
    type_value(named("editor_iso"), "12")
    click_item(named("read_iso"))
    manager.acknowledge("iso", 6)  # Identical ACK value still discards the unsent edit.
    assert named("parameter_iso").property("draftValue") == 6
    type_value(named("editor_iso"), "12")
    click_item(named("apply_iso"))
    manager.acknowledge("iso", 6)  # Device may return the existing actual value.
    assert named("parameter_iso").property("draftValue") == 6
    type_value(named("editor_iso"), "12")
    click_item(named("apply_iso"))
    manager.update(pending=False, pendingParameter="", lastError="Payload rejected ISO")
    assert named("parameter_iso").property("draftValue") == 12, "Rejected input remains editable"
    assert named("parameter_iso").property("confirmedValue") == 6
    manager.update(lastError="")

    # Reach lower camera cards through the actual scrollable viewport and
    # open an enum menu so delegate/binding failures cannot remain hidden.
    camera_scroll.setProperty("contentY", 0)
    point = camera_scroll.mapToScene(QPointF(camera_scroll.width() / 2, camera_scroll.height() / 2))
    QTest.wheelEvent(view, point, QPoint(0, -720))
    QTest.qWait(150)
    view.grabWindow().save(str(output / "camera-scroll-check.png"))
    assert camera_scroll.property("contentY") > 0, (
        "Camera page must scroll", camera_scroll.width(), camera_scroll.height(),
        camera_scroll.property("contentHeight"), point, popup.property("visible"))
    scroll_to(camera_scroll, named("editor_focusValue"))
    type_value(named("editor_focusValue"), "2147483647")
    assert named("parameter_focusValue").property("valid")
    assert_input_fits(named("editor_focusValue"))
    scroll_to(camera_scroll, named("choice_imageSize"))
    click_item(named("choice_imageSize"))
    QTest.keyClick(view, Qt.Key_Home)
    QTest.keyClick(view, Qt.Key_Return)
    QTest.qWait(60)
    click_item(named("apply_imageSize"))
    assert manager.actions[-1] == ("set", "imageSize", 0), "L uses wire value 0"
    manager.acknowledge("imageSize", 0)

    previous_actions = list(manager.actions)
    click_item(named("rangeTab"))
    assert popup.property("selectedPage") == 1
    assert manager.actions == previous_actions
    scroll_to(range_scroll, named("editor_shootingDistance"))
    type_value(named("editor_shootingDistance"), "19")
    assert not named("apply_shootingDistance").isEnabled(), "Distance below 20 m is invalid"
    type_value(named("editor_shootingDistance"), "40")
    click_item(named("apply_shootingDistance"))
    assert manager.actions[-1] == ("set", "shootingDistance", 40)
    manager.acknowledge("shootingDistance", 40)
    scroll_to(range_scroll, named("editor_distanceThreshold"))
    type_value(named("editor_distanceThreshold"), "29")
    assert not named("apply_distanceThreshold").isEnabled(), "Threshold below 30 cm is invalid"
    type_value(named("editor_distanceThreshold"), "15000")
    assert_input_fits(named("editor_distanceThreshold"))
    click_item(named("apply_distanceThreshold"))
    assert manager.actions[-1] == ("set", "distanceThreshold", 15000)
    manager.acknowledge("distanceThreshold", 15000)
    scroll_to(range_scroll, named("choice_triggerPhoto"))
    previous_actions = list(manager.actions)
    click_item(named("choice_triggerPhoto"))
    QTest.keyClick(view, Qt.Key_End)
    QTest.keyClick(view, Qt.Key_Return)
    QTest.qWait(60)
    assert named("parameter_triggerPhoto").property("draftValue") == 1
    assert manager.actions == previous_actions, "Changing a draft must not enable capture"
    click_item(named("apply_triggerPhoto"))
    assert manager.actions[-1] == ("set", "triggerPhoto", 1)
    assert not named("parameter_triggerPhoto").property("known")
    manager.acknowledge("triggerPhoto", 1)

    # Simulate payload failure: the acknowledged value remains authoritative.
    click_item(named("choice_triggerPhoto"))
    QTest.keyClick(view, Qt.Key_Home)
    QTest.keyClick(view, Qt.Key_Return)
    QTest.qWait(60)
    click_item(named("apply_triggerPhoto"))
    manager.update(pending=False, pendingParameter="", lastError="Payload rejected the parameter")
    assert named("parameter_triggerPhoto").property("confirmedValue") == 1
    manager.update(lastError="")

    click_item(named("choice_triggerPhoto"))
    combo_popup = next(child for child in named("choice_triggerPhoto").findChildren(QObject)
                       if child.metaObject().indexOfProperty("closePolicy") >= 0)
    assert combo_popup.property("visible")
    invoke(root, "closeTransientUi")
    QTest.qWait(350)
    assert not popup.property("visible")
    assert not combo_popup.property("visible"), "Nested choice menus must close with their parent"
    click_item(named("settingsButton"))
    assert popup.property("selectedPage") == 0, "Reopening defaults to camera parameters"
    assert camera_scroll.property("contentY") == 0
    manager.update(enabled=False)
    QTest.qWait(140)
    assert not root.isVisible() and not popup.property("visible")
    manager.update(enabled=True)

    translator = QTranslator()
    if args.translation:
        assert translator.load(args.translation)
        app.installTranslator(translator)
        view.engine().retranslate()
        QTest.qWait(30)
        assert button("拍照").isEnabled()
        for key in set_only_keys:
            assert "仅支持设置，协议无读取命令。" in named("hint_" + key).property("text"), key
    view.grabWindow().save(str(output / "rouke-panel.png"))
    click_item(named("settingsButton"))
    QTest.qWait(160)
    view.grabWindow().save(str(output / "rouke-camera-settings.png"))
    click_item(named("rangeTab"))
    QTest.qWait(100)
    view.grabWindow().save(str(output / "rouke-range-settings.png"))
    # A clean unknown-device preview shows the user's requested Off default,
    # rather than leaving the deliberately enabled test state in every image.
    invoke(root, "closeTransientUi")
    QTest.qWait(350)
    parameters = dict(manager.values["cameraParameters"])
    parameters.pop("triggerPhoto", None)
    manager.update(cameraParameters=parameters)
    click_item(named("settingsButton"))
    click_item(named("rangeTab"))
    assert not named("parameter_triggerPhoto").property("known")
    assert named("parameter_triggerPhoto").property("draftValue") == 0
    QTest.qWait(160)
    view.grabWindow().save(str(output / "rouke-range-default.png"))

    # Resize an open popup, including a large-font small-screen case. Neither
    # page may escape the viewport or compress its editors to fit all cards.
    previous_actions = list(manager.actions)
    for width, height, scale in ((360, 640, 1.0), (800, 600, 1.0), (800, 700, 1.5),
                                 (320, 640, 1.5), (731, 411, 1.0)):
        style.scale_value = scale
        style.changed.emit()
        view.resize(width, height)
        QTest.qWait(160)
        x, y = popup.property("x"), popup.property("y")
        assert x >= 0 and y >= 0
        assert x + popup.property("width") <= width + 1
        assert y + popup.property("height") <= height + 1
        assert abs(named("cameraTab").height() - named("rangeTab").height()) < 1, "Page tabs must align"
        for name in ("cameraTab", "rangeTab"):
            content = named(name).property("contentItem")
            labels = [item for item in [content, *ui.descendants(content)]
                      if item.metaObject().indexOfProperty("paintedWidth") >= 0 and item.isVisible()]
            assert labels, ("Tab must retain its label", name)
            for label in labels:
                assert label.property("paintedWidth") <= label.width() + 1, (
                    "Tab text must fit horizontally", name, width, scale)
                assert label.property("paintedHeight") <= label.height() + 1, (
                    "Tab text must fit vertically", name, width, scale)
        for tab_name, flick in (("cameraTab", camera_scroll), ("rangeTab", range_scroll)):
            click_item(named(tab_name))
            if tab_name == "cameraTab":
                named("editor_focusValue").setProperty("text", "2147483647")
                assert_input_fits(named("editor_focusValue"))
            else:
                assert_input_fits(named("editor_distanceThreshold"))
            view.grabWindow().save(str(output / f"rouke-{tab_name}-{width}x{height}-{scale}.png"))
            assert flick.height() > 80 * scale, (
                "Parameter viewport must remain usable", width, height, scale, flick.height())
            assert flick.property("contentHeight") > 0
            if flick.property("contentHeight") > flick.height():
                origin = flick.property("contentY")
                flick.setProperty("contentY", 0)
                point = flick.mapToScene(QPointF(flick.width() / 2, flick.height() / 2))
                QTest.wheelEvent(view, point, QPoint(0, -360))
                QTest.qWait(80)
                assert flick.property("contentY") > 0, "Overflowing forms must scroll"
                flick.setProperty("contentY", origin)
            assert abs(flick.property("contentWidth") - flick.width()) < 1
            for child in ui.descendants(flick):
                if child.isVisible() and child.objectName().startswith(("editor_", "choice_", "apply_")):
                    assert child.width() > 30 and child.height() >= 36 * scale, child.objectName()
                if child.isVisible() and child.objectName().startswith("hint_"):
                    assert child.property("paintedWidth") <= child.width() + 1, (
                        "Parameter hint must wrap within its row", child.objectName(), width, scale)
                    assert child.property("paintedHeight") <= child.height() + 1, (
                        "Parameter hint must retain every line", child.objectName(), width, scale)
        invoke(root, "closeTransientUi")
        QTest.qWait(350)
        assert named("cameraActionsGrid").property("columns") == 2
        # Whole-column overflow is tested through the production native entry
        # in CameraControlPreviewTest, above the compass reservation.
        click_item(named("settingsButton"))
        QTest.qWait(160)
        assert popup.property("visible")
    assert manager.actions == previous_actions, "Layout changes must not send writes or manual commands"
    manager.update(enabled=False)
    QTest.qWait(150)
    assert not root.isVisible()
    assert not popup.property("visible")
    view.setSource(QUrl())
    QTest.qWait(30)
    errors = [message for message in warnings if any(token in message for token in
              ("TypeError", "ReferenceError", "Binding loop", "Unable to assign", "Cannot assign"))]
    assert not errors, "\n".join(errors)
    print("PASS: camera actions, one refresh per open/immediate cancel, offline GET-only probe, ACK/draft isolation, explicit parameter dispatch, default-Off trigger, "
          "range validation, set-only hints, scrolling, popup lifecycle, resize/font layouts; no QML errors")
    view.close()


if __name__ == "__main__":
    main()
