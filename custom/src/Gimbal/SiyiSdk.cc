/****************************************************************************
 *
 * 思翼云台相机 UDP SDK 封装。
 *
 ****************************************************************************/

#include "SiyiSdk.h"

#include "SiyiProtocol.h"

#include <QtCore/QByteArray>

SiyiSdk::SiyiSdk(QObject* parent)
    : QObject(parent)
{
    connect(&_socket, &QUdpSocket::readyRead, this, &SiyiSdk::_readPendingDatagrams);
}

void SiyiSdk::setEndpoint(const QString& host, quint16 port)
{
    QHostAddress address(host);
    if (address.isNull()) {
        _host = QHostAddress();
        _port = 0;
        emit communicationError(tr("Invalid SIYI SDK host: %1").arg(host));
        return;
    }

    _host = address;
    _port = port;
}

bool SiyiSdk::sendManualZoom(qint8 direction)
{
    if (direction < -1 || direction > 1) {
        emit communicationError(tr("Invalid SIYI manual zoom direction: %1").arg(static_cast<int>(direction)));
        return false;
    }

    return _sendPacket(SiyiProtocol::manualZoomPacket(direction));
}

bool SiyiSdk::sendManualZoomTo(qint8 direction, const QString& host, quint16 port)
{
    if (direction < -1 || direction > 1) {
        emit communicationError(tr("Invalid SIYI manual zoom direction: %1").arg(static_cast<int>(direction)));
        return false;
    }

    const QHostAddress address(host);
    if (address.isNull() || port == 0) {
        emit communicationError(tr("Invalid SIYI SDK endpoint: %1:%2").arg(host).arg(port));
        return false;
    }

    return _sendPacketTo(SiyiProtocol::manualZoomPacket(direction), address, port);
}

bool SiyiSdk::sendAbsoluteZoom(double zoomLevel)
{
    return _sendPacket(SiyiProtocol::absoluteZoomPacket(zoomLevel));
}

bool SiyiSdk::requestCurrentZoom()
{
    return _sendPacket(SiyiProtocol::requestCurrentZoomPacket());
}

bool SiyiSdk::requestCameraSystemStatus()
{
    return _sendPacket(SiyiProtocol::requestCameraSystemStatusPacket());
}

bool SiyiSdk::takePhoto()
{
    return _sendPacket(SiyiProtocol::takePhotoPacket());
}

bool SiyiSdk::toggleVideoRecording()
{
    return _sendPacket(SiyiProtocol::toggleVideoRecordingPacket());
}

void SiyiSdk::_readPendingDatagrams()
{
    while (_socket.hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(static_cast<int>(_socket.pendingDatagramSize()));
        QHostAddress sender;
        quint16 senderPort = 0;
        const qint64 bytesRead = _socket.readDatagram(datagram.data(), datagram.size(), &sender, &senderPort);
        if (bytesRead <= 0) {
            continue;
        }
        datagram.resize(static_cast<int>(bytesRead));

        // 只处理当前配置 endpoint 返回的数据，避免同 IP 的旧端口或其他思翼服务污染状态。
        if (sender != _host || senderPort != _port) {
            continue;
        }

        const auto decoded = SiyiProtocol::decodePacket(datagram);
        if (!decoded.valid) {
            continue;
        }

        emit packetReceived();
        if (decoded.command == SiyiProtocol::CommandManualZoom) {
            double zoomLevel = 0.0;
            if (SiyiProtocol::parseManualZoomAckPayload(decoded.payload, &zoomLevel)) {
                emit manualZoomReceived(zoomLevel);
            }
        } else if (decoded.command == SiyiProtocol::CommandCameraSystemInfo) {
            SiyiProtocol::CameraSystemStatus status;
            if (SiyiProtocol::parseCameraSystemStatusPayload(decoded.payload, &status)) {
                emit cameraSystemStatusReceived(status.hdrStatus,
                                                status.recordingStatus,
                                                status.gimbalMotionMode,
                                                status.gimbalMountingDirection,
                                                status.videoOutputStatus,
                                                status.zoomLinkage);
            }
        } else if (decoded.command == SiyiProtocol::CommandFunctionFeedback) {
            quint8 infoType = 0;
            if (SiyiProtocol::parseFunctionFeedbackPayload(decoded.payload, &infoType)) {
                emit functionFeedbackReceived(infoType);
            }
        } else if (decoded.command == SiyiProtocol::CommandCurrentZoomValue) {
            double zoomLevel = 0.0;
            if (SiyiProtocol::parseCurrentZoomPayload(decoded.payload, &zoomLevel)) {
                emit currentZoomReceived(zoomLevel);
            }
        }
    }
}

bool SiyiSdk::_sendPacket(const QByteArray& packet)
{
    return _sendPacketTo(packet, _host, _port);
}

bool SiyiSdk::_sendPacketTo(const QByteArray& packet, const QHostAddress& host, quint16 port)
{
    if (packet.isEmpty()) {
        emit communicationError(tr("Cannot send an empty SIYI SDK packet."));
        return false;
    }

    if (host.isNull() || port == 0) {
        emit communicationError(tr("SIYI SDK endpoint is not configured."));
        return false;
    }

    const qint64 written = _socket.writeDatagram(packet, host, port);
    if (written != packet.size()) {
        emit communicationError(tr("Failed to send SIYI SDK packet."));
        return false;
    }

    return true;
}
