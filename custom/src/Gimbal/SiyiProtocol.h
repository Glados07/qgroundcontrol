/****************************************************************************
 *
 * 思翼私有 SDK 协议封包工具。
 * 该文件由 Python 版 SDK 的 siyi_message.py / crc16_python.py 转写而来，
 * 只保留当前云台相机缩放功能需要的命令，后续扩展姿态/拍照命令时继续在这里追加。
 *
 ****************************************************************************/

#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QtGlobal>

class SiyiProtocol
{
public:
    enum Command : quint8 {
        CommandAbsoluteZoom     = 0x0f,
        CommandCurrentZoomValue = 0x18,
    };

    struct DecodedPacket {
        bool valid = false;
        quint8 command = 0;
        quint16 sequence = 0;
        QByteArray payload;
    };

    static QByteArray absoluteZoomPacket(double zoomLevel);
    static QByteArray requestCurrentZoomPacket();
    static DecodedPacket decodePacket(const QByteArray& packet);
    static bool parseCurrentZoomPayload(const QByteArray& payload, double* zoomLevel);

private:
    static QByteArray _encode(Command command, const QByteArray& payload);
    static quint16 _crc16(const QByteArray& bytes);
    static void _appendLittleEndian16(QByteArray& bytes, quint16 value);
};
