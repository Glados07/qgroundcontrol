"""Qt 6 layout smoke test of the actual resource-backed Fly View settings page.

Requires PySide6 and a binary resource built from custom/custom.qrc. Native
Fact controls are loaded; the app singleton, Fact metadata and platform/file
services are test doubles. This is not an Android/full-application test.
"""
import argparse
import json
import os
from pathlib import Path
import sys

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
os.environ.setdefault("QT_QUICK_BACKEND", "software")

import PySide6

# This workstation also has Qt 5 on PATH. Pin Qt 6 plugins for this test
# process so QGuiApplication does not discover incompatible installations.
os.environ["QT_PLUGIN_PATH"] = str(Path(PySide6.__file__).parent / "plugins")
os.environ["QT_QPA_PLATFORM_PLUGIN_PATH"] = str(Path(os.environ["QT_PLUGIN_PATH"]) / "platforms")

from PySide6.QtCore import (QObject, Property, QResource, Signal, Slot, QUrl,
                            QPoint, QPointF, QTranslator, QMetaObject, Q_ARG,
                            Qt, qInstallMessageHandler)
from PySide6.QtGui import QGuiApplication, QColor, QFontDatabase
from PySide6.QtQml import (QQmlProperty, qmlRegisterType, qmlRegisterSingletonType,
                           qmlRegisterSingletonInstance)
from PySide6.QtQuick import QQuickItem, QQuickView
from PySide6.QtTest import QTest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
KEEP = []
WARNINGS = []


class Fact(QObject):
    changed = Signal()
    rawValueChanged = Signal()

    def __init__(self, value=False, metadata=None):
        super().__init__()
        # A QML-created default Fact may receive a QObject parent argument.
        self.data = metadata or {}
        self._value = False if isinstance(value, QObject) else value
        KEEP.append(self)

    def get_value(self):
        return self._value

    def set_value(self, value):
        if self._value != value:
            self._value = value
            self.changed.emit()
            self.rawValueChanged.emit()

    value = Property("QVariant", get_value, set_value, notify=changed)
    rawValue = Property("QVariant", get_value, set_value, notify=rawValueChanged)
    valueString = Property(str, lambda self: str(self._value), notify=changed)
    visible = Property(bool, lambda self: True, constant=True)
    typeIsBool = Property(bool, lambda self: isinstance(self._value, bool), notify=changed)
    typeIsString = Property(bool, lambda self: isinstance(self._value, str), notify=changed)
    shortDescription = Property(str, lambda self: self.data.get("shortDesc", ""), constant=True)
    units = Property(str, lambda self: self.data.get("units", ""), constant=True)
    enumValues = Property("QVariantList", lambda self: self.data.get("enumValues", []), constant=True)
    enumStrings = Property("QVariantList", lambda self: self.data.get("enumStrings", []), constant=True)
    enumIndex = Property(int, lambda self: self.data.get("enumValues", [self._value]).index(self._value)
                         if self._value in self.data.get("enumValues", [self._value]) else 0, notify=changed)

    @Slot(str, bool, result=str)
    def validate(self, text, convert_only):
        return ""


def facts(relative):
    # Parse production metadata, not a separately maintained list of fields.
    data = json.loads((ROOT / relative).read_text(encoding="utf-8"))
    result = {}
    for entry in data["QGC.MetaData.Facts"]:
        if isinstance(entry.get("enumStrings"), str):
            entry["enumStrings"] = entry["enumStrings"].split(",")
        if isinstance(entry.get("enumValues"), str):
            values = []
            for value in entry["enumValues"].split(","):
                try:
                    values.append(float(value))
                except ValueError:
                    values.append(value)
            entry["enumValues"] = values
        result[entry["name"]] = Fact(entry.get("default", False), entry)
    return result


class Style(QObject):
    changed = Signal()
    scale_value = 1.0
    light_value = False
    scale = Property(float, lambda self: self.scale_value, notify=changed)
    light = Property(bool, lambda self: self.light_value, notify=changed)


class Core(QObject):
    def __init__(self):
        super().__init__()
        self.app = facts("src/Settings/App.SettingsGroup.json")
        self.app["mavlinkActionsSavePath"] = "/sdcard/very-long-directory-without-spaces/Custom-QGroundControl/MAVLinkActions"
        self.fly = facts("src/Settings/FlyView.SettingsGroup.json")
        self.gimbal = facts("custom/src/Gimbal/GimbalControl.SettingsGroup.json")
        self.fly_custom = facts("custom/src/Settings/FlyViewCustom.SettingsGroup.json")
        self.viewer = facts("custom/src/Viewer3D/Viewer3D.SettingsGroup.json")
        self.viewer["enabled"].set_value(True)
        self.plugin = {
            "options": {"preFlightChecklistUrl": QUrl("qrc:/checklist.qml")},
            "flyViewCustomSettings": self.fly_custom,
            "gimbalControlSettings": self.gimbal,
            "viewer3DSettings": self.viewer,
            "uniRcChannelController": {"sdkRouteActive": True, "channelValues": [1500] * 16},
            "external3DMapManager": None,
        }
        self.settings = {"appSettings": self.app, "flyViewSettings": self.fly,
                         "mavlinkActionsSettings": {"flyViewActionsFile": Fact(""),
                                                     "joystickActionsFile": Fact("")}}

    settingsManager = Property("QVariantMap", lambda self: self.settings, constant=True)
    corePlugin = Property("QVariantMap", lambda self: self.plugin, constant=True)


class Chinese(QTranslator):
    labels = {
        "General": "常规", "Guided Commands": "引导指令", "MAVLink Actions": "MAVLink 动作",
        "Virtual Joystick": "虚拟摇杆", "Instrument Panel": "仪表面板", "Gimbal Camera": "云台相机",
        "Zoom Step": "变焦步长", "Enabled": "启用", "Channel Values": "通道实时数值",
        "SDK Interface": "SDK 接口", "SDK Bluetooth Address": "SDK 蓝牙地址",
        "SIYI A8 Mini Gimbal Camera": "思翼 A8 Mini 云台相机", "UniPod MT11 Gimbal Camera": "UniPod MT11 云台相机",
        "SDK Host": "SDK 主机地址", "SDK Port": "SDK 端口", "3D View": "3D 视图",
        "Minimum Altitude": "最低高度", "Maximum Altitude": "最高高度",
        "Go To Location Max Distance": "前往目标位置的最大距离",
        "Use Preflight Checklist": "使用飞行前检查清单", "Enforce Preflight Checklist": "强制执行飞行前检查清单",
        "Enable Multi-Vehicle Panel": "启用多载具面板", "Keep Map Centered On Vehicle": "保持地图以载具为中心",
        "Show Telemetry Log Replay Status Bar": "显示遥测日志回放状态栏",
        "Show simple camera controls (DIGICAM_CONTROL)": "显示简易相机控制（DIGICAM_CONTROL）",
        "Update return to home position based on device location.": "根据设备位置更新返航点",
        "Reverse channel gimbal zoom control": "反转遥控器通道的云台变焦方向",
    }

    def translate(self, context, source, disambiguation=None, n=-1):
        return self.labels.get(source, source)


def register_qml(uri, name, relative, singleton=False):
    url = QUrl.fromLocalFile(str(relative))
    if singleton:
        qmlRegisterSingletonType(url, uri, 1, 0, name)
    else:
        qmlRegisterType(url, uri, 1, 0, name)


def descendants(item):
    for child in item.childItems():
        yield child
        yield from descendants(child)


def settle():
    QTest.qWait(120)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--resource", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    output = Path(args.output)
    output.mkdir(parents=True, exist_ok=True)
    app = QGuiApplication(sys.argv)
    # The Windows offscreen plugin may not populate the system font database.
    for filename in ("C:/Windows/Fonts/msyh.ttc", "C:/Windows/Fonts/consola.ttf"):
        if Path(filename).exists():
            assert QFontDatabase.addApplicationFont(filename) >= 0, filename
    qInstallMessageHandler(lambda kind, context, message: WARNINGS.append(message))
    assert QResource.registerResource(args.resource), "Could not register custom binary resource"
    style = Style()
    core = Core()
    qmlRegisterType(Fact, "QGroundControl.FactSystem", 1, 0, "Fact")
    qmlRegisterSingletonInstance(Core, "QGroundControl", 1, 0, "QGroundControl", core)
    for uri in ("QGroundControl.AppSettings", "QGroundControl.MultiVehicleManager"):
        qmlRegisterType(QObject, uri, 1, 0, "LayoutTestPlaceholder")
    register_qml("QGroundControl.ScreenTools", "ScreenTools", HERE / "ScreenTools.qml", True)
    register_qml("QGroundControl.Palette", "QGCPalette", HERE / "QGCPalette.qml")
    for name in ("QGCLabel", "QGCComboBox", "QGCCheckBoxSlider", "QGCFlickable"):
        register_qml("QGroundControl.Controls", name, ROOT / f"src/QmlControls/{name}.qml")
    for name in ("QGCTextField", "QGCButton", "QGCFileDialog", "ParameterEditorDialog"):
        register_qml("QGroundControl.Controls", name, HERE / f"{name}.qml")
    for name in ("FactTextField", "FactComboBox", "FactCheckBoxSlider"):
        register_qml("QGroundControl.FactControls", name, ROOT / f"src/FactSystem/FactControls/{name}.qml")
    register_qml("QGroundControl.Controllers", "QGCFileDialogController", HERE / "QGCFileDialogController.qml")

    view = QQuickView()
    view.engine().setImportPathList([str(Path(PySide6.__file__).parent / "qml"), "qrc:/qt/qml"])
    view.rootContext().setContextProperty("layoutTestStyle", style)
    view.setResizeMode(QQuickView.SizeRootObjectToView)
    view.resize(1200, 850)
    view.setSource(QUrl("qrc:/Custom/qml/QGroundControl/AppSettings/FlyViewSettings.qml"))
    if view.status() == QQuickView.Error:
        raise AssertionError("\n".join(error.toString() for error in view.errors()))
    view.show()
    settle()
    root = view.rootObject()
    flick = root.findChild(QQuickItem, "flyViewSettingsFlickable")
    content = root.findChild(QQuickItem, "flyViewSettingsContent")
    gimbal = next(item for item in descendants(root)
                  if item.metaObject().indexOfProperty("showUniRcSettings") >= 0)
    gimbal.setProperty("showUniRcSettings", True)  # Android-visible layout on desktop.
    translator = Chinese()
    app.installTranslator(translator)
    view.engine().retranslate()

    cases = [(320, 700, 1.0, False), (480, 800, 1.0, False),
             (800, 700, 1.0, False), (1200, 850, 1.0, False),
             (1920, 1080, 1.0, False), (800, 700, 1.5, False),
             (1920, 1080, 1.5, False), (1200, 850, 1.0, True)]
    for width, height, scale, light in cases:
        style.scale_value, style.light_value = scale, light
        style.changed.emit()
        view.setColor(QColor("#ffffff" if light else "#222222"))
        view.resize(width, height)
        settle()
        assert abs(flick.property("contentWidth") - width) < 1
        margin = root.property("pageMargin")
        maximum_width = root.property("contentMaximumWidth")
        assert abs(maximum_width - 8 * scale * 100) < 1, "Content width limit must scale with the font"
        expected_width = min(maximum_width, max(0, width - 2 * margin))
        assert abs(content.width() - expected_width) < 1, (width, content.width(), expected_width)
        left_margin = content.x()
        right_margin = width - content.x() - content.width()
        assert abs(left_margin - right_margin) < 1, (width, left_margin, right_margin)
        assert left_margin >= margin - 1 and right_margin >= margin - 1
        assert content.height() > 0
        backgrounds = root.findChildren(QQuickItem, "flyViewSettingsSectionBackground")
        assert backgrounds, "Missing section backgrounds"
        for background in backgrounds:
            assert background.property("color").alpha() == 0, "Section background must not add a shaded fill"
            assert QQmlProperty.read(background, "border.width") == 1, "Native section outline must remain one pixel wide"
            assert QQmlProperty.read(background, "border.color").alpha() > 0, "Section outline must remain visible"
        channel_grid = root.findChild(QQuickItem, "uniRcChannelGrid")
        # Repeater delegates belong to the visual tree, not necessarily the QObject tree.
        channel_tiles = [item for item in descendants(channel_grid)
                         if item.objectName() == "uniRcChannelTile"]
        assert len(channel_tiles) == 16, ("Every RC channel must have its own tile", len(channel_tiles))
        for tile in channel_tiles:
            assert tile.isVisible() and tile.width() > 0 and tile.height() > 0
            assert tile.property("color") == QColor("#ffffff" if light else "#222222"), "Channel tiles must retain the native page background"
            assert QQmlProperty.read(tile, "border.width") == 1, "Each channel needs a separate outline"
            assert QQmlProperty.read(tile, "border.color") == QColor("#bbbbbb" if light else "#707070")
            point = tile.mapToItem(channel_grid, QPointF(0, 0))
            assert point.x() >= -1 and point.x() + tile.width() <= channel_grid.width() + 1
            labels = sorted((item for item in tile.childItems()
                             if item.metaObject().indexOfProperty("text") >= 0), key=lambda item: item.x())
            assert len(labels) == 2
            assert QQmlProperty.read(labels[0], "font.bold"), "Channel names must remain bold"
            assert labels[0].opacity() == 1, "Channel names must retain full contrast"
            assert labels[0].property("color") == QColor("#000000" if light else "#ffffff")
            assert labels[0].x() + labels[0].width() <= labels[1].x(), "Channel name and value must not overlap"
            assert labels[1].property("text") == "1500", "Channel value binding changed"
        count = 0
        for item in descendants(content):
            if not item.isVisible() or item.width() <= 0:
                continue
            # Check complete row/control bounds, including content far below
            # the scroll viewport. Text input internals/popups may scroll.
            name = item.metaObject().className()
            if any(token in name for token in ("FlyViewSettingsRow", "FlyViewFactTextField",
                                               "FlyViewFactComboBox", "FlyViewComboBox",
                                               "FlyViewFactSwitch", "FlyViewSettingsSection")):
                point = item.mapToItem(content, QPointF(0, 0))
                assert point.x() >= -1 and point.x() + item.width() <= content.width() + 1, (name, width, point.x(), item.width(), content.width())
                assert item.height() > 0, (name, "zero height")
                count += 1
        assert count >= 35, count
        prefix = f"{width}x{height}-scale{scale}-{'light' if light else 'dark'}"
        flick.setProperty("contentY", 0)
        settle()
        assert view.grabWindow().save(str(output / f"{prefix}-top.png"))
        position = gimbal.mapToItem(content, QPointF(0, 0)).y()
        flick.setProperty("contentY", min(position, flick.property("contentHeight") - height))
        settle()
        assert view.grabWindow().save(str(output / f"{prefix}-gimbal.png"))
        position = channel_grid.mapToItem(content, QPointF(0, 0)).y() - 40 * scale
        flick.setProperty("contentY", max(0, min(position, flick.property("contentHeight") - height)))
        settle()
        assert view.grabWindow().save(str(output / f"{prefix}-channels.png"))
        print(f"PASS {prefix}: {count} layout items fit centered {content.width():g}px content; "
              f"{len(backgrounds)} transparent, outlined sections; 16 separately outlined channels")

    # The new wrappers must keep the original Fact write-through semantics.
    flick.setProperty("contentY", 0)
    settle()
    checklist = next(item for item in descendants(root)
                     if "FlyViewFactSwitch" in item.metaObject().className()
                     and item.property("text") == Chinese.labels["Use Preflight Checklist"])
    old = core.app["useChecklist"].get_value()
    point = checklist.mapToScene(QPointF(10, checklist.height() / 2))
    QTest.mouseClick(view, Qt.LeftButton, Qt.NoModifier, QPoint(int(point.x()), int(point.y())))
    settle()
    assert core.app["useChecklist"].get_value() != old, "Full-row switch click lost Fact binding"
    core.app["useChecklist"].set_value(old)

    combo_row = next(item for item in descendants(root)
                     if "FlyViewComboBox" in item.metaObject().className()
                     and item.property("label") == Chinese.labels["SDK Interface"])
    combo = next(item for item in descendants(combo_row)
                 if item.metaObject().className().startswith("QGCComboBox"))
    # The remaining SDK selector has one legal entry. Seed only the test double
    # with an invalid stored value to verify activation writes back that entry.
    core.gimbal["uniRcSdkInterface"].set_value(-1)
    assert QMetaObject.invokeMethod(combo, "activated", Qt.DirectConnection, Q_ARG(int, 0))
    assert core.gimbal["uniRcSdkInterface"].get_value() == 0

    # Legacy frame and direction are fixed product conventions, not settings.
    assert set(core.fly_custom) == {"showHeadingCompassBar", "showGimbalHeadingCompassBar",
                                    "proximityRadarAlertDistance"}
    assert "gimbalLegacyYawReference" not in core.fly_custom
    assert "gimbalLegacyYawReversed" not in core.fly_custom
    assert not any(item.metaObject().indexOfProperty("label") >= 0
                   and item.property("label") == "Legacy gimbal feedback yaw frame"
                   for item in descendants(root)), "Removed legacy feedback frame selector reappeared"
    assert not any("FlyViewFactSwitch" in item.metaObject().className()
                   and item.property("text") == "Reverse legacy vehicle-frame yaw feedback"
                   for item in descendants(root)), "Removed feedback direction switch reappeared"

    host_row = next(item for item in descendants(root)
                    if "FlyViewFactTextField" in item.metaObject().className()
                    and item.property("label") == Chinese.labels["SDK Host"])
    field = next(item for item in descendants(host_row)
                 if item.metaObject().className().startswith("FactTextField"))
    field.setProperty("text", "192.168.144.26")
    assert QMetaObject.invokeMethod(field, "editingFinished", Qt.DirectConnection)
    assert core.gimbal["sdkHost"].get_value() == "192.168.144.26"

    core.viewer["useExternal3DMapSource"].set_value(True)
    settle()
    assert any(item.isVisible() and item.property("label") == "Origin Latitude"
               for item in descendants(root)), "External map controls did not expand"
    core.viewer["useGoogle3DMapSource"].set_value(True)
    settle()
    assert not core.viewer["useExternal3DMapSource"].get_value(), "3D source exclusivity changed"
    print("PASS: switch, combo, text-field Fact bindings and 3D source visibility/exclusivity")

    errors = [message for message in WARNINGS if any(token in message for token in
              ("TypeError", "ReferenceError", "Binding loop", "Unable to assign", "is not a type", "Cannot assign"))]
    assert not errors, "\n".join(errors)
    print("PASS: no QML binding/type errors; screenshots saved to", output)
    view.close()


if __name__ == "__main__":
    main()
