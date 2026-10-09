"""Render the production UniRC toolbar/drawer QML and exercise real clicks.

Run with PySide6: python custom/test/UI/UniRcLinkIndicatorTest.py
The real custom resource and native ToolIndicatorPage/QGCLabel are loaded.
Only app services, metrics, the palette and adjacent indicators are doubles.
Offscreen software rendering; no SDK connection or persistent settings.
"""

import os
from pathlib import Path
import subprocess

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
os.environ.setdefault("QT_QUICK_BACKEND", "software")

import PySide6

QT_ROOT = Path(PySide6.__file__).parent
os.environ["QT_PLUGIN_PATH"] = str(QT_ROOT / "plugins")
os.environ["QT_QPA_PLATFORM_PLUGIN_PATH"] = str(QT_ROOT / "plugins/platforms")

from PySide6.QtCore import (QObject, Property, Signal, Slot, QResource, QUrl,
                            QPointF, Qt, QTranslator, qInstallMessageHandler)
from PySide6.QtGui import QGuiApplication, QFontDatabase, QColor, QImage, QPainter
from PySide6.QtQml import (QQmlComponent, QQmlProperty, qmlRegisterType,
                           qmlRegisterSingletonType, qmlRegisterSingletonInstance)
from PySide6.QtQuick import QQuickItem, QQuickView
from PySide6.QtTest import QTest

ROOT = Path(__file__).resolve().parents[3]
OUTPUT = ROOT / "debug/unirc-link-indicator"
FIXTURES = ROOT / "custom/test/UI/FlyViewSettingsLayout"
WARNINGS = []


class Style(QObject):
    changed = Signal()
    scale_value = 1.0
    light_value = False
    scale = Property(float, lambda self: self.scale_value, notify=changed)
    light = Property(bool, lambda self: self.light_value, notify=changed)


class LinkStatus(QObject):
    statusChanged = Signal()
    valid = False
    strength = 0
    quality = 0
    available = Property(bool, lambda self: self.valid, notify=statusChanged)
    strengthPercent = Property(int, lambda self: self.strength, notify=statusChanged)
    strengthLevel = Property(int, lambda self: self.strength // 20, notify=statusChanged)
    qualityLevel = Property(int, lambda self: self.quality, notify=statusChanged)
    signalQuality = Property(int, lambda self: (-15, 5, 10)[self.quality], notify=statusChanged)

    def update(self, strength, quality, available=True):
        self.strength, self.quality, self.valid = strength, quality, available
        self.statusChanged.emit()


class Controller(QObject):
    def __init__(self):
        super().__init__()
        self.status = LinkStatus()

    linkStatus = Property(QObject, lambda self: self.status, constant=True)
    transportConnected = Property(bool, lambda self: True, constant=True)


class Plugin(QObject):
    changed = Signal()

    def __init__(self, controller, adjacent_url):
        super().__init__()
        self.controller = controller
        self.adjacent_url = adjacent_url

    uniRcChannelController = Property(QObject, lambda self: self.controller, notify=changed)
    toolBarIndicators = Property("QVariantList", lambda self: [self.adjacent_url], constant=True)


class VehicleLinkManager(QObject):
    changed = Signal()
    lost = False
    communicationLost = Property(bool, lambda self: self.lost, notify=changed)


class Vehicle(QObject):
    def __init__(self, adjacent_url, gimbal_url):
        super().__init__()
        self.adjacent_url = adjacent_url
        self.gimbal_url = gimbal_url
        self.link_manager = VehicleLinkManager()

    toolIndicators = Property("QVariantList", lambda self: [self.adjacent_url, self.gimbal_url, self.adjacent_url], constant=True)
    modeIndicators = Property("QVariantList", lambda self: [self.adjacent_url], constant=True)
    vehicleLinkManager = Property(QObject, lambda self: self.link_manager, constant=True)


class VehicleManager(QObject):
    changed = Signal()

    def __init__(self, vehicle):
        super().__init__()
        self.current = vehicle

    activeVehicle = Property(QObject, lambda self: self.current, notify=changed)
    vehicle = Property(QObject, lambda self: self.current, notify=changed)
    parameterReadyVehicleAvailable = Property(bool, lambda self: False, constant=True)


class Core(QObject):
    def __init__(self, plugin, vehicle_manager):
        super().__init__()
        self.plugin, self.manager = plugin, vehicle_manager

    corePlugin = Property(QObject, lambda self: self.plugin, constant=True)
    multiVehicleManager = Property(QObject, lambda self: self.manager, constant=True)
    globalPalette = Property("QVariantMap", lambda self: {"groupBorder": "#707070"}, constant=True)


class MainWindow(QObject):
    widthChanged = Signal()
    view = None
    opened_count = 0
    opened_indicator = None
    page = None
    current_width = 800
    width = Property(float, lambda self: self.current_width, notify=widthChanged)

    @Slot(QObject, QObject)
    def showIndicatorDrawer(self, component, indicator):
        self.opened_count += 1
        self.opened_indicator = indicator
        self.close_drawer()
        self.page = component.create(component.creationContext())
        assert self.page, [error.toString() for error in component.errors()]
        surface = self.view.rootObject().findChild(QQuickItem, "drawerSurface")
        self.page.setParent(surface)
        self.page.setParentItem(surface)
        padding = surface.property("padding")
        self.page.setPosition(QPointF(padding, padding))
        surface.setProperty("contentPage", self.page)

    def close_drawer(self):
        if self.page:
            surface = self.view.rootObject().findChild(QQuickItem, "drawerSurface")
            surface.setProperty("contentPage", None)
            self.page.setParentItem(None)
            self.page.deleteLater()
            self.page = None


HARNESS = b"""
import QtQuick
import QGroundControl.Palette
import QGroundControl.ScreenTools

Rectangle {
    width: 800
    height: 560
    color: previewPalette.windowShade
    QGCPalette { id: previewPalette }
    Rectangle {
        width: parent.width
        height: ScreenTools.defaultFontPixelHeight * 3
        color: previewPalette.window
        Loader {
            objectName: "toolbarLoader"
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.rightMargin: ScreenTools.defaultFontPixelWidth * 2
            source: "qrc:/Custom/qml/QGroundControl/Controls/FlyViewToolBarIndicators.qml"
        }
    }
    Rectangle {
        objectName: "drawerSurface"
        property Item contentPage: null
        property real padding: ScreenTools.defaultFontPixelHeight / 2
        visible: !!contentPage
        x: Math.max(8, parent.width - width - 16)
        y: ScreenTools.defaultFontPixelHeight * 3.5
        width: contentPage ? contentPage.width + padding * 2 : 0
        height: contentPage ? contentPage.height + padding * 2 : 0
        color: previewPalette.window
        radius: ScreenTools.defaultFontPixelHeight / 4
        border.width: 1
        border.color: previewPalette.groupBorder
    }
}
"""


def descendants(item):
    for child in item.childItems():
        yield child
        yield from descendants(child)


def find_item(item, name):
    # Repeater delegates live in the visual tree, which can differ from QObject ownership.
    return next((child for child in descendants(item) if child.objectName() == name), None)


def settle():
    QTest.qWait(80)


def main():
    OUTPUT.mkdir(parents=True, exist_ok=True)
    rcc = QT_ROOT / ("rcc.exe" if os.name == "nt" else "rcc")
    lrelease = QT_ROOT / ("lrelease.exe" if os.name == "nt" else "lrelease")
    subprocess.run([str(rcc), "--binary", str(ROOT / "custom/custom.qrc"),
                    "-o", str(OUTPUT / "custom.rcc")], check=True)
    subprocess.run([str(lrelease), str(ROOT / "custom/translations/custom_zh_CN.ts"),
                    "-qm", str(OUTPUT / "custom_zh_CN.qm")], check=True)
    adjacent = OUTPUT / "AdjacentIndicator.qml"
    adjacent.write_text('''import QtQuick
Item {
    objectName: "adjacentIndicator"
    property bool showIndicator: true
    anchors.top: parent.top
    anchors.bottom: parent.bottom
    width: height
    Rectangle { anchors.centerIn: parent; width: parent.width * 0.5; height: width;
                radius: width / 2; color: "#86949b"; opacity: 0.5 }
}
''', encoding="utf-8")
    gimbal = OUTPUT / "GimbalIndicator.qml"
    gimbal.write_text(adjacent.read_text(encoding="utf-8").replace(
        'objectName: "adjacentIndicator"', 'objectName: "gimbalIndicator"'), encoding="utf-8")
    app = QGuiApplication([])
    for filename in ("C:/Windows/Fonts/msyh.ttc", "C:/Windows/Fonts/consola.ttf"):
        if Path(filename).exists():
            assert QFontDatabase.addApplicationFont(filename) >= 0
    qInstallMessageHandler(lambda kind, context, message: WARNINGS.append(message))
    assert QResource.registerResource(str(OUTPUT / "custom.rcc"))
    translator = QTranslator()
    assert translator.load(str(OUTPUT / "custom_zh_CN.qm"))
    app.installTranslator(translator)
    style, controller, window = Style(), Controller(), MainWindow()
    adjacent_url = QUrl.fromLocalFile(str(adjacent))
    plugin = Plugin(controller, adjacent_url)
    vehicle = Vehicle(adjacent_url, QUrl.fromLocalFile(str(gimbal)))
    manager = VehicleManager(vehicle)
    core = Core(plugin, manager)
    qmlRegisterSingletonInstance(Core, "QGroundControl", 1, 0, "QGroundControl", core)
    qmlRegisterType(QObject, "QGroundControl.Toolbar", 1, 0, "Placeholder")
    qmlRegisterSingletonType(QUrl.fromLocalFile(str(FIXTURES / "ScreenTools.qml")),
                             "QGroundControl.ScreenTools", 1, 0, "ScreenTools")
    qmlRegisterType(QUrl.fromLocalFile(str(FIXTURES / "QGCPalette.qml")),
                    "QGroundControl.Palette", 1, 0, "QGCPalette")
    for name in ("QGCLabel", "ToolIndicatorPage"):
        qmlRegisterType(QUrl.fromLocalFile(str(ROOT / f"src/QmlControls/{name}.qml")),
                        "QGroundControl.Controls", 1, 0, name)
    for name in ("UniRcSignalIcon", "UniRcLinkIndicatorPage"):
        qmlRegisterType(QUrl.fromLocalFile(str(ROOT / f"custom/src/QmlControls/{name}.qml")),
                        "Custom.Widgets", 1, 0, name)

    view = QQuickView()
    window.view = view
    view.engine().setImportPathList([str(QT_ROOT / "qml"), "qrc:/qt/qml"])
    view.rootContext().setContextProperty("layoutTestStyle", style)
    view.rootContext().setContextProperty("mainWindow", window)
    view.setResizeMode(QQuickView.SizeRootObjectToView)
    component = QQmlComponent(view.engine())
    harness_url = QUrl.fromLocalFile(str(OUTPUT / "Preview.qml"))
    component.setData(HARNESS, harness_url)
    assert not component.isError(), [error.toString() for error in component.errors()]
    root = component.create()
    assert root, [error.toString() for error in component.errors()]
    view.setContent(harness_url, component, root)
    view.show()
    settle()
    toolbar = root.findChild(QQuickItem, "toolbarLoader").property("item")
    indicator = find_item(root, "uniRcLinkIndicator")
    assert indicator, ("Production toolbar must include the UniRC indicator", WARNINGS)
    strength = find_item(root, "uniRcToolbarStrength")
    icon = find_item(root, "uniRcToolbarSignalIcon")
    touch_area = find_item(root, "uniRcLinkTouchArea")
    indicator_width = indicator.width()

    def click():
        point = touch_area.mapToScene(QPointF(touch_area.width() / 2, touch_area.height() / 2))
        count = window.opened_count
        QTest.mouseClick(view, Qt.LeftButton, Qt.NoModifier, point.toPoint())
        settle()
        assert window.opened_count == count + 1 and window.opened_indicator == indicator
        assert window.page and not window.page.property("waitForParameters")

    def check_bounds():
        for item in descendants(window.page):
            if not item.isVisible() or item.width() <= 0:
                continue
            if item.metaObject().indexOfProperty("text") >= 0:
                point = item.mapToItem(window.page, QPointF(0, 0))
                assert point.x() >= -1 and point.x() + item.width() <= window.page.width() + 1
                assert item.property("contentWidth") <= item.width() + 1, item.property("text")
        detail_strength = window.page.findChild(QQuickItem, "uniRcDetailStrength")
        detail_quality = window.page.findChild(QQuickItem, "uniRcDetailQuality")
        strength_size = detail_strength.property("fontInfo").property("pointSize").toNumber()
        quality_size = detail_quality.property("fontInfo").property("pointSize").toNumber()
        assert strength_size > 0 and abs(strength_size - quality_size) < 0.01, (strength_size, quality_size)
        for name in ("uniRcDetailStrengthCaption", "uniRcDetailQualityCaption"):
            assert window.page.findChild(QQuickItem, name).property("lineCount") == 1
        strength_pos = detail_strength.mapToItem(window.page, QPointF(0, 0))
        quality_pos = detail_quality.mapToItem(window.page, QPointF(0, 0))
        assert quality_pos.x() > strength_pos.x() and abs(quality_pos.y() - strength_pos.y()) < 1
        # Equal columns can differ by one pixel when the available width is odd.
        assert abs(detail_strength.width() - detail_quality.width()) <= 1, (detail_strength.width(), detail_quality.width())
        surface = root.findChild(QQuickItem, "drawerSurface")
        assert surface.x() >= 0 and surface.x() + surface.width() <= root.width()
        assert surface.y() + surface.height() <= root.height()

    for light in (False, True):
        style.light_value = light
        style.changed.emit()
        for percent in (0, 20, 40, 60, 80, 100):
            level = percent // 20
            # Deliberately vary quality independently of strength.
            quality = (level + 2) % 3
            controller.status.update(percent, quality)
            settle()
            assert strength.property("text") == f"{percent}%"
            assert abs(indicator.width() - indicator_width) < 1, "Signal changes must not move neighboring indicators"
            assert icon.property("level") == level and icon.property("available")
            expected_colors = (("#bd3e43", "#f27e82"), ("#a66309", "#efb957"), ("#087f73", "#49d7bd"))
            band = 0 if level <= 1 else 1 if level <= 3 else 2
            assert icon.property("signalColor").name() == expected_colors[band][0 if light else 1]
            click()
            detail_strength = window.page.findChild(QQuickItem, "uniRcDetailStrength").property("text")
            assert detail_strength == f"{percent}%", (percent, detail_strength, window.page.property("available"), WARNINGS)
            assert window.page.findChild(QQuickItem, "uniRcDetailQuality").property("text") == ("弱", "中", "强")[quality]
            check_bounds()
            assert view.grabWindow().save(str(OUTPUT / f"{'light' if light else 'dark'}-{percent:03}.png"))
        controller.status.update(100, 2, False)  # stale values must not leak to the UI
        settle()
        assert strength.property("text") == "--" and not icon.property("available")
        assert window.page.findChild(QQuickItem, "uniRcDetailStrength").property("text") == "未获取"
        assert window.page.findChild(QQuickItem, "uniRcDetailQuality").property("text") == "未获取"
        assert view.grabWindow().save(str(OUTPUT / f"{'light' if light else 'dark'}-unknown.png"))
    print("PASS: six strength levels, independent quality/color, stale state, real drawer click and translations")

    def check_gimbal_position():
        gimbal_item = find_item(root, "gimbalIndicator")
        indicator_right = indicator.mapToItem(toolbar, QPointF(indicator.width(), 0)).x()
        gimbal_left = gimbal_item.mapToItem(toolbar, QPointF(0, 0)).x()
        assert abs(gimbal_left - indicator_right - toolbar.property("spacing")) < 1

    check_gimbal_position()

    # SDK transport remains connected throughout this flight-link loss.
    window.close_drawer()
    connected_width = toolbar.width()
    vehicle.link_manager.lost = True
    vehicle.link_manager.changed.emit()
    settle()
    assert not indicator.isVisible() and not indicator.property("showIndicator")
    assert not touch_area.property("enabled") and toolbar.width() < connected_width
    vehicle.link_manager.lost = False
    vehicle.link_manager.changed.emit()
    settle()
    assert indicator.isVisible()
    check_gimbal_position()
    print("PASS: flight-link loss hides the icon and releases spacing; reconnect restores it immediately before the gimbal")

    # A contact sheet of unaltered production-QML grabs for review. Color,
    # text and icon rendering all come from the actual toolbar/drawer above.
    style.light_value = False
    style.changed.emit()
    captures = []
    for name, percent, quality, available in (("strong", 100, 2, True),
                                              ("medium", 60, 1, True),
                                              ("weak", 20, 0, True),
                                              ("unknown", 0, 0, False)):
        controller.status.update(percent, quality, available)
        settle()
        click()
        captured = view.grabWindow()
        assert captured.save(str(OUTPUT / f"preview-{name}.png"))
        captures.append(captured.copy(360, 0, 440, 280))
    contact_sheet = QImage(896, 576, QImage.Format_ARGB32)
    contact_sheet.fill(QColor("#333333"))
    painter = QPainter(contact_sheet)
    for index, captured in enumerate(captures):
        painter.drawImage((index % 2) * 456, (index // 2) * 296, captured)
    painter.end()
    assert contact_sheet.save(str(OUTPUT / "overview-dark.png"))

    manager.current = None
    manager.changed.emit()
    settle()
    assert find_item(root, "uniRcLinkIndicator") is None
    manager.current = vehicle
    manager.changed.emit()
    settle()
    indicator = find_item(root, "uniRcLinkIndicator")
    strength = find_item(root, "uniRcToolbarStrength")
    icon = find_item(root, "uniRcToolbarSignalIcon")
    touch_area = find_item(root, "uniRcLinkTouchArea")
    assert indicator and indicator.isVisible()
    check_gimbal_position()
    click()
    assert window.page
    print("PASS: no active vehicle removes the icon; reconnect restores a single usable entry")

    for light in (False, True):
        window.close_drawer()
        style.scale_value, style.light_value = 1.5, light
        style.changed.emit()
        window.current_width = 320
        window.widthChanged.emit()
        view.resize(320, 640)
        controller.status.update(100, 2)
        settle()
        click()
        check_bounds()
        assert touch_area.width() >= 44 and touch_area.height() >= 44
        assert view.grabWindow().save(str(OUTPUT / f"narrow-large-font-{'light' if light else 'dark'}.png"))
        controller.status.update(100, 2, False)
        settle()
        check_bounds()
        assert window.page.findChild(QQuickItem, "uniRcDetailStrength").property("text") == "未获取"
        assert view.grabWindow().save(str(OUTPUT / f"narrow-unknown-{'light' if light else 'dark'}.png"))
    print("PASS: 320px layout, 150% fonts, touch bounds and both palettes")

    plugin.controller = None
    plugin.changed.emit()
    settle()
    assert find_item(root, "uniRcLinkIndicatorLoader") is None
    errors = [message for message in WARNINGS if any(token in message for token in (
        "TypeError", "ReferenceError", "Binding loop", "Unable to assign", "Cannot assign",
        "is not a type", "Error:", "Cannot open"))]
    assert not errors, "\n".join(errors)
    print("PASS: desktop controller absence and no QML errors; screenshots:", OUTPUT)
    window.close_drawer()
    view.close()


if __name__ == "__main__":
    main()
