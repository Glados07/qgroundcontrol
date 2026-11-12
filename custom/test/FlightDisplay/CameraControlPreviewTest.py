"""Render and exercise production A8/Rouke selectors, rails and settings.

Application services, telemetry and unrelated terrain/photo fallback use test
doubles. The overridden column loads at its native QRC base URL; custom module
exports come from the production CMake declaration. This models the effective
URL override without executing the C++ interceptor or compiled QML cache.
Screenshots are real Qt Quick component previews, not full QGC runs.
"""
import argparse
from pathlib import Path
import re
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
    def toggleVideoRecording(self):
        self.actions.append("stop" if self.values["recordingSessionActive"] else "video")

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


class PreviewStyle(ui.Style):
    mobileChanged = Signal()
    mobile_value = False
    viewport_height = 660.0
    # 4 physical pixels/mm is the existing ScreenTools fixture density.
    mobile = Property(bool, lambda self: self.mobile_value, notify=mobileChanged)
    viewportHeight = Property(float, lambda self: self.viewport_height, notify=mobileChanged)


def production_module_resources(output, rcc):
    """Build real QML exports from CMake, not a hand-maintained type list.

    This fixture omits Qt's compiled QML cache/plugin but uses the actual
    Custom.FlightDisplay source files, aliases, URI and resource prefix.
    """
    cmake = (ROOT / "custom/CMakeLists.txt").read_text(encoding="utf-8")
    cmake = re.sub(r"#[^\n]*", "", cmake)
    block = re.search(r"qt_add_qml_module\(CustomFlightDisplayModule\b(.*?)\)", cmake, re.S)
    assert block, "The production CustomFlightDisplayModule declaration is required"
    declaration = block.group(1)
    uri = re.search(r"\bURI\s+(\S+)", declaration).group(1)
    version = re.search(r"\bVERSION\s+(\S+)", declaration).group(1)
    prefix = re.search(r"\bRESOURCE_PREFIX\s+(\S+)", declaration).group(1)
    files = re.search(r"\bQML_FILES\s+(.*?)(?:\bNO_PLUGIN\b|$)", declaration, re.S).group(1).split()
    assert files and all(filename.endswith(".qml") and "${" not in filename for filename in files), files
    aliases = {}
    for properties in re.findall(r"set_source_files_properties\((.*?)\)", cmake, re.S):
        if "PROPERTIES" not in properties:
            continue
        sources, values = properties.split("PROPERTIES", 1)
        alias = re.search(r"\bQT_RESOURCE_ALIAS\s+(\S+)", values)
        if alias:
            for source in sources.split():
                aliases[source] = alias.group(1).strip('"')
    tree = ElementTree.Element("RCC")
    module_prefix = prefix.rstrip("/") + "/" + uri.replace(".", "/")
    module = ElementTree.SubElement(tree, "qresource", prefix=module_prefix)
    qmldir_lines = ["module " + uri]
    for filename in files:
        alias = aliases.get(filename, filename)
        ElementTree.SubElement(module, "file", alias=alias).text = str(ROOT / "custom" / filename)
        qmldir_lines.append(f"{Path(filename).stem} {version} {alias}")
    qmldir = output / "production-custom-flightdisplay.qmldir"
    qmldir.write_text("\n".join(qmldir_lines) + "\n", encoding="utf-8")
    ElementTree.SubElement(module, "file", alias="qmldir").text = str(qmldir)
    # Match the interceptor's effective content + native base URL. A Python
    # QQmlAbstractUrlInterceptor deadlocks between the GIL and Qt's loader
    # thread, so this does not execute the production C++ interceptor itself.
    # Crucially, do not put the new camera siblings at this native location:
    # they must be exported through the real production module declaration.
    native = ElementTree.SubElement(tree, "qresource", prefix="/qml/QGroundControl/FlightDisplay")
    ElementTree.SubElement(native, "file", alias="FlyViewTopRightColumnLayout.qml").text = str(
        ROOT / "custom/src/FlightDisplay/FlyViewTopRightColumnLayout.qml")
    source = output / "production-modules.qrc"
    ElementTree.ElementTree(tree).write(source, encoding="utf-8")
    resource = output / "production-modules.rcc"
    subprocess.run([str(rcc), "--binary", str(source), "-o", str(resource)], check=True)
    return resource


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", default=str(ROOT / "debug/camera-icons-preview"))
    parser.add_argument("--translation")
    parser.add_argument("--mobile-only", action="store_true", help="Run just constrained mobile/offline cases")
    args = parser.parse_args()
    output = Path(args.output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    qt_root = Path(PySide6.__file__).parent
    rcc = next(path for path in (qt_root / "rcc.exe", qt_root / "Qt/libexec/rcc") if path.is_file())
    resource = output / "custom.rcc"
    subprocess.run([str(rcc), "--binary", str(ROOT / "custom/custom.qrc"), "-o", str(resource)], check=True)
    modules_resource = production_module_resources(output, rcc)
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
    assert QResource.registerResource(str(modules_resource))
    assert QResource.registerResource(str(output / "native-icons.rcc"))
    a8, payload = A8Manager(), rouke.Manager()
    for manager in (a8, payload):
        manager.values.update(controlAvailable=True, sdkResponding=True, recordingKnown=True,
                              viewKnown=True, viewType=4, localMediaStorageEnabled=True)
    core, style = Core(a8, payload), PreviewStyle()
    qmlRegisterSingletonInstance(Core, "QGroundControl", 1, 0, "QGroundControl", core)
    screen_tools = output / "ScreenTools.qml"
    screen_tools.write_text((STUBS / "ScreenTools.qml").read_text(encoding="utf-8").replace(
        "property bool isMobile: false", "property bool isMobile: layoutTestStyle.mobile\n"
        "    readonly property real minTouchPixels: Math.round(5 * realPixelDensity) / layoutTestStyle.viewportHeight > 0.15\n"
        "                                               ? defaultFontPixelHeight * 3 : Math.round(5 * realPixelDensity)"), encoding="utf-8")
    ui.register_qml("QGroundControl.ScreenTools", "ScreenTools", screen_tools, True)
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
    view.engine().setImportPathList([str(qt_root / "qml"), "qrc:/qml", "qrc:/qt/qml"])
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
    property bool mobileStage: false
    Text {
        anchors.top: parent.top; anchors.topMargin: 24
        anchors.horizontalCenter: parent.horizontalCenter
        text: "QML COMPONENT PREVIEW"; color: "#718b9c"
        font.family: "Microsoft YaHei"; font.pixelSize: 11; font.letterSpacing: 1.8
    }
    Loader {
        id: controls
        objectName: "productionColumn"
        x: parent.settingsStage || parent.mobileStage ? parent.width - width - 20 : (parent.width - width) / 2
        anchors.top: parent.top
        anchors.topMargin: parent.mobileStage ? 48 : 66
        anchors.bottom: parent.mobileStage ? compassReserve.top : undefined
        anchors.bottomMargin: parent.mobileStage ? 8 : 0
        source: "qrc:/qml/QGroundControl/FlightDisplay/FlyViewTopRightColumnLayout.qml"
    }
    Rectangle {
        id: compassReserve
        objectName: "compassReserve"
        visible: parent.mobileStage
        width: 120 * layoutTestStyle.scale; height: 70 * layoutTestStyle.scale
        anchors.right: parent.right; anchors.rightMargin: 20
        anchors.bottom: parent.bottom; anchors.bottomMargin: 12
        radius: 12; color: "#243945"; border.color: "#476373"
        Text { anchors.centerIn: parent; text: "COMPASS SPACE"; color: "#a9bbc5"; font.pixelSize: 11 }
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

    def ancestor_with_property(item, property_name):
        while item.metaObject().indexOfProperty(property_name) < 0:
            item = item.parentItem()
            assert item is not None, property_name
        return item

    def assert_equal_panel_widths(record_name):
        selector = ancestor_with_property(named("selectA8"), "selectedCamera")
        panel = ancestor_with_property(named(record_name), "recordingSeconds")
        assert abs(selector.width() - panel.width()) <= 1, ("Selector and actions must have one width", selector.width(), panel.width())
        assert abs(selector.mapToScene(QPointF()).x() - panel.mapToScene(QPointF()).x()) <= 1
        # The former rail was 134 px at the fixture's 20 px base font.
        assert panel.width() < 134 * style.scale_value, "Lower action panel should be narrower"

    def assert_settings_anchor(popup):
        viewport = named("cameraColumnScroll")
        panel = ancestor_with_property(named("settingsButton"), "settingsOpen")
        assert panel.property("settingsAnchorItem") == viewport, (
            "Settings must anchor to the visible column, including its selector")
        top = viewport.mapToScene(QPointF()).y()
        margin = popup.property("edgeMargin")
        popup_top = popup.property("y")
        assert popup.property("width") <= 480 * style.scale_value + 1
        assert popup.property("height") <= 600 * style.scale_value + 1
        assert popup.property("x") >= 0 and popup_top >= 0
        assert popup.property("x") + popup.property("width") <= view.width() + 1
        assert popup_top + popup.property("height") <= view.height() + 1
        if top >= margin and view.height() - top - margin >= popup.property("minimumUsableHeight"):
            assert abs(popup_top - top) <= 1, ("Popup and entire camera column must share a top edge", popup_top, top)
        if viewport.property("contentHeight") > viewport.height() + 1:
            original = viewport.property("contentY")
            viewport.setProperty("contentY", viewport.property("contentHeight") - viewport.height())
            QTest.qWait(50)
            assert abs(popup.property("y") - popup_top) <= 1, "Scrolling the rail must not move its settings popup"
            viewport.setProperty("contentY", original)
            QTest.qWait(50)

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
            center, panel.width(), row.width(), contents.width(), row.x(), contents.x(),
            "grid", row.parentItem().x(), row.parentItem().width(), row.parentItem().implicitWidth(),
            [(child.objectName(), child.width(), child.implicitWidth(), child.parentItem().width())
             for child in ui.descendants(panel) if child.objectName() == "zoomActionsGrid"])
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

    if not args.mobile_only:
        assert_equal_panel_widths("videoButton")
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
        assert_equal_panel_widths("recordButton")
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
        refreshes_before = payload.refresh_requests
        click("settingsButton")
        QTest.qWait(160)
        popup = named("settingsPopup")
        assert popup.property("visible")
        assert_settings_anchor(popup)
        assert payload.actions == previous_actions + [5], "Opening settings must not send a write"
        assert payload.refresh_requests == refreshes_before, "Settings only displays the cache"
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

    # Match FlyViewWidgetLayer's top/bottom anchoring above the instruments.
    # The compass is a labelled space reservation, not a fabricated QGC view.
    host.setProperty("settingsStage", False)
    host.setProperty("mobileStage", True)
    style.mobile_value = True
    touch_device = QTest.createTouchDevice()

    def reveal(item):
        scroll = named("cameraColumnScroll")
        for _ in range(25):
            p = item.mapToItem(scroll, QPointF(0, 0))
            if p.y() >= -0.5 and p.y() + item.height() <= scroll.height() + 0.5:
                return
            direction = 120 if p.y() < 0 else -120
            wheel_point = scroll.mapToScene(QPointF(2, scroll.height() / 2))
            QTest.wheelEvent(view, wheel_point, QPoint(0, direction))
            QTest.qWait(50)
        view.grabWindow().save(str(output / "mobile-scroll-failure.png"))
        raise AssertionError(("Action cannot be scrolled into view", item.objectName(), p, item.height(),
                              scroll.height(), scroll.property("contentY"), scroll.property("contentHeight")))

    def touch_scroll(scroll, start_item=None):
        scroll.setProperty("contentY", 0)
        if start_item is not None:
            reveal(start_item)
            start = start_item.mapToScene(QPointF(start_item.width() / 2, start_item.height() / 2))
        else:
            start = scroll.mapToScene(QPointF(scroll.width() / 2, scroll.height() * 0.8))
        origin = scroll.property("contentY")
        direction = 1 if origin > 0 else -1
        gesture = QTest.touchEvent(view, touch_device, False)
        gesture.press(0, QPoint(round(start.x()), round(start.y())), view).commit()
        QTest.qWait(30)
        for step in range(1, 6):
            gesture.move(0, QPoint(round(start.x()), round(start.y() + direction * step * scroll.height() * 0.07)), view).commit()
            QTest.qWait(30)
        gesture.release(0, QPoint(round(start.x()), round(start.y() + direction * scroll.height() * 0.35)), view).commit()
        QTest.qWait(150)
        if abs(scroll.property("contentY") - origin) < 1:
            view.grabWindow().save(str(output / "mobile-touch-failure.png"))
        assert abs(scroll.property("contentY") - origin) >= 1, (
            "A finger drag must scroll the column", start_item.objectName() if start_item else "column gap",
            origin, scroll.property("contentY"), scroll.property("contentHeight"), scroll.height())
        scroll.setProperty("contentY", 0)

    def click_mobile(name):
        reveal(named(name))
        click(name)

    def offline_settings_preview(width, height, scale):
        before = list(payload.actions)
        # A configured endpoint permits read-only first-response probes.
        # Parameter writes still require a responding device.
        payload.update(controlAvailable=True, sdkResponding=False)
        refreshes_before = payload.refresh_requests
        click_mobile("settingsButton")
        QTest.qWait(160)  # Wait for the popup entrance transition.
        assert payload.refresh_requests == refreshes_before
        popup = named("settingsPopup")
        assert popup.property("visible") and popup.property("selectedPage") == 0
        assert_settings_anchor(popup)
        camera_form = named("cameraSettingsScroll")
        iso_position = named("editor_iso").mapToItem(camera_form.property("contentItem"), QPointF())
        camera_form.setProperty("contentY", max(0, min(iso_position.y() - 10,
                                                       camera_form.property("contentHeight") - camera_form.height())))
        QTest.qWait(60)
        assert named("editor_iso").isEnabled()
        click("editor_iso")
        QTest.keyClick(view, Qt.Key_A, Qt.ControlModifier)
        QTest.keyClick(view, Qt.Key_1)
        QTest.keyClick(view, Qt.Key_2)
        assert named("parameter_iso").property("draftValue") == 12, (
            "Offline numeric edit", width, height, scale, named("editor_iso").property("text"))
        for item in host.findChildren(QObject):
            if item.objectName().startswith("apply_"):
                assert not item.isEnabled(), ("Offline parameter write enabled", item.objectName())
            elif item.objectName().startswith("read_"):
                assert item.isEnabled(), ("Offline GET probe unavailable", item.objectName())
        if width == 800 and scale == 1:
            capture("offline-camera-settings")
        click("rangeTab")
        assert popup.property("selectedPage") == 1
        ranging = named("rangeSettingsScroll")
        # Scroll editable rows into view without making the popup taller.
        def reveal_field(name):
            item = named(name)
            p = item.mapToItem(ranging.property("contentItem"), QPointF())
            ranging.setProperty("contentY", max(0, min(p.y() - 10,
                                                       ranging.property("contentHeight") - ranging.height())))
            QTest.qWait(60)
        reveal_field("choice_triggerPhoto")
        assert named("parameter_triggerPhoto").property("draftValue") == 0
        click("choice_triggerPhoto")
        QTest.keyClick(view, Qt.Key_End)
        QTest.keyClick(view, Qt.Key_Return)
        QTest.qWait(60)
        assert named("parameter_triggerPhoto").property("draftValue") == 1
        assert not named("apply_triggerPhoto").isEnabled()
        reveal_field("apply_triggerPhoto")
        point = named("apply_triggerPhoto").mapToScene(QPointF(named("apply_triggerPhoto").width() / 2,
                                                              named("apply_triggerPhoto").height() / 2))
        QTest.mouseClick(view, Qt.LeftButton, Qt.NoModifier, QPoint(round(point.x()), round(point.y())))
        assert payload.actions == before, "Offline trigger drafts must never send a capture command"
        # Return to the requested default before capturing the user preview.
        reveal_field("choice_triggerPhoto")
        click("choice_triggerPhoto")
        QTest.keyClick(view, Qt.Key_Home)
        QTest.keyClick(view, Qt.Key_Return)
        QTest.qWait(60)
        assert named("parameter_triggerPhoto").property("draftValue") == 0
        if width == 800 and scale == 1:
            capture("offline-range-settings")
        reveal_field("editor_shootingDistance")
        click("editor_shootingDistance")
        QTest.keyClick(view, Qt.Key_A, Qt.ControlModifier)
        QTest.keyClick(view, Qt.Key_3)
        QTest.keyClick(view, Qt.Key_0)
        assert named("parameter_shootingDistance").property("draftValue") == 30
        assert not named("apply_shootingDistance").isEnabled()
        assert payload.actions == before
        click("cameraTab")
        payload.update(sdkResponding=True)
        assert popup.property("visible") and named("parameter_iso").property("draftValue") == 12
        assert payload.actions == before, "Reconnection must not apply a saved draft"
        assert payload.refresh_requests == refreshes_before, "Navigation and connectivity changes must not send reads"
        payload.update(sdkResponding=False)
        assert popup.property("visible"), "The page must remain open when the device disconnects"
        assert named("parameter_iso").property("draftValue") == 12
        assert named("editor_iso").isEnabled() and not named("apply_iso").isEnabled()
        # Real keyboard navigation activates the selector even on a small
        # screen where the popup covers it; disposal must close the popup.
        cancellations_before = payload.refresh_cancellations
        named("selectA8").forceActiveFocus()
        QTest.keyClick(view, Qt.Key_Space)
        QTest.qWait(160)
        assert column.property("_selectedCamera") == 0
        assert not isValid(popup) or not popup.property("visible")
        assert payload.refresh_cancellations == cancellations_before, "Payload switching must not cancel backend event reads"
        assert payload.actions == before
        click_mobile("selectRouke")
        QTest.qWait(160)

    for width, height, scale in ((731, 411, 1.0), (800, 480, 1.0),
                                 (731, 411, 1.5), (800, 480, 1.5)):
        style.scale_value, style.viewport_height = scale, height
        style.changed.emit()
        style.mobileChanged.emit()
        view.resize(width, height)
        QTest.qWait(180)
        for manager, selector, prefix, action_names, record_name in (
                (a8, "selectA8", "a8", ("zoomInButton", "zoomOutButton", "photoButton", "videoButton"), "videoButton"),
                (payload, "selectRouke", "rouke", ("viewButton", "centerButton", "focusButton", "photoButton", "recordButton", "settingsButton"), "recordButton")):
            manager.finish_recording_stop()
            manager.update(controlAvailable=True, sdkResponding=True, lastError="")
            click_mobile(selector)
            QTest.qWait(160)
            scroll = named("cameraColumnScroll")
            compass = named("compassReserve")
            assert named("cameraActionsGrid").property("columns") == 2
            assert_equal_panel_widths(record_name)
            viewport_bottom = scroll.mapToScene(QPointF(0, scroll.height())).y()
            assert viewport_bottom <= compass.mapToScene(QPointF(0, 0)).y() - 7, "Camera viewport overlaps compass"
            assert scroll.property("clip") and scroll.height() > 90 * scale
            if scroll.property("contentHeight") > scroll.height() + 1:
                touch_scroll(scroll)
            minimum_touch = max(20, 20 * scale * 2.25)
            for name in action_names:
                item = named(name)
                assert item.width() + 1 >= minimum_touch and item.height() + 1 >= minimum_touch, (
                    "Mobile touch targets must not shrink", name, item.width(), item.height(), minimum_touch)
                reveal(item)
                assert item.isEnabled(), ("Online action disabled", name)
            reveal(named("sdStatusRow"))  # Bottom status remains reachable above the compass.
            # At least the shutter must dispatch after genuine scrolling.
            before = list(manager.actions)
            click_mobile("photoButton")
            assert manager.actions == before + ["photo"]
            if scroll.property("contentHeight") > scroll.height() + 1:
                before = list(manager.actions)
                touch_scroll(scroll, named("photoButton"))
                assert manager.actions == before, "Dragging from the shutter must cancel its click"
            scroll.setProperty("contentY", 0)
            capture(f"mobile-{prefix}-{width}x{height}-{scale}-online")

            # Keep service-level local-media capability true. The visual
            # online gate, not the fixture, must reject new offline actions.
            manager.update(controlAvailable=False, sdkResponding=False, lastError="Camera response timed out")
            QTest.qWait(160)
            before = list(manager.actions)
            for name in action_names:
                item = named(name)
                reveal(item)
                if name == "settingsButton":
                    assert item.isEnabled(), "Offline settings navigation remains available"
                    continue
                assert not item.isEnabled(), ("Offline action remains enabled", name)
                assert item.opacity() <= 0.5, ("Offline action must be visibly greyed", name, item.opacity())
                point = item.mapToScene(QPointF(item.width() / 2, item.height() / 2))
                QTest.mouseClick(view, Qt.LeftButton, Qt.NoModifier, QPoint(int(point.x()), int(point.y())))
                QTest.qWait(30)
            assert manager.actions == before, "Offline clicks must not dispatch camera actions"
            assert not any(item.isVisible() and item.property("kind") == "warning"
                           for item in ui.descendants(column)), "Primary rail must not show a warning triangle"
            scroll.setProperty("contentY", 0)
            capture(f"mobile-{prefix}-{width}x{height}-{scale}-offline")

            if prefix == "rouke":
                offline_settings_preview(width, height, scale)

            # Disconnecting a live recording must leave its STOP reachable.
            manager.update(recordingSessionActive=True, recordingSessionCapturing=True,
                           recordingStopRequired=True, recording=True)
            click_mobile(record_name)
            assert manager.actions[-1] == "stop", "An offline active recording must still be stoppable"
            manager.finish_recording_stop()
            manager.update(controlAvailable=True, sdkResponding=True, lastError="")
        # At large fonts the real column must scroll, preserving its bottom.
        if scroll.property("contentHeight") > scroll.height() + 1:
            scroll.setProperty("contentY", 0)
            point = scroll.mapToScene(QPointF(2, scroll.height() / 2))
            QTest.wheelEvent(view, point, QPoint(0, -240))
            QTest.qWait(100)
            assert scroll.property("contentY") > 0, "Constrained column must scroll"
            reveal(named("sdStatusRow"))
            capture(f"mobile-rouke-{width}x{height}-{scale}-scrolled")
    view.setSource(QUrl())
    QTest.qWait(60)
    errors = [message for message in warnings if any(token in message for token in
              ("TypeError", "ReferenceError", "Binding loop", "Unable to assign", "Cannot assign", "is not a type", "Cannot open"))]
    assert not errors, "\n".join(errors)
    scope = "mobile cases only" if args.mobile_only else "desktop actions/settings and mobile cases"
    print(f"PASS: native QRC module entry, {scope}, dual columns at 731x411/800x480 and 100/150%, compass bounds, wheel/touch scrolling, offline grey disabled actions/no dispatch, disconnected recording STOP; no QML errors")
    view.close()


if __name__ == "__main__":
    main()
