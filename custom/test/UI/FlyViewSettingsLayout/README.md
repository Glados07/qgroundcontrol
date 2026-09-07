# Fly View settings layout smoke test

The production page and local responsive components are loaded from the actual
`custom/custom.qrc` resource with PySide6 (Qt 6). The test checks
320/480/800/1200/1920 pixel viewports, 150% fonts, dark/light palettes, and
produces screenshots. It verifies centered content with a font-scaled maximum
width, narrow-screen shrink-to-fit, and transparent section backgrounds with
one-pixel outlines matching the native settings style.
It also checks switch/text/combo Fact write-through and 3D source switching.

Native `FactTextField`, `FactComboBox`, `FactCheckBoxSlider`, `QGCComboBox`,
`QGCLabel` and `QGCFlickable` are used. App services, Fact metadata objects, the
palette, file dialogs and text-field chrome are test doubles; the palette colors
match the current native QGC definitions. This is not a full
QGC or Android test. `showUniRcSettings` enables Android-only layout in the test
without changing the application's platform default. A small preview translator
provides Chinese screenshot labels; production translations are unchanged.

From the repository root, with PySide6 installed:

```text
rcc --binary custom/custom.qrc -o debug/flyview-settings-layout/custom.rcc
python custom/test/UI/FlyViewSettingsLayout/run.py --resource debug/flyview-settings-layout/custom.rcc --output debug/flyview-settings-layout/screenshots
```

Create the output parent directory first and use the Qt 6 `rcc` executable. The
test pins Qt 6 plugin/import paths to avoid picking up another Qt installation.
It uses the offscreen/software renderer; no visible application is launched and
no real settings, vehicle, Bluetooth or camera connection is accessed.
