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
import QtQuick.Layouts

import QGroundControl
import QGroundControl.FactSystem
import QGroundControl.FactControls
import QGroundControl.Controls
import QGroundControl.Palette
import QGroundControl.ScreenTools
import QGroundControl.AppSettings

SettingsPage {
    property var    _settingsManager:            QGroundControl.settingsManager
    property var    _videoManager:              QGroundControl.videoManager
    property var    _videoSettings:             _settingsManager.videoSettings
    property string _videoSource:               _videoSettings.videoSource.rawValue
    property bool   _isGST:                     _videoManager.gstreamerEnabled
    property bool   _isStreamSource:            _videoManager.isStreamSource
    property bool   _isUDP264:                  _isStreamSource && (_videoSource === _videoSettings.udp264VideoSource)
    property bool   _isUDP265:                  _isStreamSource && (_videoSource === _videoSettings.udp265VideoSource)
    property bool   _isRTSP:                    _isStreamSource && (_videoSource === _videoSettings.rtspVideoSource)
    property bool   _isTCP:                     _isStreamSource && (_videoSource === _videoSettings.tcpVideoSource)
    property bool   _isMPEGTS:                  _isStreamSource && (_videoSource === _videoSettings.mpegtsVideoSource)
    property bool   _videoAutoStreamConfig:     _videoManager.autoStreamConfigured
    property bool   _videoSourceDisabled:       _videoSource === _videoSettings.disabledVideoSource
    property real   _urlFieldWidth:             ScreenTools.defaultFontPixelWidth * 40
    property bool   _requiresUDPUrl:            _isUDP264 || _isUDP265 || _isMPEGTS
    property var    _gimbalControlSettings:     QGroundControl.corePlugin.gimbalControlSettings
    property var    _videoCustomSettings:       QGroundControl.corePlugin.videoCustomSettings
    property var    _dualVideoManager:          QGroundControl.corePlugin.dualVideoManager

    QGCPalette { id: qgcPal }

    SettingsGroupLayout {
        Layout.fillWidth:   true
        heading:            qsTr("Video Source")
        headingDescription: _videoAutoStreamConfig ? qsTr("Mavlink camera stream is automatically configured") : ""
        enabled:            !_videoAutoStreamConfig

        LabelledFactComboBox {
            Layout.fillWidth:   true
            label:              qsTr("Source")
            indexModel:         false
            fact:               _videoSettings.videoSource
            visible:            fact.visible
        }
    }

    SettingsGroupLayout {
        Layout.fillWidth:   true
        heading:            qsTr("Connection")
        headingDescription: _isRTSP
                            ? qsTr("Video 1 and Video 2 are generic RTSP inputs; camera SDK controls are configured separately.")
                            : ""
        visible:            !_videoSourceDisabled && !_videoAutoStreamConfig && (_isTCP || _isRTSP || _requiresUDPUrl)

        LabelledFactTextField {
            Layout.fillWidth:           true
            textFieldPreferredWidth:    _urlFieldWidth
            label:                      qsTr("RTSP URL 1")
            fact:                       _videoSettings.rtspUrl
            visible:                    _isRTSP && _videoSettings.rtspUrl.visible
        }

        FactCheckBoxSlider {
            Layout.fillWidth: true
            text: qsTr("Prefer RTSP-over-TCP for URL 1")
            fact: _videoCustomSettings.primaryRtspTcpOnly
            visible: _isRTSP
        }

        LabelledFactTextField {
            Layout.fillWidth:           true
            textFieldPreferredWidth:    _urlFieldWidth
            label:                      qsTr("RTSP URL 2")
            fact:                       _videoCustomSettings.secondaryRtspUrl
            visible:                    _isRTSP
        }

        FactCheckBoxSlider {
            Layout.fillWidth: true
            text: qsTr("Prefer RTSP-over-TCP for URL 2")
            fact: _videoCustomSettings.secondaryRtspTcpOnly
            visible: _isRTSP
        }

        QGCLabel {
            Layout.fillWidth: true
            text: qsTr("RTSP URL 2 uses an independent receiver. Leave it empty to disable the second video window. Automatic transport is the default; when TCP is preferred but cannot establish media, QGC falls back to automatic negotiation.")
            visible: _isRTSP
            wrapMode: Text.WordWrap
            font.pointSize: ScreenTools.smallFontPointSize
            opacity: 0.72
        }

        QGCLabel {
            Layout.fillWidth: true
            text: qsTr("RTSP URL 2 matches the configured or active Video 1 stream. The second receiver is disabled to avoid opening the same stream twice.")
            visible: _isRTSP && _dualVideoManager
                     && _dualVideoManager.duplicateSource
            wrapMode: Text.WordWrap
            font.pointSize: ScreenTools.smallFontPointSize
            color: qgcPal.warningText
        }

        LabelledFactTextField {
            Layout.fillWidth:           true
            label:                      qsTr("TCP URL")
            textFieldPreferredWidth:    _urlFieldWidth
            fact:                       _videoSettings.tcpUrl
            visible:                    _isTCP && _videoSettings.tcpUrl.visible
        }

        LabelledFactTextField {
            Layout.fillWidth:           true
            textFieldPreferredWidth:    _urlFieldWidth
            label:                      qsTr("UDP URL")
            fact:                       _videoSettings.udpUrl
            visible:                    _requiresUDPUrl && _videoSettings.udpUrl.visible
        }
    }

    // Keep integration controls independent from MAVLink auto-stream locking
    // of the native Video Source and Connection groups.
    SettingsGroupLayout {
        Layout.fillWidth: true
        heading: qsTr("Video Stream Integration")
        headingDescription: qsTr("Controls MAVLink video source selection and Android H.265 hardware decoding.")

        FactCheckBoxSlider {
            Layout.fillWidth: true
            text: qsTr("Use MAVLink automatic video stream")
            fact: _gimbalControlSettings.mavlinkAutoVideoStream
        }

        QGCLabel {
            Layout.fillWidth: true
            text: qsTr("Keep this option off to edit the video source manually. Restart QGC after changing it.")
            wrapMode: Text.WordWrap
            font.pointSize: ScreenTools.smallFontPointSize
            opacity: 0.72
        }

        FactCheckBoxSlider {
            Layout.fillWidth: true
            text: qsTr("Force hardware decoding for Android H.265")
            fact: _gimbalControlSettings.forceAndroidH265HardwareDecoder
        }

        QGCLabel {
            Layout.fillWidth: true
            text: qsTr("Enabled by default for low-latency H.265 playback. If a device cannot open two MediaCodec decoder instances, turn this off for troubleshooting.") + " "
                  + qsTr("Restart QGC after changing this option.")
            wrapMode: Text.WordWrap
            font.pointSize: ScreenTools.smallFontPointSize
            opacity: 0.72
        }
    }

    SettingsGroupLayout {
        Layout.fillWidth:   true
        heading:            qsTr("Settings")
        visible:            !_videoSourceDisabled

        LabelledFactTextField {
            Layout.fillWidth:   true
            label:              qsTr("Aspect Ratio")
            fact:               _videoSettings.aspectRatio
            visible:            !_videoAutoStreamConfig && _isStreamSource && _videoSettings.aspectRatio.visible
        }

        FactCheckBoxSlider {
            Layout.fillWidth:   true
            text:               qsTr("Stop recording when disarmed")
            fact:               _videoSettings.disableWhenDisarmed
            visible:            !_videoAutoStreamConfig && _isStreamSource && fact.visible
        }

        FactCheckBoxSlider {
            Layout.fillWidth:   true
            text:               qsTr("Low Latency Mode")
            fact:               _videoSettings.lowLatencyMode
            visible:            !_videoAutoStreamConfig && _isStreamSource && fact.visible && _isGST
        }

        LabelledFactComboBox {
            Layout.fillWidth:   true
            label:              qsTr("Video decode priority")
            fact:               _videoSettings.forceVideoDecoder
            visible:            fact.visible
            indexModel:         false
        }
    }

    SettingsGroupLayout {
        Layout.fillWidth: true
        heading:            qsTr("Local Video Storage")

        FactCheckBoxSlider {
            Layout.fillWidth: true
            text:             qsTr("Save photos and videos locally")
            fact:             _gimbalControlSettings.localMediaStorageEnabled
        }

        QGCLabel {
            Layout.fillWidth: true
            text:             qsTr("Camera photo and recording actions also save media to this device, independently of gimbal SD card storage.")
            wrapMode:         Text.WordWrap
            font.pointSize:   ScreenTools.smallFontPointSize
            opacity:          0.72
        }

        LabelledFactComboBox {
            Layout.fillWidth:   true
            label:              qsTr("Record File Format")
            fact:               _videoSettings.recordingFormat
            visible:            _videoSettings.recordingFormat.visible
        }

        FactCheckBoxSlider {
            Layout.fillWidth:   true
            text:               qsTr("Auto-Delete Saved Recordings")
            fact:               _videoSettings.enableStorageLimit
            visible:            fact.visible
        }

        LabelledFactTextField {
            Layout.fillWidth:   true
            label:              qsTr("Max Storage Usage")
            fact:               _videoSettings.maxVideoSize
            visible:            fact.visible
            enabled:            _videoSettings.enableStorageLimit.rawValue
        }
    }
}
