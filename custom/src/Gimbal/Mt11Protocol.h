/****************************************************************************
 *
 * UniPod MT11 SDK wire-protocol helpers.
 *
 ****************************************************************************/

#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QList>
#include <QtCore/QtGlobal>

class Mt11Protocol
{
public:
    // Command 0x0f accepts only the documented 1.0x-30.0x absolute
    // target. Zoom feedback has a wider representation because commands
    // 0x05, 0x16 and 0x18 report the camera's hybrid zoom ratio.
    static constexpr double MinimumZoom = 1.0;
    static constexpr double MaximumAbsoluteZoom = 30.0;
    static constexpr double MaximumFeedbackZoom = 255.9;

    enum Command : quint8 {
        CommandManualZoom       = 0x05,
        CommandCameraSystemInfo = 0x0a,
        CommandFunctionFeedback = 0x0b,
        CommandPhotoAndRecord   = 0x0c,
        CommandAbsoluteZoom     = 0x0f,
        CommandVideoMode        = 0x10,
        CommandSetVideoMode     = 0x11,
        CommandMaximumZoomValue = 0x16,
        CommandCurrentZoomValue = 0x18,
        CommandCameraEncodingParameters = 0x20,
    };

    enum CameraStreamType : quint8 {
        CameraStreamRecording = 0,
        CameraStreamMain = 1,
        CameraStreamSub = 2,
    };

    enum VideoSource : quint8 {
        VideoSourceZoom = 0,
        VideoSourceWide = 1,
        VideoSourceThermal = 2,
        VideoSourceZoomAndThermal = 3,
        VideoSourceWideAndThermal = 4,
        VideoSourceZoomAndWide = 5,
        VideoSourceNone = 6,
    };

    // The MT11 camera-control UI exposes the three video layouts documented
    // for command 0x11. Their numeric values intentionally match main_stream.
    enum VideoWorkMode : qint8 {
        VideoWorkModeUnknown = -1,
        VideoWorkModeZoom = VideoSourceZoom,
        VideoWorkModeThermal = VideoSourceThermal,
        VideoWorkModeZoomAndThermal = VideoSourceZoomAndThermal,
    };

    struct DecodedPacket {
        bool valid = false;
        quint8 control = 0;
        quint8 command = 0;
        quint16 sequence = 0;
        QByteArray payload;
    };

    struct CameraSystemStatus {
        quint8 hdrStatus = 0;
        quint8 recordingStatus = 0;
        quint8 gimbalMotionMode = 0;
        quint8 gimbalMountingDirection = 0;
        quint8 videoOutputStatus = 0;
        quint8 zoomLinkage = 0;
    };

    struct VideoMode {
        quint8 mainStream = VideoSourceZoom;
        quint8 subStream = VideoSourceThermal;

        bool thermalOnMainStream() const {
            return mainStream == VideoSourceThermal;
        }
    };

    struct CameraEncodingParameters {
        quint8 streamType = CameraStreamRecording;
        quint8 videoEncodingType = 0;
        quint16 width = 0;
        quint16 height = 0;
        quint16 bitrateKbps = 0;
        quint8 frameRate = 0;
    };

    static QByteArray manualZoomPacket(qint8 direction);
    static QByteArray absoluteZoomPacket(double zoomLevel);
    static QByteArray requestMaximumZoomPacket();
    static QByteArray requestCurrentZoomPacket();
    static QByteArray requestCameraSystemStatusPacket();
    static QByteArray takePhotoPacket();
    static QByteArray toggleVideoRecordingPacket();
    static QByteArray requestVideoModePacket();
    static QByteArray setVideoModePacket(VideoWorkMode mode);
    static QByteArray requestCameraEncodingParametersPacket(quint8 streamType);
    // Compatibility wrapper for the former two-state UI.
    static QByteArray setThermalModePacket(bool thermalOnMainStream);

    static DecodedPacket decodePacket(const QByteArray& packet);
    static bool decodeDatagram(const QByteArray& datagram,
                               QList<DecodedPacket>* packets);
    static bool isAckPacket(const DecodedPacket& packet);

    static bool parseManualZoomAckPayload(const QByteArray& payload,
                                          double* zoomLevel);
    static bool parseAbsoluteZoomAckPayload(const QByteArray& payload,
                                            bool* accepted);
    static bool parseZoomValuePayload(const QByteArray& payload,
                                      double minimumZoom,
                                      double maximumZoom,
                                      double* zoomLevel);
    static bool parseCameraSystemStatusPayload(const QByteArray& payload,
                                               CameraSystemStatus* status);
    static bool parseFunctionFeedbackPayload(const QByteArray& payload,
                                             quint8* infoType);
    static bool parseVideoModePayload(const QByteArray& payload,
                                      VideoMode* mode);
    static bool parseCameraEncodingParametersPayload(
        const QByteArray& payload,
        CameraEncodingParameters* parameters);
    static VideoWorkMode videoWorkMode(quint8 mainStream,
                                       quint8 subStream);

private:
    static QByteArray _photoAndRecordPacket(quint8 functionType);
    static QByteArray _encode(Command command, const QByteArray& payload);
    static quint16 _crc16(const QByteArray& bytes);
    static void _appendLittleEndian16(QByteArray& bytes, quint16 value);
};
