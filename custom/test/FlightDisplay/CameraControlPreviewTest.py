"""Render and exercise production A8/Rouke selectors, rails and settings.

Only application services, telemetry and unrelated terrain/photo fallback are
test doubles. Screenshots are real Qt Quick component previews, not QGC runs.
"""
import argparse
from pathlib import Path
import subprocess
import sys
from xml.etree import ElementTree
from shiboken6 import isValid

sys.dont_write_bytecode = True

import RoukeCameraControlTest as rouke
from RoukeCameraControlTest import ROOT, STUBS, ui, PySide6
from PySide6.QtCore import (QObject, Property, Signal, Slot, QResource, QUrl,
                           QPoint, QPointF, Qt, QTranslator)
from PySide6.QtGui import QGuiApplication, QFontDatabase
from PySide6.QtQml import qmlRegisterSingletonInstance, qmlRegisterType
from PySide6.QtQuick import QQuickItem, QQuickView
from PySide6.QtTest import QTest


class A8Manager(rouke.Manager):
    recordingSessionStateChanged = Signal()
    photoCountChanged = Signal()
    localPhotoCountChanged = Signal()
    continuousZoomActiveChanged = Signal()
    recordingSessionCapturing = Property(bool, lambda self: self.values.get("recordingSessionCapturing", False), notify=recordingSessionStateChanged)
    cameraRecordingPending = Property(bool, lambda self: self.values["cameraRecordingPending"], notify=recordingSessionStateChanged)
    videoRecordingAvailable = Property(bool, lambda self: True, constant=True)
    cameraStatusKnown = Property(bool, lambda self: True, constant=True)
    zoomControlsUnlocked = Property(bool, lambda self: True, constant=True)
    zoomStatusKnown = Property(bool, lambda self: True, constant=True)
    zoomInTapAvailable = Property(bool, lambda self: True, constant=True)
    zoomOutTapAvailable = Property(bool, lambda self: True, constant=True)
    zoomInHoldAvailable = Property(bool, lambda self: True, constant=True)
    zoomOutHoldAvailable = Property(bool, lambda self: True, constant=True)
    continuousZoomActive = Property(bool, lambda self: False, constant=True)
    currentZoom = Property(float, lambda self: 4.0, constant=True)
    actualZoomKnown = Property(bool, lambda self: True, constant=True)
    actualZoom = Property(float, lambda self: 4.0, constant=True)

    def update(self, **values):
        super().update(**values)
        self.recordingSessionStateChanged.emit()

    @Slot()
    def toggleVideoRecording(self): self.actions.append("video")

    @Slot()
    def cancelZoom(self): self.actions.append("cancelZoom")

    @Slot()
    def stopZoom(self): self.actions.append("stopZoom")

    @Slot()
    def zoomIn(self): self.actions.append("zoomIn")

    @Slot()
    def zoomOut(self): self.actions.append("zoomOut")

    @Slot(int, int, result=bool)
    def startZoomWithPressDuration(self, direction, duration):
        self.actions.append(("hold", direction, duration))
        return True


class Core(QObject):
    def __init__(self, a8, payload):
        super().__init__()
        self.managers = {"gimbalControlManager": a8, "roukeControlManager": payload}

    corePlugin = Property("QVariantMap", lambda self: self.managers, constant=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", default=str(ROOT / "debug/camera-icons-preview"))
    parser.add_argument("--translation")
    args = parser.parse_args()
    output = Path(args.output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    qt_root = Path(PySide6.__file__).parent
    rcc = next(path for path in (qt_root / "rcc.exe", qt_root / "Qt/libexec/rcc") if path.is_file())
    resource = output / "custom.rcc"
    subprocess.run([str(rcc), "--binary", str(ROOT / "custom/custom.qrc"), "-o", str(resource)], check=True)
    # Production A8 retains the optional MT11 menu. Register its real image
    # assets as well, even though that menu is not shown in this product.
    tree = ElementTree.Element("RCC")
    assets = ElementTree.SubElement(tree, "qresource", prefix="/")
    for name in ("camera", "thermometer", "layers", "view-carousel"):
        ElementTree.SubElement(assets, "file", alias=f"InstrumentValueIcons/{name}.svg").text = str(ROOT / f"resources/InstrumentValueIcons/{name}.svg")
    icons = output / "native-icons.qrc"
    ElementTree.ElementTree(tree).write(icons, encoding="utf-8")
    subprocess.run([str(rcc), "--binary", str(icons), "-o", str(output / "native-icons.rcc")], check=True)
    app = QGuiApplication([])
    for filename in ("C:/Windows/Fonts/msyh.ttc", "C:/Windows/Fonts/consola.ttf"):
        if Path(filename).exists(): QFontDatabase.addApplicationFont(filename)
    assert QResource.registerResource(str(resource))
    assert QResource.registerResource(str(output / "native-icons.rcc"))
    a8, payload = A8Manager(), rouke.Manager()
    for manager in (a8, payload):
        manager.values.update(controlAvailable=True, sdkResponding=True, recordingKnown=True,
                              viewKnown=True, viewType=4, localMediaStorageEnabled=True)
    core, style = Core(a8, payload), ui.Style()
    qmlRegisterSingletonInstance(Core, "QGroundControl", 1, 0, "QGroundControl", core)
    ui.register_qml("QGroundControl.ScreenTools", "ScreenTools", STUBS / "ScreenTools.qml", True)
    palette = output / "QGCPalette.qml"
    palette.write_text((STUBS / "QGCPalette.qml").read_text(encoding="utf-8").replace(
        "QtObject {", 'QtObject {\n    property color colorGreen: "#00e04b"\n    property color colorRed: "#f32836"'), encoding="utf-8")
    ui.register_qml("QGroundControl.Palette", "QGCPalette", palette)
    for name in ("QGCLabel", "QGCColoredImage", "DeadMouseArea"):
        ui.register_qml("QGroundControl.Controls", name, ROOT / f"src/QmlControls/{name}.qml")
    qmlRegisterType(QQuickItem, "QGroundControl.Controls", 1, 0, "TerrainProgress")
    qmlRegisterType(QQuickItem, "QGroundControl.FlightDisplay", 1, 0, "PhotoVideoControl")
    qmlRegisterType(QQuickItem, "QGroundControl.FlightMap", 1, 0, "PreviewPlaceholder")
    warnings = []
    ui.qInstallMessageHandler(lambda kind, context, message: warnings.append(message))
    translator = QTranslator()
    if args.translation:
        assert translator.load(args.translation)
        app.installTranslator(translator)
    view = QQuickView()
    view.engine().setImportPathList([str(qt_root / "qml"), "qrc:/qt/qml"])
    view.rootContext().setContextProperty("layoutTestStyle", style)
    view.rootContext().setContextProperty("globals", {"activeVehicle": None})
    view.rootContext().setContextProperty("_rightPanelWidth", 0)
    harness = output / "preview.qml"
    harness.write_text('''
import QtQuick
Rectangle {
    objectName: "previewStage"
    width: 380; height: 660
    color: "#14222c"
    property bool settingsStage: false
    Text {
        anchors.top: parent.top; anchors.topMargin: 24
        anchors.horizontalCenter: parent.horizontalCenter
        text: "QML COMPONENT PREVIEW"; color: "#718b9c"
        font.family: "Microsoft YaHei"; font.pixelSize: 11; font.letterSpacing: 1.8
    }
    Loader {
        id: controls
        objectName: "productionColumn"
        x: parent.settingsStage ? parent.width - width - 20 : (parent.width - width) / 2
        y: 66
        source: "qrc:/Custom/qml/QGroundControl/FlightDisplay/FlyViewTopRightColumnLayout.qml"
    }
}
''', encoding="utf-8")
    view.setResizeMode(QQuickView.SizeRootObjectToView)
    view.resize(380, 660)
    view.setSource(QUrl.fromLocalFile(str(harness)))
    view.show()
    QTest.qWait(200)
    assert view.status() != QQuickView.Error, "\n".join(warnings)
    host = view.rootObject()
    column = host.findChild(QQuickItem, "productionColumn").property("item")
    assert column is not None, "\n".join(warnings)

    def named(name):
        result = host.findChild(QObject, name)
        if result is None:
            result = next((item for item in ui.descendants(view.contentItem()) if item.objectName() == name), None)
        assert result is not None, (name, "\n".join(warnings))
        return result

    def click(name):
        item = named(name)
        assert item.isEnabled(), name
        point = item.mapToScene(QPointF(item.width() / 2, item.height() / 2))
        assert 0 < point.x() < view.width() and 0 < point.y() < view.height(), (name, point)
        QTest.mouseClick(view, Qt.LeftButton, Qt.NoModifier, QPoint(int(point.x()), int(point.y())))
        QTest.qWait(100)

    def capture(name):
        QTest.mouseMove(view, QPoint(8, view.height() - 8))
        QTest.qWait(650)
        assert view.grabWindow().save(str(output / (name + ".png")))

    def icon_of(item):
        icons = [child for child in ui.descendants(item)
                 if child.metaObject().indexOfProperty("kind") >= 0 and child.isVisible()]
        assert len(icons) == 1, (item.objectName(), "One operation pictogram is expected")
        return icons[0]

    def surface_of(item):
        return item.property("background") if item.metaObject().indexOfProperty("background") >= 0 else item

    def is_recording_red(color):
        return color.red() > color.green() * 1.3 and color.red() > color.blue() * 1.3

    def sd_snapshot():
        row = named("sdStatusRow")
        return (row.property("known"), row.property("inserted"), row.property("statusText"),
                row.property("statusColor").name())

    def exercise_sd_states(manager, panel, prefix):
        row = named("sdStatusRow")
        contents = next(child for child in row.childItems()
                        if child.metaObject().indexOfProperty("spacing") >= 0)
        center = contents.mapToItem(panel, QPointF(contents.width() / 2, 0)).x()
        # Layouts may align the outer item to a whole device pixel.
        assert abs(center - panel.width() / 2) <= 1.01, (
            "The complete SD icon/label/state row must be centered", prefix,
            center, panel.width(), row.width(), contents.width(), row.x(), contents.x())
        texts = [str(child.property("text")) for child in ui.descendants(panel)
                 if child.isVisible() and child.metaObject().indexOfProperty("text") >= 0]
        assert not any("LOCAL" in text.upper() or "本地" in text for text in texts), texts
        snapshots = []
        for known, inserted, label in ((False, False, "unknown"), (True, True, "inserted"), (True, False, "absent")):
            manager.update(sdCardStatusKnown=known, sdCardInserted=inserted)
            assert row.property("known") == known and row.property("inserted") == inserted
            color = row.property("statusColor")
            assert (color.green() > color.red()) == (known and inserted), "Only a confirmed inserted card is green"
            if not (known and inserted):
                assert is_recording_red(color), "Unconfirmed/absent cards use the red indicator"
            labels = [str(child.property("text")) for child in ui.descendants(row)
                      if child.isVisible() and child.metaObject().indexOfProperty("text") >= 0]
            assert labels == ["SD"], "The resting status row contains only the dot and SD label"
            snapshots.append(sd_snapshot())
            if known: capture(prefix + "-sd-" + label)
        assert len({snapshot[2] for snapshot in snapshots}) == 3, "Each SD state must have distinct accessible wording"
        manager.update(sdCardStatusKnown=False, sdCardInserted=False)
        return sd_snapshot()

    def photo_press_preview(manager, reference_name, prefix):
        photo, reference = named("photoButton"), named(reference_name)
        surface = surface_of(photo)
        QTest.mouseMove(view, QPoint(8, view.height() - 8))
        QTest.qWait(160)
        assert surface.property("color").alpha() == 0, "Resting shutter must have no filled background"
        assert abs(photo.width() - reference.width()) < 1 and abs(photo.height() - reference.height()) < 1
        assert abs(icon_of(photo).width() - icon_of(reference).width()) < 1, "Operation pictograms share one size"
        point = photo.mapToScene(QPointF(photo.width() / 2, photo.height() / 2))
        before = list(manager.actions)
        QTest.mousePress(view, Qt.LeftButton, Qt.NoModifier, QPoint(int(point.x()), int(point.y())))
        QTest.qWait(160)  # Let the production 90/100 ms press animations settle.
        assert surface.property("color").alpha() > 0, "A held press must provide visible feedback"
        assert manager.actions == before, "Holding the shutter must not dispatch early"
        assert view.grabWindow().save(str(output / (prefix + "-photo-pressed.png")))
        QTest.mouseRelease(view, Qt.LeftButton, Qt.NoModifier, QPoint(int(point.x()), int(point.y())))
        QTest.mouseMove(view, QPoint(8, view.height() - 8))
        QTest.qWait(180)
        assert manager.actions == before + ["photo"], "Release dispatches exactly one photo"
        assert surface.property("color").alpha() == 0, "Release restores the transparent resting state"
        assert view.grabWindow().save(str(output / (prefix + "-photo-released.png")))

    capture("01-a8-controls")
    photo_press_preview(a8, "zoomInButton", "01a-a8")
    a8_panel = named("videoButton").parentItem()
    while a8_panel.metaObject().indexOfProperty("recordingSeconds") < 0:
        a8_panel = a8_panel.parentItem()
    frame_radius = a8_panel.property("radius")
    assert 0 < frame_radius < a8_panel.width() * 0.25, "A8 uses a rounded rectangular frame"
    idle_width, video_width = a8_panel.width(), named("videoButton").width()
    unknown_sd = exercise_sd_states(a8, a8_panel, "01c-a8")
    video_icon = icon_of(named("videoButton"))
    resting_record_color = video_icon.property("color")
    a8.update(recordingSessionActive=True, cameraRecordingPending=True)
    QTest.qWait(160)
    assert not is_recording_red(video_icon.property("color")), "Pending intent is not confirmed recording"
    assert sd_snapshot() == unknown_sd, "Recording intent must not imply that an SD card is inserted"
    capture("01d-a8-recording-pending")
    a8.update(cameraRecordingPending=False, recordingSessionCapturing=True, recordingSessionActive=True,
              recording=True, localRecording=True)
    a8_panel.setProperty("recordingSeconds", 5025)
    QTest.qWait(60)
    assert abs(a8_panel.width() - idle_width) < 1
    assert abs(named("videoButton").width() - video_width) < 1
    timer_label = named("videoStatusLabel")
    assert timer_label.property("paintedWidth") <= timer_label.width() + 1
    assert video_icon.property("kind") == "recordStop" and is_recording_red(video_icon.property("color"))
    assert sd_snapshot() == unknown_sd, "Confirmed capture still must not fabricate SD presence"
    capture("01b-a8-long-recording")
    a8.update(recordingSessionCapturing=False, recordingSessionActive=False, recording=False, localRecording=False)
    a8.recordingSessionStateChanged.emit()
    QTest.qWait(160)
    assert video_icon.property("kind") == "record" and video_icon.property("color") == resting_record_color
    for name in ("zoomInButton", "zoomOutButton", "photoButton", "videoButton"):
        click(name)
    assert all(action in a8.actions for action in ("zoomIn", "zoomOut", "photo", "video")), a8.actions
    zoom = named("zoomInButton")
    point = zoom.mapToScene(QPointF(zoom.width() / 2, zoom.height() / 2))
    tap_count = a8.actions.count("zoomIn")
    QTest.mousePress(view, Qt.LeftButton, Qt.NoModifier, QPoint(int(point.x()), int(point.y())))
    QTest.qWait(500)
    QTest.mouseRelease(view, Qt.LeftButton, Qt.NoModifier, QPoint(int(point.x()), int(point.y())))
    QTest.qWait(80)
    assert any(isinstance(action, tuple) and action[0] == "hold" for action in a8.actions), a8.actions
    assert "stopZoom" in a8.actions and a8.actions.count("zoomIn") == tap_count
    # Switch using the real keyboard-enabled selector while a zoom hold is
    # still active. Disposal must cancel A8 using its original manager.
    cancelled_before_switch = a8.actions.count("cancelZoom")
    QTest.mousePress(view, Qt.LeftButton, Qt.NoModifier, QPoint(int(point.x()), int(point.y())))
    QTest.qWait(500)
    named("selectRouke").forceActiveFocus()
    QTest.keyClick(view, Qt.Key_Space)
    QTest.qWait(150)
    QTest.mouseRelease(view, Qt.LeftButton, Qt.NoModifier, QPoint(int(point.x()), int(point.y())))
    assert column.property("_selectedCamera") == 1
    assert a8.actions.count("cancelZoom") > cancelled_before_switch, a8.actions
    assert payload.actions == [], "A8 disposal must not call into the Rouke manager"
    rouke_panel = named("settingsButton").parentItem()
    while rouke_panel.metaObject().indexOfProperty("settingsOpen") < 0:
        rouke_panel = rouke_panel.parentItem()
    assert abs(rouke_panel.property("radius") - frame_radius) < 1, "Payload frames share one corner radius"
    capture("02-rouke-controls")
    photo_press_preview(payload, "focusButton", "02a-rouke")
    unknown_sd = exercise_sd_states(payload, rouke_panel, "02c-rouke")
    assert host.findChild(QObject, "stopButton") is None, "Rouke presents one recording toggle"
    record_icon = icon_of(named("recordButton"))
    resting_record_color = record_icon.property("color")
    rail_width, record_width = rouke_panel.width(), named("recordButton").width()
    click("recordButton")
    assert payload.actions[-1] == "start"
    QTest.qWait(160)
    assert not is_recording_red(record_icon.property("color")), "Pending intent is not confirmed recording"
    assert sd_snapshot() == unknown_sd
    assert named("recordButton").isEnabled() and record_icon.property("kind") == "recordStop"
    capture("02d-rouke-recording-pending")
    click("recordButton")
    assert payload.actions[-1] == "stop", "The same button stops a start that is awaiting its ACK"
    # The transport's C++ tests decide when this obligation persists. The UI
    # must respect a supplied stop obligation after a rejected/lost response.
    payload.update(pending=False, cameraRecordingPending=False, lastError="Stop acknowledgement timed out")
    QTest.qWait(160)
    assert not is_recording_red(record_icon.property("color"))
    assert record_icon.property("kind") == "recordStop" and named("recordButton").isEnabled()
    capture("02e-rouke-recording-retry")
    click("recordButton")
    assert payload.actions[-1] == "stop", "A retained stop obligation must never restart recording"
    payload.finish_recording_stop()
    QTest.mouseMove(view, QPoint(8, view.height() - 8))
    QTest.qWait(160)
    assert record_icon.property("kind") == "record" and record_icon.property("color") == resting_record_color
    click("recordButton")
    assert payload.actions[-1] == "start"
    payload.update(pending=False, cameraRecordingPending=False, recordingSessionCapturing=True,
                   recordingSessionActive=True, recording=True, localRecording=True)
    rouke_panel.setProperty("recordingSeconds", 5025)
    QTest.qWait(150)
    assert is_recording_red(record_icon.property("color")), "Confirmed recording is red"
    assert record_icon.property("kind") == "recordStop"
    assert abs(rouke_panel.width() - rail_width) < 1 and abs(named("recordButton").width() - record_width) < 1
    timer_label = named("videoStatusLabel")
    assert timer_label.property("paintedWidth") <= timer_label.width() + 1
    assert sd_snapshot() == unknown_sd
    capture("02b-rouke-recording")
    click("recordButton")
    assert payload.actions[-1] == "stop"
    payload.finish_recording_stop()
    QTest.mouseMove(view, QPoint(8, view.height() - 8))
    QTest.qWait(160)
    assert record_icon.property("kind") == "record" and record_icon.property("color") == resting_record_color
    previous_actions = list(payload.actions)
    click("viewButton")
    capture("03-rouke-lens-menu")
    click("view_5")
    assert payload.actions == previous_actions + [5]
    payload.update(viewKnown=True, viewType=5)
    # The same production column and camera selector remain visible next to
    # the expanded settings; there is no fabricated navigation in this host.
    host.setProperty("settingsStage", True)
    view.resize(880, 900)
    QTest.qWait(120)
    click("settingsButton")
    popup = named("settingsPopup")
    assert popup.property("visible")
    assert payload.actions == previous_actions + [5], "Opening settings must not send a command"
    camera = named("cameraSettingsScroll")
    for fraction, label in ((0, "04-camera-upper"), (0.5, "05-camera-middle"), (1, "06-camera-lower")):
        camera.setProperty("contentY", max(0, camera.property("contentHeight") - camera.height()) * fraction)
        capture(label)
    click("rangeTab")
    assert named("parameter_triggerPhoto").property("draftValue") == 0
    assert not named("parameter_triggerPhoto").property("known")
    capture("07-range-default")
    ranging = named("rangeSettingsScroll")
    ranging.setProperty("contentY", max(0, ranging.property("contentHeight") - ranging.height()))
    capture("08-range-lower")
    # Actual production selector disposes the Rouke rail and its popup.
    click("selectA8")
    assert column.property("_selectedCamera") == 0
    assert not isValid(popup) or not popup.property("visible")
    capture("09-a8-settings-closed")
    view.setSource(QUrl())
    QTest.qWait(60)
    errors = [message for message in warnings if any(token in message for token in
              ("TypeError", "ReferenceError", "Binding loop", "Unable to assign", "Cannot assign", "is not a type", "Cannot open"))]
    assert not errors, "\n".join(errors)
    print("PASS: production selector, A8 actions, Rouke single recording toggle/pending stop/retry/timer, red-green SD states, settings previews and switch lifecycle; no QML errors")
    view.close()


if __name__ == "__main__":
    main()
