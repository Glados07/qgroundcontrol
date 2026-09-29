"""Run production compass/overlay and DualPipView/PipState in Qt Quick.

Run: python custom/test/FlightDisplay/GimbalCompassTest.py (requires PySide6).
Only telemetry, fonts, palette, unrelated alert and video/map content are
test doubles. No network, camera, settings writes or QGC build is required.
"""

import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest
from xml.etree import ElementTree

os.environ["QT_QPA_PLATFORM"] = "offscreen"
os.environ["QT_QUICK_BACKEND"] = "software"

import PySide6

QT_ROOT = Path(PySide6.__file__).parent
os.environ["QT_PLUGIN_PATH"] = str(QT_ROOT / "plugins")
os.environ["QT_QPA_PLATFORM_PLUGIN_PATH"] = str(QT_ROOT / "plugins/platforms")

from PySide6.QtCore import QObject, Property, Q_ARG, QMetaObject, QResource, Qt, QUrl
from PySide6.QtGui import QColor, QGuiApplication
from PySide6.QtQml import QQmlComponent, qmlRegisterSingletonType, qmlRegisterType
from PySide6.QtQuick import QQuickView
from PySide6.QtTest import QTest

ROOT = Path(__file__).resolve().parents[3]
FLIGHT = ROOT / "custom/src/FlightDisplay"
STUBS = Path(__file__).with_name("CompassStubs")


class ScreenTools(QObject):
    isMobile = Property(bool, lambda self: False, constant=True)
    defaultFontPixelHeight = Property(float, lambda self: 16, constant=True)
    defaultFontPixelWidth = Property(float, lambda self: 8, constant=True)
    smallFontPointSize = Property(float, lambda self: 10, constant=True)


class Palette(QObject):
    text = Property(QColor, lambda self: QColor("white"), constant=True)
    windowShadeDark = Property(QColor, lambda self: QColor("#333333"), constant=True)


def build_resources(directory):
    """Keep Loader asynchronous URL resolution native, as in the product."""
    rcc = next(path for path in (QT_ROOT / "rcc.exe", QT_ROOT / "Qt/libexec/rcc")
               if path.is_file())
    tree = ElementTree.Element("RCC")
    resource = ElementTree.SubElement(tree, "qresource", prefix="/")
    for alias, source in (
            ("Custom/qml/QGroundControl/FlightDisplay/FlyViewCompassBar.qml",
             FLIGHT / "FlyViewCompassBar.qml"),
            ("custom/img/compassPointer.svg",
             ROOT / "custom/src/FlightMap/Images/compassPointer.svg")):
        ElementTree.SubElement(resource, "file", alias=alias).text = str(source)
    qrc = Path(directory) / "compass.qrc"
    binary = Path(directory) / "compass.rcc"
    ElementTree.ElementTree(tree).write(qrc, encoding="utf-8", xml_declaration=True)
    subprocess.run([str(rcc), "--binary", str(qrc), "-o", str(binary)], check=True)
    return str(binary)


HARNESS = """
import QtQuick
import QGroundControl
import QGroundControl.Controls
import CompassTest

Item {
    width: 1200
    height: 900
    property var dataSource: QGroundControl
    property Item overlayItem: overlay
    property alias threeD: viewer3DWindow.isOpen

    // Evaluate FlyView's actual binding against real PipState transitions,
    // not a separately maintained copy of the main-view visibility rule.
    QtObject { id: viewer3DWindow; property bool isOpen: false }
    component View: Rectangle {
        property alias pipState: state
        property alias pipView: state.pipView
        PipState { id: state }
    }
    View { id: mapControl; pipView: pip.item1PipView; color: "gray" }
    View { id: videoControl; pipView: pip.item2PipView; color: "blue" }
    View { id: secondaryVideoControl; pipView: pip.item3PipView; color: "green" }
    DualPipView {
        id: pip
        item1: mapControl
        item2: QGroundControl.videoManager.hasVideo ? videoControl : null
        item3: secondaryVideoControl
        currentItemSettingsKey: "CompassTest"
    }
    QGCToolInsets { id: insets; topEdgeCenterInset: 17 }
    FlyViewCustomLayer {
        id: overlay
        anchors.fill: parent
        parentToolInsets: insets
        a8VideoIsMain: __MAIN_VIEW_BINDING__
    }
    function selectView(index) {
        var view = [mapControl, videoControl, secondaryVideoControl][index]
        if (pip._lowerItem === view) pip._activateSlot(1)
        else if (pip._upperItem === view) pip._activateSlot(2)
    }
    function detachA8() {
        videoControl.pipState.state = videoControl.pipState.windowState
    }
}
"""


class GimbalCompassTest(unittest.TestCase):
    def setUp(self):
        self.view = QQuickView()
        self.view.engine().setImportPathList([str(QT_ROOT / "qml"), "qrc:/qt/qml"])
        self.warnings = []
        self.removed_view = False
        self.view.engine().warnings.connect(
            lambda errors: self.warnings.extend(error.toString() for error in errors))
        source = (FLIGHT / "FlyView.qml").read_text(encoding="utf-8")
        binding = re.search(r"a8VideoIsMain:\s*(.*?)\n\s*visible:", source, re.S)
        self.assertIsNotNone(binding, "FlyView must wire the A8 main-view condition")
        harness = HARNESS.replace("__MAIN_VIEW_BINDING__", binding.group(1))
        component = QQmlComponent(self.view.engine())
        self.component = component  # Keep the creation context alive for the test.
        url = QUrl.fromLocalFile(str(Path(__file__).with_name("CompassHarness.qml")))
        component.setData(harness.encode("utf-8"), url)
        self.assertFalse(component.isError(), [e.toString() for e in component.errors()])
        self.root = component.create()
        self.assertIsNotNone(self.root, [e.toString() for e in component.errors()])
        self.view.setContent(url, component, self.root)
        self.view.show()
        QTest.qWait(20)
        self.overlay = self.root.property("overlayItem")
        self.qgc = self.root.property("dataSource")
        self.vehicle = self.qgc.property("multiVehicleManager").property("activeVehicle")
        self.provider = self.qgc.property("corePlugin").property("gimbalAzimuthProvider")
        loaders = [item for item in self.overlay.childItems()
                   if item.property("source") == QUrl(
                       "qrc:/Custom/qml/QGroundControl/FlightDisplay/FlyViewCompassBar.qml")]
        self.assertEqual(len(loaders), 2)
        self.gimbal, self.aircraft = loaders

    def tearDown(self):
        self.view.close()
        # Native PIP icons are not packaged in this standalone harness.
        errors = [warning for warning in self.warnings
                  if not ("DualPipView.qml:" in warning and "QML Image: Cannot open:" in warning)
                  # Native PipState can restore anchors before reparenting a
                  # removed view. This existing transient warning is unrelated
                  # to compass bindings; allow it only in the removal test.
                  and not (self.removed_view and "CompassHarness.qml:" in warning
                           and "QML View: Cannot anchor to an item that isn't a parent or sibling."
                           in warning)]
        self.assertEqual(errors, [])

    def select(self, index):
        self.assertTrue(QMetaObject.invokeMethod(self.root, "selectView", Qt.DirectConnection,
                                                Q_ARG("QVariant", index)))
        QTest.qWait(5)

    def assert_gimbal_visible(self, expected):
        self.assertEqual(self.gimbal.property("active"), expected)
        self.assertEqual(self.gimbal.isVisible(), expected)
        inset = self.overlay.property("totalToolInsets").property("topEdgeCenterInset")
        self.assertGreater(inset, 17) if expected else self.assertEqual(inset, 17)

    @staticmethod
    def labels(compass):
        return [label for child in compass.childItems() for label in child.childItems()
                if label.property("_unwrappedAngle") is not None]

    def test_main_view_switches_and_insets(self):
        for index in (0, 1, 2, 1, 0, 2, 0, 1):
            with self.subTest(view=index):
                self.select(index)
                self.assert_gimbal_visible(index == 1)
                self.assertTrue(self.aircraft.isVisible())

    def test_standard_scale_uses_resolved_azimuth_without_reversing_it(self):
        self.select(1)
        compass = self.gimbal.property("item")
        for angle in (0, 44.9, 45, 90, 135, 180, 270, 315, 359, 359.9, 360, -90):
            with self.subTest(angle=angle):
                self.provider.setProperty("absoluteYaw", angle)
                QTest.qWait(1)
                heading = angle % 360
                self.assertEqual(compass.property("directionDegrees"), angle)
                self.assertEqual(compass.property("heading"), heading)
                expected_text = "Gimbal " + str(int(heading + 0.5) % 360) + "°"
                texts = [label.property("text") for child in compass.childItems()
                         for label in child.childItems()]
                self.assertIn(expected_text, texts)
                labels = self.labels(compass)
                self.assertEqual(len(labels), 11)
                for label in labels:
                    unwrapped = label.property("_unwrappedAngle")
                    expected_x = compass.width() / 2 + (unwrapped - heading) * compass.width() / 360
                    self.assertAlmostEqual(label.x() + label.width() / 2, expected_x)
                    self.assertEqual(label.property("text"),
                                     ("N", "NE", "E", "SE", "S", "SW", "W", "NW")
                                     [int(unwrapped % 360 / 45)])

    def test_east_west_positions_and_aircraft_scale_unchanged(self):
        self.select(1)
        self.vehicle.property("heading").setProperty("rawValue", 0)
        QTest.qWait(1)
        for loader in (self.gimbal, self.aircraft):
            compass = loader.property("item")
            labels = self.labels(compass)
            east = next(label for label in labels if label.property("text") == "E")
            west = next(label for label in labels if label.property("text") == "W")
            north = next(label for label in labels if label.property("text") == "N")
            center = compass.width() / 2
            self.assertGreater(east.x() + east.width() / 2, center)
            self.assertLess(west.x() + west.width() / 2, center)
            self.assertAlmostEqual(north.x() + north.width() / 2, center)

    def test_gimbal_and_aircraft_use_independent_world_headings(self):
        self.select(1)
        # A locked gimbal can keep its earth heading while the aircraft turns.
        # This checks display bindings only; it does not simulate the provider.
        for gimbal_angle, aircraft_angle in ((90, 0), (90, 180), (270, 180), (0, 270)):
            with self.subTest(gimbal=gimbal_angle, aircraft=aircraft_angle):
                self.provider.setProperty("absoluteYaw", gimbal_angle)
                self.vehicle.property("heading").setProperty("rawValue", aircraft_angle)
                QTest.qWait(1)
                for loader, angle in ((self.gimbal, gimbal_angle), (self.aircraft, aircraft_angle)):
                    compass = loader.property("item")
                    self.assertEqual(compass.property("heading"), angle)
                    center_label = next(label for label in self.labels(compass)
                                        if abs(label.x() + label.width() / 2 - compass.width() / 2) < 0.01)
                    self.assertEqual(center_label.property("text"), ("N", "E", "S", "W")[angle // 90])

    def test_right_and_left_bearings_bring_east_and_west_to_pointer(self):
        self.select(1)
        compass = self.gimbal.property("item")
        # The provider supplies resolved world yaw. Positive/rightward yaw
        # must bring E to the pointer, not W; do not mirror the scale again.
        for direction, cardinal in ((1, "E"), (-1, "W")):
            previous_distance = float("inf")
            for turn in (0, 30, 60, 90):
                self.provider.setProperty("absoluteYaw", direction * turn)
                QTest.qWait(1)
                target = min((label for label in self.labels(compass)
                              if label.property("text") == cardinal),
                             key=lambda label: abs(label.x() + label.width() / 2 - compass.width() / 2))
                distance = abs(target.x() + target.width() / 2 - compass.width() / 2)
                self.assertLess(distance, previous_distance)
                self.assertAlmostEqual(compass.property("heading"), (direction * turn) % 360)
                previous_distance = distance
            self.assertAlmostEqual(previous_distance, 0)

    def test_north_crossing_is_continuous(self):
        self.select(1)
        compass = self.gimbal.property("item")
        positions = []
        for angle in (359, 0, 1):
            self.provider.setProperty("absoluteYaw", angle)
            QTest.qWait(1)
            north = next(label for label in self.labels(compass)
                         if label.property("text") == "N")
            positions.append(north.x() + north.width() / 2)
        for before, after in zip(positions, positions[1:]):
            self.assertAlmostEqual(after - before, -compass.width() / 360)

    def test_invalid_telemetry_link_and_setting_hide_only_gimbal(self):
        self.select(1)
        setting = self.qgc.property("corePlugin").property("flyViewCustomSettings").property(
            "showGimbalHeadingCompassBar")
        for obj, prop, bad, good in (
                (self.provider, "valid", False, True),
                (self.provider, "absoluteYaw", float("nan"), 0),
                (self.vehicle.property("vehicleLinkManager"), "communicationLost", True, False),
                (setting, "rawValue", False, True)):
            with self.subTest(property=prop):
                obj.setProperty(prop, bad)
                QTest.qWait(1)
                self.assert_gimbal_visible(False)
                self.assertTrue(self.aircraft.isVisible())
                obj.setProperty(prop, good)
                QTest.qWait(1)
                self.assert_gimbal_visible(True)

    def test_removed_video_and_3d_do_not_show_gimbal(self):
        self.removed_view = True
        self.select(1)
        self.qgc.property("videoManager").setProperty("hasVideo", False)
        QTest.qWait(5)
        self.assert_gimbal_visible(False)
        self.qgc.property("videoManager").setProperty("hasVideo", True)
        QTest.qWait(5)
        self.select(1)
        self.assert_gimbal_visible(True)
        self.root.setProperty("threeD", True)
        QTest.qWait(1)
        self.assert_gimbal_visible(False)
        self.root.setProperty("threeD", False)
        QTest.qWait(1)
        self.assert_gimbal_visible(True)

    def test_detached_a8_and_hidden_overlay(self):
        self.select(0)
        self.assertTrue(QMetaObject.invokeMethod(self.root, "detachA8", Qt.DirectConnection))
        QTest.qWait(5)
        self.assert_gimbal_visible(False)
        self.select(1)
        self.assert_gimbal_visible(True)
        self.overlay.setVisible(False)
        QTest.qWait(1)
        self.assert_gimbal_visible(False)


if __name__ == "__main__":
    app = QGuiApplication([])
    qmlRegisterSingletonType(QUrl.fromLocalFile(str(STUBS / "QGroundControl.qml")),
                             "QGroundControl", 1, 0, "QGroundControl")
    qmlRegisterSingletonType(ScreenTools, "QGroundControl.ScreenTools", 1, 0,
                             "ScreenTools", lambda engine: ScreenTools(engine))
    qmlRegisterType(Palette, "QGroundControl.Palette", 1, 0, "QGCPalette")
    for name in ("QGCLabel", "QGCColoredImage"):
        qmlRegisterType(QUrl.fromLocalFile(str(STUBS / (name + ".qml"))),
                        "QGroundControl.Controls", 1, 0, name)
    for name in ("PipState", "QGCToolInsets"):
        qmlRegisterType(QUrl.fromLocalFile(str(ROOT / "src/QmlControls" / (name + ".qml"))),
                        "QGroundControl.Controls", 1, 0, name)
    qmlRegisterType(QUrl.fromLocalFile(str(STUBS / "GeneratorBusVoltageAlert.qml")),
                    "Custom.Widgets", 1, 0, "GeneratorBusVoltageAlert")
    for name in ("DualPipView", "FlyViewCustomLayer"):
        qmlRegisterType(QUrl.fromLocalFile(str(FLIGHT / (name + ".qml"))),
                        "CompassTest", 1, 0, name)
    with tempfile.TemporaryDirectory(prefix="qgc-compass-test-") as directory:
        resource = build_resources(directory)
        if not QResource.registerResource(resource):
            raise RuntimeError("Cannot register compass test resources")
        try:
            result = unittest.main(verbosity=2, exit=False).result
        finally:
            QResource.unregisterResource(resource)
        raise SystemExit(0 if result.wasSuccessful() else 1)
