"""Exercise the production DualPipView/PipState with Qt Quick mouse events.

Run with PySide6: python custom/test/FlightDisplay/DualPipResizeTest.py
Offscreen/software rendering; settings, font metrics and video/map contents
are test doubles. No camera, persistent settings or full QGC build is needed.
"""

import os
from pathlib import Path
import unittest

os.environ["QT_QPA_PLATFORM"] = "offscreen"
os.environ["QT_QUICK_BACKEND"] = "software"

import PySide6

QT_ROOT = Path(PySide6.__file__).parent
os.environ["QT_PLUGIN_PATH"] = str(QT_ROOT / "plugins")
os.environ["QT_QPA_PLATFORM_PLUGIN_PATH"] = str(QT_ROOT / "plugins/platforms")

from PySide6.QtCore import QObject, Property, QPoint, QPointF, Qt, QUrl, Slot
from PySide6.QtGui import QGuiApplication
from PySide6.QtQml import (QQmlComponent, qmlRegisterSingletonType,
                          qmlRegisterType)
from PySide6.QtQuick import QQuickView
from PySide6.QtTest import QTest

ROOT = Path(__file__).resolve().parents[3]


class Settings(QObject):
    @Slot(str, str, result=str)
    def loadGlobalSetting(self, key, default):
        return default

    @Slot(str, bool, result=bool)
    def loadBoolGlobalSetting(self, key, default):
        return default

    @Slot(str, str)
    def saveGlobalSetting(self, key, value):
        pass

    @Slot(str, bool)
    def saveBoolGlobalSetting(self, key, value):
        pass


class ScreenTools(QObject):
    isMobile = Property(bool, lambda self: False, constant=True)
    defaultFontPixelHeight = Property(float, lambda self: 16, constant=True)


HARNESS = b"""
import QtQuick
import QGroundControl.Controls
import PipResizeTest

Item {
    width: 1200
    height: 1000
    property Item pipItem: pip
    property Item holderItem: holder

    Item {
        id: holder
        x: 37
        y: 53
        width: 1100
        height: 880

        component View: Rectangle {
            property alias pipState: state
            property alias pipView: state.pipView
            PipState { id: state }
        }

        View { id: map; pipView: pip.item1PipView; color: "gray" }
        View { id: video1; pipView: pip.item2PipView; color: "blue" }
        View { id: video2; pipView: pip.item3PipView; color: "green" }

        DualPipView {
            id: pip
            z: 1
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            anchors.margins: 12
            item1: map
            item2: video1
            item3: video2
            currentItemSettingsKey: "ResizeTest"
        }
    }
}
"""


class DualPipResizeTest(unittest.TestCase):
    def setUp(self):
        self.view = QQuickView()
        self.view.engine().setImportPathList([str(QT_ROOT / "qml"), "qrc:/qt/qml"])
        self.warnings = []
        self.view.engine().warnings.connect(
            lambda errors: self.warnings.extend(error.toString() for error in errors))
        component = QQmlComponent(self.view.engine())
        url = QUrl.fromLocalFile(str(Path(__file__).with_name("ResizeHarness.qml")))
        component.setData(HARNESS, url)
        self.assertFalse(component.isError(), [e.toString() for e in component.errors()])
        self.root = component.create()
        self.assertIsNotNone(self.root, [e.toString() for e in component.errors()])
        self.view.setContent(url, component, self.root)
        self.view.show()
        QTest.qWait(20)
        self.pip = self.root.property("pipItem")
        self.holder = self.root.property("holderItem")

    def tearDown(self):
        self.view.close()
        # The standalone harness loads the actual QML from disk, without QGC's
        # image resources. Their explicit icon dimensions still apply.
        errors = [warning for warning in self.warnings
                  if "QML Image: Cannot open:" not in warning]
        self.assertEqual(errors, [])

    def handle(self, slot):
        pane = next(child for child in self.pip.childItems()
                    if child.property("slotIndex") == slot)
        return next(area for icon in pane.childItems() for area in icon.childItems()
                    if area.property("initialWidth") is not None)

    def press(self, slot):
        handle = self.handle(slot)
        point = handle.mapToScene(QPointF(handle.width() / 2, handle.height() / 2)).toPoint()
        QTest.mouseMove(self.view, point)
        QTest.qWait(20)
        QTest.mousePress(self.view, Qt.LeftButton, Qt.NoModifier, point)
        self.assertTrue(handle.property("pressed"))
        return handle, point

    def check_width(self, expected):
        self.assertAlmostEqual(self.pip.width(), expected, delta=0.01)
        for name in ("_lowerItem", "_upperItem"):
            item = self.pip.property(name)
            if item:
                self.assertAlmostEqual(item.width(), expected, delta=0.01)
                self.assertAlmostEqual(item.height(), expected * 9 / 16, delta=0.01)

    def drag(self, slot, offsets):
        main = self.pip.property("_fullItem")
        initial_width = self.pip.width()
        handle, start = self.press(slot)
        end = start
        try:
            for dx, dy in offsets:
                end = start + QPoint(dx, dy)
                QTest.mouseMove(self.view, end)
                self.check_width(max(self.holder.width() * 0.10,
                                     min(self.holder.width() * 0.75, initial_width + dx)))
                self.assertTrue(handle.property("pressed"))
                self.assertTrue(handle.isVisible())
        finally:
            QTest.mouseRelease(self.view, Qt.LeftButton, Qt.NoModifier, end)
        self.assertFalse(handle.property("pressed"))
        self.assertEqual(self.pip.property("_fullItem"), main)

    def test_both_handles_follow_pointer_and_reverse(self):
        for slot in (1, 2):
            with self.subTest(slot=slot):
                self.pip.setProperty("_pipSize", 220)
                self.drag(slot, [(dx, 0) for dx in (10, 20, 30, 60, 120, 80, 20, -30, 0)])

    def test_drag_outside_pane_and_clamp_at_limits(self):
        for slot in (1, 2):
            with self.subTest(slot=slot):
                self.pip.setProperty("_pipSize", 220)
                self.drag(slot, [(0, -160), (40, -160), (800, -160), (700, -160),
                                 (600, -160), (-180, -160), (-120, -160), (-100, -160)])

    def test_release_then_grab_again_and_single_pane(self):
        self.pip.setProperty("item3", None)
        QTest.qWait(1)
        self.drag(1, [(40, -20), (80, -40)])
        self.drag(1, [(-20, 0), (-40, 0)])

    def test_cancel_does_not_detach_handle(self):
        handle, start = self.press(1)
        QTest.mouseMove(self.view, start + QPoint(40, 0))
        handle.ungrabMouse()
        self.assertFalse(handle.property("pressed"))
        QTest.mouseRelease(self.view, Qt.LeftButton, Qt.NoModifier, start + QPoint(40, 0))
        self.drag(2, [(20, -20), (40, -40)])
        self.drag(1, [(-20, 0), (-40, 0)])

    def test_resize_after_swap_and_parent_resize(self):
        old_main = self.pip.property("_fullItem")
        lower = self.pip.property("_lowerItem")
        point = lower.mapToScene(QPointF(lower.width() / 2, lower.height() / 2)).toPoint()
        QTest.mouseMove(self.view, point)
        QTest.mouseClick(self.view, Qt.LeftButton, Qt.NoModifier, point)
        QTest.qWait(20)
        # Re-enter the pane after the clicked view has been reparented.
        QTest.mouseMove(self.view, QPoint(10, 10))
        self.assertEqual(self.pip.property("_lowerItem"), old_main)
        self.assertEqual(self.pip.property("_fullItem"), lower)
        self.drag(1, [(20, 0), (40, 0)])
        self.pip.setProperty("_pipSize", 500)
        self.holder.setWidth(400)
        self.check_width(300)
        self.drag(2, [(-20, 0), (-40, 0)])


if __name__ == "__main__":
    app = QGuiApplication([])
    qmlRegisterSingletonType(Settings, "QGroundControl", 1, 0,
                            "QGroundControl", lambda engine: Settings(engine))
    qmlRegisterSingletonType(ScreenTools, "QGroundControl.ScreenTools", 1, 0,
                            "ScreenTools", lambda engine: ScreenTools(engine))
    qmlRegisterType(QUrl.fromLocalFile(str(ROOT / "src/QmlControls/PipState.qml")),
                    "QGroundControl.Controls", 1, 0, "PipState")
    qmlRegisterType(QUrl.fromLocalFile(str(ROOT / "custom/src/FlightDisplay/DualPipView.qml")),
                    "PipResizeTest", 1, 0, "DualPipView")
    unittest.main(verbosity=2)
