/****************************************************************************
 *
 * (c) 2009-2020 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
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
import QGroundControl.MultiVehicleManager
import QGroundControl.Palette
import QGroundControl.Controllers

SettingsPage {
    property var    _settingsManager:                   QGroundControl.settingsManager
    property var    _flyViewSettings:                   _settingsManager.flyViewSettings
    property var    _mavlinkActionsSettings:            _settingsManager.mavlinkActionsSettings
    property Fact   _virtualJoystick:                   _settingsManager.appSettings.virtualJoystick
    property Fact   _virtualJoystickAutoCenterThrottle: _settingsManager.appSettings.virtualJoystickAutoCenterThrottle
    property Fact   _virtualJoystickLeftHandedMode:     _settingsManager.appSettings.virtualJoystickLeftHandedMode
    property Fact   _enableMultiVehiclePanel:           _settingsManager.appSettings.enableMultiVehiclePanel
    property Fact   _showAdditionalIndicatorsCompass:   _flyViewSettings.showAdditionalIndicatorsCompass
    property Fact   _lockNoseUpCompass:                 _flyViewSettings.lockNoseUpCompass
    property Fact   _guidedMinimumAltitude:             _flyViewSettings.guidedMinimumAltitude
    property Fact   _guidedMaximumAltitude:             _flyViewSettings.guidedMaximumAltitude
    property Fact   _maxGoToLocationDistance:           _flyViewSettings.maxGoToLocationDistance
    property var    _viewer3DSettings:                  QGroundControl.corePlugin.viewer3DSettings
    property var    _viewer3DExternal3DMapManager:      QGroundControl.corePlugin.external3DMapManager
    property Fact   _viewer3DEnabled:                   _viewer3DSettings.enabled
    property Fact   _viewer3DUseGoogle3DMapSource:      _viewer3DSettings.useGoogle3DMapSource
    property Fact   _viewer3DGoogle3DMapsApiKey:        _viewer3DSettings.google3DMapsApiKey
    property Fact   _viewer3DUseExternal3DMapSource:    _viewer3DSettings.useExternal3DMapSource
    property Fact   _viewer3DExternal3DMapFilePath:     _viewer3DSettings.external3DMapFilePath
    property Fact   _viewer3DExternal3DMapOriginLat:    _viewer3DSettings.external3DMapOriginLatitude
    property Fact   _viewer3DExternal3DMapOriginLon:    _viewer3DSettings.external3DMapOriginLongitude
    property Fact   _viewer3DExternal3DMapOriginAlt:    _viewer3DSettings.external3DMapOriginAltitude
    property Fact   _viewer3DExternal3DMapUnitToMeters: _viewer3DSettings.external3DMapUnitToMeters
    property Fact   _viewer3DExternal3DMapScale:        _viewer3DSettings.external3DMapScale
    property Fact   _viewer3DExternal3DMapYaw:          _viewer3DSettings.external3DMapYaw
    property Fact   _viewer3DOsmFilePath:               _viewer3DSettings.osmFilePath
    property Fact   _viewer3DBuildingLevelHeight:       _viewer3DSettings.buildingLevelHeight
    property Fact   _viewer3DAltitudeBias:              _viewer3DSettings.altitudeBias
    QGCFileDialogController { id: fileController }

    function mavlinkActionList() {
        var fileModel = fileController.getFiles(_settingsManager.appSettings.mavlinkActionsSavePath, "*.json")
        fileModel.unshift(qsTr("<None>"))
        return fileModel
    }

    SettingsGroupLayout {
        Layout.fillWidth:   true
        heading:            qsTr("General")

        FactCheckBoxSlider {
            id:                 useCheckList
            Layout.fillWidth:   true
            text:               qsTr("Use Preflight Checklist")
            fact:               _useChecklist
            visible:            _useChecklist.visible && QGroundControl.corePlugin.options.preFlightChecklistUrl.toString().length
            property Fact _useChecklist:      _settingsManager.appSettings.useChecklist
        }

        FactCheckBoxSlider {
            Layout.fillWidth:   true
            text:               qsTr("Enforce Preflight Checklist")
            fact:               _enforceChecklist
            enabled:            _settingsManager.appSettings.useChecklist.value
            visible:            useCheckList.visible && _enforceChecklist.visible
            property Fact _enforceChecklist: _settingsManager.appSettings.enforceChecklist
        }

        FactCheckBoxSlider {
            Layout.fillWidth:   true
            text:               qsTr("Enable Multi-Vehicle Panel")
            fact:               _enableMultiVehiclePanel
            visible:            _enableMultiVehiclePanel.visible
        }

        FactCheckBoxSlider {
            Layout.fillWidth:   true
            text:               qsTr("Keep Map Centered On Vehicle")
            fact:               _keepMapCenteredOnVehicle
            visible:            _keepMapCenteredOnVehicle.visible
            property Fact _keepMapCenteredOnVehicle: _flyViewSettings.keepMapCenteredOnVehicle
        }

        FactCheckBoxSlider {
            Layout.fillWidth:   true
            text:               qsTr("Show Telemetry Log Replay Status Bar")
            fact:               _showLogReplayStatusBar
            visible:            _showLogReplayStatusBar.visible
            property Fact _showLogReplayStatusBar: _flyViewSettings.showLogReplayStatusBar
        }

        FactCheckBoxSlider {
            Layout.fillWidth:   true
            text:               qsTr("Show simple camera controls (DIGICAM_CONTROL)")
            visible:            _showDumbCameraControl.visible
            fact:               _showDumbCameraControl

            property Fact _showDumbCameraControl: _flyViewSettings.showSimpleCameraControl
        }

        FactCheckBoxSlider {
            Layout.fillWidth:   true
            text:               qsTr("Update return to home position based on device location.")
            fact:               _updateHomePosition
            visible:            _updateHomePosition.visible
            property Fact _updateHomePosition: _flyViewSettings.updateHomePosition
        }
    }

    SettingsGroupLayout {
        Layout.fillWidth:   true
        heading:            qsTr("Guided Commands")
        visible:            _guidedMinimumAltitude.visible || _guidedMaximumAltitude.visible || _maxGoToLocationDistance.visible

        LabelledFactTextField {
            Layout.fillWidth:   true
            label:              qsTr("Minimum Altitude")
            fact:               _guidedMinimumAltitude
            visible:            fact.visible
        }

        LabelledFactTextField {
            Layout.fillWidth:   true
            label:              qsTr("Maximum Altitude")
            fact:               _guidedMaximumAltitude
            visible:            fact.visible
        }

        LabelledFactTextField {
            Layout.fillWidth:   true
            label:              qsTr("Go To Location Max Distance")
            fact:               _maxGoToLocationDistance
            visible:            fact.visible
        }
    }

    SettingsGroupLayout {
        Layout.fillWidth:       true
        Layout.preferredWidth:  ScreenTools.defaultFontPixelWidth * 35
        heading:                qsTr("MAVLink Actions")
        headingDescription:     qsTr("Action JSON files should be created in the '%1' folder.").arg(QGroundControl.settingsManager.appSettings.mavlinkActionsSavePath)

        LabelledComboBox {
            Layout.fillWidth:   true
            label:              qsTr("Fly View Actions")
            model:              mavlinkActionList()
            onActivated:        (index) => index == 0 ? _mavlinkActionsSettings.flyViewActionsFile.rawValue = "" : _mavlinkActionsSettings.flyViewActionsFile.rawValue = comboBox.currentText
            enabled:            model.length > 1

            Component.onCompleted: {
                var index = comboBox.find(_mavlinkActionsSettings.flyViewActionsFile.valueString)
                comboBox.currentIndex = index == -1 ? 0 : index
            }
        }

        LabelledComboBox {
            Layout.fillWidth:   true
            label:              qsTr("Joystick Actions")
            model:              mavlinkActionList()
            onActivated:        (index) => index == 0 ? _mavlinkActionsSettings.joystickActionsFile.rawValue = "" : _mavlinkActionsSettings.joystickActionsFile.rawValue = comboBox.currentText
            enabled:            model.length > 1

            Component.onCompleted: {
                var index = comboBox.find(_mavlinkActionsSettings.joystickActionsFile.valueString)
                comboBox.currentIndex = index == -1 ? 0 : index
            }
        }
    }

    SettingsGroupLayout {
        Layout.fillWidth:   true
        heading:            qsTr("Virtual Joystick")
        visible:            _virtualJoystick.visible || _virtualJoystickAutoCenterThrottle.visible || _virtualJoystickLeftHandedMode.visible

        FactCheckBoxSlider {
            Layout.fillWidth:   true
            text:               qsTr("Enabled")
            visible:            _virtualJoystick.visible
            fact:               _virtualJoystick
        }

        FactCheckBoxSlider {
            Layout.fillWidth:   true
            text:               qsTr("Auto-Center Throttle")
            visible:            _virtualJoystickAutoCenterThrottle.visible
            enabled:            _virtualJoystick.rawValue
            fact:               _virtualJoystickAutoCenterThrottle
        }

        FactCheckBoxSlider {
            Layout.fillWidth:   true
            text:               qsTr("Left-Handed Mode (swap sticks)")
            visible:            _virtualJoystickLeftHandedMode.visible
            enabled:            _virtualJoystick.rawValue
            fact:               _virtualJoystickLeftHandedMode
        }
    }

    SettingsGroupLayout {
        Layout.fillWidth:   true
        heading:            qsTr("Instrument Panel")
        visible:            _showAdditionalIndicatorsCompass.visible || _lockNoseUpCompass.visible

        FactCheckBoxSlider {
            Layout.fillWidth:   true
            text:               qsTr("Show additional heading indicators on Compass")
            visible:            _showAdditionalIndicatorsCompass.visible
            fact:               _showAdditionalIndicatorsCompass
        }

        FactCheckBoxSlider {
            Layout.fillWidth:   true
            text:               qsTr("Lock Compass Nose-Up")
            visible:            _lockNoseUpCompass.visible
            fact:               _lockNoseUpCompass
        }
    }

    SettingsGroupLayout {
        Layout.fillWidth:   true
        heading:            qsTr("3D View")
        visible:            _viewer3DSettings.visible

        FactCheckBoxSlider {
            Layout.fillWidth:   true
            text:               qsTr("Enabled")
            fact:               _viewer3DEnabled
            visible:            _viewer3DEnabled.visible
        }

        FactCheckBoxSlider {
            Layout.fillWidth:   true
            text:               qsTr("Use Google 3D Maps")
            fact:               _viewer3DUseGoogle3DMapSource
            enabled:            _viewer3DEnabled.rawValue
            visible:            _viewer3DUseGoogle3DMapSource.visible
        }

        FactCheckBoxSlider {
            Layout.fillWidth:   true
            text:               qsTr("Use External 3D Model Map")
            fact:               _viewer3DUseExternal3DMapSource
            enabled:            _viewer3DEnabled.rawValue && !_viewer3DUseGoogle3DMapSource.rawValue
            visible:            _viewer3DUseExternal3DMapSource.visible && !_viewer3DUseGoogle3DMapSource.rawValue
        }

        Connections {
            target: _viewer3DUseGoogle3DMapSource
            function onRawValueChanged() {
                if (_viewer3DUseGoogle3DMapSource.rawValue && _viewer3DUseExternal3DMapSource.rawValue) {
                    _viewer3DUseExternal3DMapSource.value = false
                }
            }
        }

        Connections {
            target: _viewer3DUseExternal3DMapSource
            function onRawValueChanged() {
                if (_viewer3DUseExternal3DMapSource.rawValue && _viewer3DUseGoogle3DMapSource.rawValue) {
                    _viewer3DUseGoogle3DMapSource.value = false
                }
            }
        }

        LabelledFactTextField {
            Layout.fillWidth:   true
            label:              qsTr("Google 3D Maps API Key")
            fact:               _viewer3DGoogle3DMapsApiKey
            enabled:            _viewer3DEnabled.rawValue && _viewer3DUseGoogle3DMapSource.rawValue
            visible:            _viewer3DGoogle3DMapsApiKey.visible && _viewer3DUseGoogle3DMapSource.rawValue
        }

        ColumnLayout{
            Layout.fillWidth:   true
            spacing:            ScreenTools.defaultFontPixelWidth
            enabled:            _viewer3DEnabled.rawValue && _viewer3DUseExternal3DMapSource.rawValue && !_viewer3DUseGoogle3DMapSource.rawValue
            visible:            _viewer3DUseExternal3DMapSource.rawValue && !_viewer3DUseGoogle3DMapSource.rawValue

            RowLayout{
                Layout.fillWidth:   true
                spacing:            ScreenTools.defaultFontPixelWidth

                QGCLabel {
                    wrapMode:   Text.WordWrap
                    text:       qsTr("External 3D Model File:")
                }

                QGCTextField {
                    id:                 external3DMapFileTextField
                    height:             ScreenTools.defaultFontPixelWidth * 4.5
                    unitsLabel:         ""
                    showUnits:          false
                    Layout.fillWidth:   true
                    readOnly:           true
                    text:               _viewer3DExternal3DMapFilePath.rawValue
                }
            }

            RowLayout{
                Layout.alignment:   Qt.AlignRight
                spacing:            ScreenTools.defaultFontPixelWidth

                QGCButton {
                    text: qsTr("Clear")

                    onClicked: {
                        external3DMapFileTextField.text = "Please select an external 3D model file"
                        _viewer3DExternal3DMapFilePath.value = external3DMapFileTextField.text
                        _viewer3DExternal3DMapManager.clearStatus()
                    }
                }

                QGCButton {
                    text: _viewer3DExternal3DMapManager.importing ? qsTr("Importing...") : qsTr("Select File")
                    enabled: !_viewer3DExternal3DMapManager.importing

                    onClicked: {
                        var filename = _viewer3DExternal3DMapFilePath.rawValue
                        const found = filename.match(/(.*)[\/\\]/)
                        if (found) {
                            filename = found[1] || ''
                            external3DMapFileDialog.folder = (filename[0] === "/") ? filename.slice(1) : filename
                        }
                        external3DMapFileDialog.openForLoad()
                    }

                    QGCFileDialog {
                        id:             external3DMapFileDialog
                        nameFilters:    [qsTr("3D model files (*.obj *.gltf *.glb *.qml *.fbx *.dae *.stl *.ply)"), qsTr("Runtime loadable (*.obj *.gltf *.glb)"), qsTr("Balsam generated QML (*.qml)"), qsTr("Authoring files (*.fbx *.dae *.stl *.ply)")]
                        title:          qsTr("Select external 3D model map")

                        onAcceptedForLoad: (file) => {
                            external3DMapFileTextField.text = file
                            _viewer3DExternal3DMapManager.importModelFile(file)
                        }
                    }
                }
            }

            QGCLabel {
                Layout.fillWidth:   true
                wrapMode:           Text.WordWrap
                visible:            _viewer3DExternal3DMapManager.lastImportStatus.length > 0
                text:               _viewer3DExternal3DMapManager.lastImportStatus
            }

            LabelledFactTextField {
                Layout.fillWidth:   true
                label:              qsTr("Origin Latitude")
                fact:               _viewer3DExternal3DMapOriginLat
                visible:            _viewer3DExternal3DMapOriginLat.visible
            }

            LabelledFactTextField {
                Layout.fillWidth:   true
                label:              qsTr("Origin Longitude")
                fact:               _viewer3DExternal3DMapOriginLon
                visible:            _viewer3DExternal3DMapOriginLon.visible
            }

            LabelledFactTextField {
                Layout.fillWidth:   true
                label:              qsTr("Origin Altitude")
                fact:               _viewer3DExternal3DMapOriginAlt
                visible:            _viewer3DExternal3DMapOriginAlt.visible
            }

            LabelledFactTextField {
                Layout.fillWidth:   true
                label:              qsTr("Model Unit To Meters")
                fact:               _viewer3DExternal3DMapUnitToMeters
                visible:            _viewer3DExternal3DMapUnitToMeters.visible
            }

            LabelledFactTextField {
                Layout.fillWidth:   true
                label:              qsTr("Model Scale")
                fact:               _viewer3DExternal3DMapScale
                visible:            _viewer3DExternal3DMapScale.visible
            }

            LabelledFactTextField {
                Layout.fillWidth:   true
                label:              qsTr("North/Yaw Angle")
                fact:               _viewer3DExternal3DMapYaw
                visible:            _viewer3DExternal3DMapYaw.visible
            }
        }

        ColumnLayout{
            Layout.fillWidth:   true
            spacing:            ScreenTools.defaultFontPixelWidth
            enabled:            _viewer3DEnabled.rawValue && !_viewer3DUseGoogle3DMapSource.rawValue && !_viewer3DUseExternal3DMapSource.rawValue
            visible:            _viewer3DOsmFilePath.rawValue && !_viewer3DUseGoogle3DMapSource.rawValue && !_viewer3DUseExternal3DMapSource.rawValue

            RowLayout{
                Layout.fillWidth:   true
                spacing:            ScreenTools.defaultFontPixelWidth

                QGCLabel {
                    wrapMode:   Text.WordWrap
                    visible:    true
                    text:       qsTr("3D Map File:")
                }

                QGCTextField {
                    id:                 osmFileTextField
                    height:             ScreenTools.defaultFontPixelWidth * 4.5
                    unitsLabel:         ""
                    showUnits:          false
                    visible:            true
                    Layout.fillWidth:   true
                    readOnly:           true
                    text:               _viewer3DOsmFilePath.rawValue
                }
            }

            RowLayout{
                Layout.alignment:   Qt.AlignRight
                spacing:            ScreenTools.defaultFontPixelWidth

                QGCButton {
                    text: qsTr("Clear")

                    onClicked: {
                        osmFileTextField.text = "Please select an OSM file"
                        _viewer3DOsmFilePath.value = osmFileTextField.text
                    }
                }

                QGCButton {
                    text: qsTr("Select File")

                    onClicked: {
                        var filename = _viewer3DOsmFilePath.rawValue
                        const found = filename.match(/(.*)[\/\\]/)
                        if(found){
                            filename = found[1]||''
                            fileDialog.folder = (filename[0] === "/")?(filename.slice(1)):(filename)
                        }
                        fileDialog.openForLoad()
                    }

                    QGCFileDialog {
                        id:             fileDialog
                        nameFilters:    [qsTr("OpenStreetMap files (*.osm)")]
                        title:          qsTr("Select map file")

                        onAcceptedForLoad: (file) => {
                            osmFileTextField.text = file
                            _viewer3DOsmFilePath.value = osmFileTextField.text
                        }
                    }
                }
            }
        }

        LabelledFactTextField {
            Layout.fillWidth:   true
            label:              qsTr("Average Building Level Height")
            fact:               _viewer3DBuildingLevelHeight
            enabled:            _viewer3DEnabled.rawValue && !_viewer3DUseGoogle3DMapSource.rawValue && !_viewer3DUseExternal3DMapSource.rawValue
            visible:            _viewer3DBuildingLevelHeight.visible && !_viewer3DUseGoogle3DMapSource.rawValue && !_viewer3DUseExternal3DMapSource.rawValue
        }

        LabelledFactTextField {
            Layout.fillWidth:   true
            label:              qsTr("Vehicles Altitude Bias")
            fact:               _viewer3DAltitudeBias
            enabled:            _viewer3DEnabled.rawValue && !_viewer3DUseGoogle3DMapSource.rawValue
            visible:            _viewer3DAltitudeBias.visible && !_viewer3DUseGoogle3DMapSource.rawValue
        }
    }
}
