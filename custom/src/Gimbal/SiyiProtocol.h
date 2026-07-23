/****************************************************************************
 *
 * 思翼私有 SDK 协议封包工具。
 * 该文件由 Python 版 SDK 的 siyi_message.py / crc16_python.py 转写而来，
 * 只保留当前云台相机缩放、拍照和录像功能需要的命令。
 *
 ****************************************************************************/

#pragma once

#include <QtCore/QByteArray>
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
        CommandCurrentZoomValue = 0x18,
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
        quint8 command = 0;
        quint16 sequence = 0;
        QByteArray payload;
    };

    static QByteArray manualZoomPacket(qint8 direction);
    static QByteArray requestCameraSystemStatusPacket();
    static QByteArray takePhotoPacket();
    static QByteArray toggleVideoRecordingPacket();
    static QByteArray absoluteZoomPacket(double zoomLevel);
    static QByteArray requestCurrentZoomPacket();
    static DecodedPacket decodePacket(const QByteArray& packet);
    static bool parseManualZoomAckPayload(const QByteArray& payload, double* zoomLevel);
    static bool parseCameraSystemStatusPayload(const QByteArray& payload, CameraSystemStatus* status);
    static bool parseFunctionFeedbackPayload(const QByteArray& payload, quint8* infoType);
    static bool parseCurrentZoomPayload(const QByteArray& payload,
                                        double minimumZoom,
                                        double maximumZoom,
                                        double* zoomLevel,
                                        bool* usedLegacyTenthsEncoding = nullptr);

private:
    static QByteArray _photoAndRecordPacket(quint8 functionType);
    static QByteArray _encode(Command command, const QByteArray& payload);
    static quint16 _crc16(const QByteArray& bytes);
    static void _appendLittleEndian16(QByteArray& bytes, quint16 value);
};
