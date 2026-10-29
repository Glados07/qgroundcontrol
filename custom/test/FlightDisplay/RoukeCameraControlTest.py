"""Qt 6 resource-backed Rouke panel contract test (no network or real settings).

Uses the existing settings-layout UI doubles and the production Rouke panel.
Build custom.qrc with Qt 6 rcc, then pass --resource and --output.
"""
import argparse
import os
from pathlib import Path
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
from PySide6.QtCore import QObject, Property, Signal, Slot, QResource, QUrl, QPoint, QPointF, Qt, QTranslator
from PySide6.QtGui import QGuiApplication, QFontDatabase
from PySide6.QtQml import qmlRegisterSingletonInstance
from PySide6.QtQuick import QQuickView
from PySide6.QtTest import QTest


class Manager(QObject):
    changed = Signal()

    enabled = Property(bool, lambda self: self.values["enabled"], notify=changed)
    controlAvailable = Property(bool, lambda self: self.values["controlAvailable"], notify=changed)
    sdkResponding = Property(bool, lambda self: self.values["sdkResponding"], notify=changed)
    pending = Property(bool, lambda self: self.values["pending"], notify=changed)
    gimbalCenterPending = Property(bool, lambda self: self.values["gimbalCenterPending"], notify=changed)
    localMediaStorageEnabled = Property(bool, lambda self: self.values["localMediaStorageEnabled"], notify=changed)
    recordingSessionActive = Property(bool, lambda self: self.values["recordingSessionActive"], notify=changed)
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

    def __init__(self):
        super().__init__()
        self.values = dict(enabled=True, controlAvailable=False, sdkResponding=False,
                           pending=False, gimbalCenterPending=False, localMediaStorageEnabled=True,
                           recordingSessionActive=False, recording=False,
                           recordingKnown=False, localRecording=False,
                           localRecordingPending=False, photoCount=0, localPhotoCount=0,
                           viewKnown=False, viewType=4, lastError="", localMediaError="")
        self.actions = []

    def update(self, **values):
        self.values.update(values)
        self.changed.emit()
        QTest.qWait(30)

    @Slot()
    def takePhoto(self): self.actions.append("photo")

    @Slot()
    def startRecording(self): self.actions.append("start")

    @Slot()
    def stopRecording(self): self.actions.append("stop")

    @Slot()
    def autoFocus(self): self.actions.append("focus")

    @Slot()
    def centerGimbal(self): self.actions.append("center")

    @Slot(int)
    def setView(self, value): self.actions.append(value)




class Core(QObject):
    def __init__(self, manager):
        super().__init__()
        self.manager = manager

    corePlugin = Property("QVariantMap", lambda self: {"roukeControlManager": self.manager}, constant=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--resource", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--translation")
    args = parser.parse_args()
    output = Path(args.output)
    output.mkdir(parents=True, exist_ok=True)
    app = QGuiApplication([])
    for filename in ("C:/Windows/Fonts/msyh.ttc", "C:/Windows/Fonts/consola.ttf"):
        if Path(filename).exists():
            assert QFontDatabase.addApplicationFont(filename) >= 0
    assert QResource.registerResource(args.resource)
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
    view.setSource(QUrl("qrc:/Custom/qml/QGroundControl/FlightDisplay/RoukeCameraControl.qml"))
    assert view.status() != QQuickView.Error, "\n".join(e.toString() for e in view.errors())
    root = view.rootObject()
    view.resize(int(root.implicitWidth()), int(root.implicitHeight()))
    view.show()
    QTest.qWait(100)

    def button(text):
        return next(item for item in ui.descendants(root)
                    if item.metaObject().indexOfProperty("pressed") >= 0
                    and item.property("text") == text)

    def click(text):
        item = button(text)
        assert item.isEnabled(), text
        p = item.mapToScene(QPointF(item.width() / 2, item.height() / 2))
        QTest.mouseClick(view, Qt.LeftButton, Qt.NoModifier, QPoint(int(p.x()), int(p.y())))
        QTest.qWait(20)

    assert not button("Focus").isEnabled() and not button("LR1").isEnabled()
    click("Take Photo")  # Local storage is independent of payload availability.
    assert manager.actions == ["photo"]
    manager.update(localMediaStorageEnabled=False)
    assert not button("Take Photo").isEnabled()
    manager.update(controlAvailable=True)
    click("Stop")  # Unknown remote recording state still permits explicit stop.
    click("Record")
    click("Focus")
    click("Center")
    click("OV2735")
    assert manager.actions == ["photo", "stop", "start", "focus", "center", 5]
    manager.update(viewKnown=True, viewType=5, recordingKnown=True,
                   recording=True, recordingSessionActive=True, localRecording=True)
    assert button("OV2735").property("checked") and not button("LR1").property("checked")
    assert not button("Record").isEnabled()
    manager.update(pending=True)
    assert not button("Focus").isEnabled() and button("Stop").isEnabled()
    click("Stop")  # Pending camera ACK must not block local stop.
    click("Center")  # A camera ACK must not block the independent gimbal action.
    manager.update(gimbalCenterPending=True)
    assert not button("Center").isEnabled(), "A pending center must not be sent twice"
    manager.update(pending=False)
    assert button("Focus").isEnabled(), "Gimbal centering must not block camera actions"
    manager.update(gimbalCenterPending=False)
    manager.update(pending=False, sdkResponding=True)
    translator = QTranslator()
    if args.translation:
        assert translator.load(args.translation)
        app.installTranslator(translator)
        view.engine().retranslate()
        QTest.qWait(30)
        assert button("拍照").isEnabled()
    view.grabWindow().save(str(output / "rouke-panel.png"))
    manager.update(enabled=False)
    assert not root.isVisible()
    view.setSource(QUrl())
    QTest.qWait(30)
    errors = [message for message in warnings if any(token in message for token in
              ("TypeError", "ReferenceError", "Binding loop", "Unable to assign", "Cannot assign"))]
    assert not errors, "\n".join(errors)
    print("PASS: offline local media, command dispatch, ACK-state view, unknown-state stop and pending gating; no QML errors")
    view.close()


if __name__ == "__main__":
    main()
