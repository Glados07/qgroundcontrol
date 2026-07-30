/****************************************************************************
 *
 * 思翼私有 SDK 协议封包工具。
 * 该文件由 Python 版 SDK 的 siyi_message.py / crc16_python.py 转写而来，
 * 只保留当前云台相机缩放、拍照和录像功能需要的命令。
 *
 ****************************************************************************/

#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QList>
#include <QtCore/QtGlobal>

class SiyiProtocol
{
public:
    enum Command : quint8 {
        CommandManualZoom       = 0x05,
        CommandCameraSystemInfo = 0x0a,
        CommandFunctionFeedback = 0x0b,
        CommandPhotoAndRecord   = 0x0c,
        CommandAbsoluteZoom     = 0x0f,
        CommandMaximumZoomValue = 0x16,
        CommandCurrentZoomValue = 0x18,
        CommandCameraEncodingParameters = 0x20,
    };

    enum CameraStreamType : quint8 {
        CameraStreamRecording = 0,
        CameraStreamMain = 1,
        CameraStreamSub = 2,
    };

    struct CameraSystemStatus {
        quint8 hdrStatus = 0;
        quint8 recordingStatus = 0;
        quint8 gimbalMotionMode = 0;
        quint8 gimbalMountingDirection = 0;
        quint8 videoOutputStatus = 0;
        // 旧版 A8 Mini 回包只有前 7 字节，0xff 表示该字段不可用。
        quint8 zoomLinkage = 0xff;
    };

    struct DecodedPacket {
        bool valid = false;
        quint8 control = 0;
        quint8 command = 0;
        quint16 sequence = 0;
        QByteArray payload;
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
    static QByteArray requestCameraSystemStatusPacket();
    static QByteArray takePhotoPacket();
    static QByteArray toggleVideoRecordingPacket();
    static QByteArray absoluteZoomPacket(double zoomLevel);
    static QByteArray requestMaximumZoomPacket();
    static QByteArray requestCurrentZoomPacket();
    static QByteArray requestCameraEncodingParametersPacket(quint8 streamType);
    static DecodedPacket decodePacket(const QByteArray& packet);
    static bool decodeDatagram(const QByteArray& datagram,
                               QList<DecodedPacket>* packets);
    static bool isAckPacket(const DecodedPacket& packet);
    static bool parseManualZoomAckPayload(const QByteArray& payload, double* zoomLevel);
    static bool parseAbsoluteZoomAckPayload(const QByteArray& payload, bool* accepted);
    static bool parseCameraSystemStatusPayload(const QByteArray& payload, CameraSystemStatus* status);
    static bool parseFunctionFeedbackPayload(const QByteArray& payload, quint8* infoType);
    static bool parseCameraEncodingParametersPayload(
        const QByteArray& payload,
        CameraEncodingParameters* parameters);
    static bool parseMaximumZoomPayload(const QByteArray& payload,
                                        double maximumSupportedZoom,
                                        double* zoomLevel,
                                        bool* usedLegacyTenthsEncoding = nullptr);
    static bool parseCurrentZoomPayload(const QByteArray& payload,
                                        double minimumZoom,
                                        double maximumZoom,
                                        double* zoomLevel,
                                        bool* usedLegacyTenthsEncoding = nullptr);

private:
    static bool _parseZoomPayload(const QByteArray& payload,
                                  double minimumZoom,
                                  double maximumZoom,
                                  double* zoomLevel,
                                  bool* usedLegacyTenthsEncoding);
    static QByteArray _photoAndRecordPacket(quint8 functionType);
    static QByteArray _encode(Command command, const QByteArray& payload);
    static quint16 _crc16(const QByteArray& bytes);
    static void _appendLittleEndian16(QByteArray& bytes, quint16 value);
};
