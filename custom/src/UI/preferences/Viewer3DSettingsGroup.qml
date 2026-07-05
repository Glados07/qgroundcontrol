/****************************************************************************
 *
 * Viewer3D settings group for SecDev custom build.
 * 该文件只承载 Viewer3D 设置组，避免把 3D 模块逻辑散落到目标分支原有设置页中。
 *
 ****************************************************************************/

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

import QGroundControl
import QGroundControl.FactSystem
import QGroundControl.FactControls
import QGroundControl.Controls
import QGroundControl.ScreenTools

Loader {
    id: root

    Layout.fillWidth: true
    active: viewer3DReady && viewer3DSettingsVisible()
    sourceComponent: viewer3DSettingsComponent

    // Viewer3D 设置和外部模型导入管理器由 CustomPlugin 暴露，先检查对象和 Fact 是否齐全。
    property var  viewer3DSettings:             viewer3DCorePluginObject("viewer3DSettings")
    property var  external3DMapManager:         viewer3DCorePluginObject("external3DMapManager")
    property bool external3DMapManagerReady:    external3DMapManager !== null && external3DMapManager !== undefined
    property bool viewer3DReady:                viewer3DRequiredFactsReady()

    property var enabledFact:                   viewer3DFact("enabled")
    property var useGoogle3DMapSourceFact:      viewer3DFact("useGoogle3DMapSource")
    property var google3DMapsApiKeyFact:        viewer3DFact("google3DMapsApiKey")
    property var useExternal3DMapSourceFact:    viewer3DFact("useExternal3DMapSource")
    property var external3DMapFilePathFact:     viewer3DFact("external3DMapFilePath")
    property var external3DMapOriginLatFact:    viewer3DFact("external3DMapOriginLatitude")
    property var external3DMapOriginLonFact:    viewer3DFact("external3DMapOriginLongitude")
    property var external3DMapOriginAltFact:    viewer3DFact("external3DMapOriginAltitude")
    property var external3DMapUnitToMetersFact: viewer3DFact("external3DMapUnitToMeters")
    property var external3DMapScaleFact:        viewer3DFact("external3DMapScale")
    property var external3DMapYawFact:          viewer3DFact("external3DMapYaw")
    property var osmFilePathFact:               viewer3DFact("osmFilePath")
    property var buildingLevelHeightFact:       viewer3DFact("buildingLevelHeight")
    property var altitudeBiasFact:              viewer3DFact("altitudeBias")

    onLoaded:       if (item) { item.width = width }
    onWidthChanged: if (item) { item.width = width }

    function viewer3DCorePluginObject(name) {
        try {
            if (QGroundControl.corePlugin && QGroundControl.corePlugin[name] !== undefined) {
                return QGroundControl.corePlugin[name]
            }
        } catch (e) {
            console.warn("Viewer3D setting object is not available:", name, e)
        }
        return null
    }

    function viewer3DFact(name) {
        if (!viewer3DSettings) {
            return null
        }
        var fact = viewer3DSettings[name]
        return fact === undefined ? null : fact
    }

    function viewer3DRequiredFactsReady() {
        var requiredFacts = [
            "enabled",
            "useGoogle3DMapSource",
            "google3DMapsApiKey",
            "useExternal3DMapSource",
            "external3DMapFilePath",
            "external3DMapOriginLatitude",
            "external3DMapOriginLongitude",
            "external3DMapOriginAltitude",
            "external3DMapUnitToMeters",
            "external3DMapScale",
            "external3DMapYaw",
            "osmFilePath",
            "buildingLevelHeight",
            "altitudeBias"
        ]
        for (var i = 0; i < requiredFacts.length; i++) {
            if (!viewer3DFact(requiredFacts[i])) {
                return false
            }
        }
        return true
    }

    function viewer3DSettingsVisible() {
        var settingsVisible = viewer3DSettings ? viewer3DSettings["visible"] : undefined
        return viewer3DSettings && (settingsVisible === undefined || settingsVisible)
    }

    function factRaw(fact, fallbackValue) {
        return fact && fact.rawValue !== undefined ? fact.rawValue : fallbackValue
    }

    function factVisible(fact) {
        return fact && (fact.visible === undefined || fact.visible)
    }

    function setFactValue(fact, value) {
        if (fact) {
            fact.rawValue = value
        }
    }

    Component {
        id: viewer3DSettingsComponent

        SettingsGroupLayout {
            Layout.fillWidth: true
            heading:          qsTr("3D View")

            QGCCheckBoxSlider {
                Layout.fillWidth: true
                text:             qsTr("Enabled")
                checked:          factRaw(root.enabledFact, false)
                visible:          factVisible(root.enabledFact)

                onClicked: setFactValue(root.enabledFact, checked)
            }

            QGCCheckBoxSlider {
                Layout.fillWidth: true
                text:             qsTr("Use Google 3D Maps")
                checked:          factRaw(root.useGoogle3DMapSourceFact, false)
                enabled:          factRaw(root.enabledFact, false)
                visible:          factVisible(root.useGoogle3DMapSourceFact)

                onClicked: setFactValue(root.useGoogle3DMapSourceFact, checked)
            }

            QGCCheckBoxSlider {
                Layout.fillWidth: true
                text:             qsTr("Use External 3D Model Map")
                checked:          factRaw(root.useExternal3DMapSourceFact, false)
                enabled:          factRaw(root.enabledFact, false) && !factRaw(root.useGoogle3DMapSourceFact, false)
                visible:          factVisible(root.useExternal3DMapSourceFact) && !factRaw(root.useGoogle3DMapSourceFact, false)

                onClicked: setFactValue(root.useExternal3DMapSourceFact, checked)
            }

            Connections {
                target: root.useGoogle3DMapSourceFact
                function onRawValueChanged() {
                    if (factRaw(root.useGoogle3DMapSourceFact, false) && factRaw(root.useExternal3DMapSourceFact, false)) {
                        setFactValue(root.useExternal3DMapSourceFact, false)
                    }
                }
            }

            Connections {
                target: root.useExternal3DMapSourceFact
                function onRawValueChanged() {
                    if (factRaw(root.useExternal3DMapSourceFact, false) && factRaw(root.useGoogle3DMapSourceFact, false)) {
                        setFactValue(root.useGoogle3DMapSourceFact, false)
                    }
                }
            }

            LabelledFactTextField {
                Layout.fillWidth: true
                label:            qsTr("Google 3D Maps API Key")
                fact:             root.google3DMapsApiKeyFact
                enabled:          factRaw(root.enabledFact, false) && factRaw(root.useGoogle3DMapSourceFact, false)
                visible:          factVisible(root.google3DMapsApiKeyFact) && factRaw(root.useGoogle3DMapSourceFact, false)
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing:          ScreenTools.defaultFontPixelWidth
                enabled:          factRaw(root.enabledFact, false) && factRaw(root.useExternal3DMapSourceFact, false) && !factRaw(root.useGoogle3DMapSourceFact, false)
                visible:          factRaw(root.useExternal3DMapSourceFact, false) && !factRaw(root.useGoogle3DMapSourceFact, false)

                RowLayout {
                    Layout.fillWidth: true
                    spacing:          ScreenTools.defaultFontPixelWidth

                    QGCLabel {
                        wrapMode: Text.WordWrap
                        text:     qsTr("External 3D Model File:")
                    }

                    QGCTextField {
                        id:               external3DMapFileTextField
                        height:           ScreenTools.defaultFontPixelWidth * 4.5
                        unitsLabel:       ""
                        showUnits:        false
                        Layout.fillWidth: true
                        readOnly:         true
                        text:             factRaw(root.external3DMapFilePathFact, "")
                    }
                }

                RowLayout {
                    Layout.alignment: Qt.AlignRight
                    spacing:          ScreenTools.defaultFontPixelWidth

                    QGCButton {
                        text: qsTr("Clear")

                        onClicked: {
                            external3DMapFileTextField.text = "Please select an external 3D model file"
                            setFactValue(root.external3DMapFilePathFact, external3DMapFileTextField.text)
                            if (root.external3DMapManagerReady) {
                                root.external3DMapManager.clearStatus()
                            }
                        }
                    }

                    QGCButton {
                        text:    root.external3DMapManagerReady && root.external3DMapManager.importing ? qsTr("Importing...") : qsTr("Select File")
                        enabled: root.external3DMapManagerReady && !root.external3DMapManager.importing

                        onClicked: {
                            var filename = factRaw(root.external3DMapFilePathFact, "")
                            const found = filename.match(/(.*)[\/\\]/)
                            if (found) {
                                filename = found[1] || ""
                                external3DMapFileDialog.folder = (filename[0] === "/") ? filename.slice(1) : filename
                            }
                            external3DMapFileDialog.openForLoad()
                        }

                        QGCFileDialog {
                            id:          external3DMapFileDialog
                            nameFilters: [qsTr("3D model files (*.obj *.gltf *.glb *.qml *.fbx *.dae *.stl *.ply)"), qsTr("Runtime loadable (*.obj *.gltf *.glb)"), qsTr("Balsam generated QML (*.qml)"), qsTr("Authoring files (*.fbx *.dae *.stl *.ply)")]
                            title:       qsTr("Select external 3D model map")

                            onAcceptedForLoad: (file) => {
                                external3DMapFileTextField.text = file
                                if (root.external3DMapManagerReady) {
                                    root.external3DMapManager.importModelFile(file)
                                }
                            }
                        }
                    }
                }

                QGCLabel {
                    Layout.fillWidth: true
                    wrapMode:         Text.WordWrap
                    visible:          root.external3DMapManagerReady && root.external3DMapManager.lastImportStatus.length > 0
                    text:             root.external3DMapManagerReady ? root.external3DMapManager.lastImportStatus : ""
                }

                LabelledFactTextField {
                    Layout.fillWidth: true
                    label:            qsTr("Origin Latitude")
                    fact:             root.external3DMapOriginLatFact
                    visible:          factVisible(root.external3DMapOriginLatFact)
                }

                LabelledFactTextField {
                    Layout.fillWidth: true
                    label:            qsTr("Origin Longitude")
                    fact:             root.external3DMapOriginLonFact
                    visible:          factVisible(root.external3DMapOriginLonFact)
                }

                LabelledFactTextField {
                    Layout.fillWidth: true
                    label:            qsTr("Origin Altitude")
                    fact:             root.external3DMapOriginAltFact
                    visible:          factVisible(root.external3DMapOriginAltFact)
                }

                LabelledFactTextField {
                    Layout.fillWidth: true
                    label:            qsTr("Model Unit To Meters")
                    fact:             root.external3DMapUnitToMetersFact
                    visible:          factVisible(root.external3DMapUnitToMetersFact)
                }

                LabelledFactTextField {
                    Layout.fillWidth: true
                    label:            qsTr("Model Scale")
                    fact:             root.external3DMapScaleFact
                    visible:          factVisible(root.external3DMapScaleFact)
                }

                LabelledFactTextField {
                    Layout.fillWidth: true
                    label:            qsTr("North/Yaw Angle")
                    fact:             root.external3DMapYawFact
                    visible:          factVisible(root.external3DMapYawFact)
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing:          ScreenTools.defaultFontPixelWidth
                enabled:          factRaw(root.enabledFact, false) && !factRaw(root.useGoogle3DMapSourceFact, false) && !factRaw(root.useExternal3DMapSourceFact, false)
                visible:          !factRaw(root.useGoogle3DMapSourceFact, false) && !factRaw(root.useExternal3DMapSourceFact, false)

                RowLayout {
                    Layout.fillWidth: true
                    spacing:          ScreenTools.defaultFontPixelWidth

                    QGCLabel {
                        wrapMode: Text.WordWrap
                        text:     qsTr("3D Map File:")
                    }

                    QGCTextField {
                        id:               osmFileTextField
                        height:           ScreenTools.defaultFontPixelWidth * 4.5
                        unitsLabel:       ""
                        showUnits:        false
                        Layout.fillWidth: true
                        readOnly:         true
                        text:             factRaw(root.osmFilePathFact, "")
                    }
                }

                RowLayout {
                    Layout.alignment: Qt.AlignRight
                    spacing:          ScreenTools.defaultFontPixelWidth

                    QGCButton {
                        text: qsTr("Clear")

                        onClicked: {
                            osmFileTextField.text = "Please select an OSM file"
                            setFactValue(root.osmFilePathFact, osmFileTextField.text)
                        }
                    }

                    QGCButton {
                        text: qsTr("Select File")

                        onClicked: {
                            var filename = factRaw(root.osmFilePathFact, "")
                            const found = filename.match(/(.*)[\/\\]/)
                            if (found) {
                                filename = found[1] || ""
                                fileDialog.folder = (filename[0] === "/") ? filename.slice(1) : filename
                            }
                            fileDialog.openForLoad()
                        }

                        QGCFileDialog {
                            id:          fileDialog
                            nameFilters: [qsTr("OpenStreetMap files (*.osm)")]
                            title:       qsTr("Select map file")

                            onAcceptedForLoad: (file) => {
                                osmFileTextField.text = file
                                setFactValue(root.osmFilePathFact, osmFileTextField.text)
                            }
                        }
                    }
                }
            }

            LabelledFactTextField {
                Layout.fillWidth: true
                label:            qsTr("Average Building Level Height")
                fact:             root.buildingLevelHeightFact
                enabled:          factRaw(root.enabledFact, false) && !factRaw(root.useGoogle3DMapSourceFact, false) && !factRaw(root.useExternal3DMapSourceFact, false)
                visible:          factVisible(root.buildingLevelHeightFact) && !factRaw(root.useGoogle3DMapSourceFact, false) && !factRaw(root.useExternal3DMapSourceFact, false)
            }

            LabelledFactTextField {
                Layout.fillWidth: true
                label:            qsTr("Vehicles Altitude Bias")
                fact:             root.altitudeBiasFact
                enabled:          factRaw(root.enabledFact, false) && !factRaw(root.useGoogle3DMapSourceFact, false)
                visible:          factVisible(root.altitudeBiasFact) && !factRaw(root.useGoogle3DMapSourceFact, false)
            }
        }
    }
}
