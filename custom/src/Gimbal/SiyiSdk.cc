/****************************************************************************
 *
 * 思翼云台相机 UDP SDK 封装。
 *
 ****************************************************************************/

#include "SiyiSdk.h"

#include "QGCLoggingCategory.h"
#include "SiyiProtocol.h"

#include <QtCore/QByteArray>
#include <QtCore/QtMath>
#include <QtCore/QTimer>

namespace {

QGC_LOGGING_CATEGORY(SiyiSdkLog, "gcs.custom.gimbal.sdk")

} // namespace

SiyiSdk::SiyiSdk(QObject* parent)
    : QObject(parent)
{
    connect(&_socket, &QUdpSocket::readyRead, this, &SiyiSdk::_readPendingDatagrams);
}

void SiyiSdk::setEndpoint(const QString& host, quint16 port)
{
    QHostAddress address(host);
    if (!address.isEqual(_host) || port != _port) {
        cancelGimbalModeRequest();
    }
    if (address.isNull()) {
        _host = QHostAddress();
        _port = 0;
        emit communicationError(tr("Invalid SIYI SDK host: %1").arg(host));
        return;
    }

    _host = address;
    _port = port;
}

void SiyiSdk::setZoomRange(double minimumZoom, double maximumZoom)
{
    if (!qIsFinite(minimumZoom) || !qIsFinite(maximumZoom) || minimumZoom > maximumZoom) {
        emit communicationError(tr("Invalid SIYI zoom range: %1-%2.").arg(minimumZoom).arg(maximumZoom));
        return;
    }

    _minimumZoom = minimumZoom;
    _maximumZoom = maximumZoom;
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

bool SiyiSdk::requestMaximumZoom()
{
    return _sendPacket(SiyiProtocol::requestMaximumZoomPacket());
}

bool SiyiSdk::requestCurrentZoom()
{
    return _sendPacket(SiyiProtocol::requestCurrentZoomPacket());
}

bool SiyiSdk::requestRecordingStreamParameters()
{
    return _sendPacket(
        SiyiProtocol::requestCameraEncodingParametersPacket(
            SiyiProtocol::CameraStreamRecording));
}

bool SiyiSdk::requestCameraSystemStatus()
{
    return _sendPacket(SiyiProtocol::requestCameraSystemStatusPacket());
}

bool SiyiSdk::requestGimbalMode(quint64 requestId)
{
    cancelGimbalModeRequest();
    if (_host.isNull() || _port == 0) {
        return false;
    }
    // The SDK specifies a frame sequence, not a universally supported echoed
    // transaction id. Keep the established sequence-zero packet unchanged and
    // isolate each read-only query by its local UDP port instead. No camera
    // controls are sent on this socket, and ordinary polling cannot answer it.
    auto *socket = new QUdpSocket(this);
    const QHostAddress local(_host.protocol() == QAbstractSocket::IPv6Protocol
                                ? QHostAddress::AnyIPv6 : QHostAddress::AnyIPv4);
    if (!socket->bind(local, 0)) {
        socket->deleteLater();
        return false;
    }
    _modeSocket = socket;
    _modeRequestId = requestId;
    _modeRequestAge.start();
    connect(socket, &QUdpSocket::readyRead, this, [this, socket]() {
        if (_modeSocket != socket || !_modeRequestAge.isValid()) {
            return;
        }
        while (socket->hasPendingDatagrams()) {
            QByteArray bytes;
            bytes.resize(int(socket->pendingDatagramSize()));
            QHostAddress sender;
            quint16 port = 0;
            const qint64 size = socket->readDatagram(bytes.data(), bytes.size(), &sender, &port);
            if (size <= 0 || !sender.isEqual(_host) || port != _port || _modeRequestAge.elapsed() > 1500) {
                continue;
            }
            bytes.resize(int(size));
            QList<SiyiProtocol::DecodedPacket> packets;
            if (!SiyiProtocol::decodeDatagram(bytes, &packets)) {
                continue;
            }
            for (const auto &packet : packets) {
                SiyiProtocol::CameraSystemStatus status;
                if (SiyiProtocol::isAckPacket(packet)
                    && packet.command == SiyiProtocol::CommandCameraSystemInfo
                    && SiyiProtocol::parseCameraSystemStatusPayload(packet.payload, &status)) {
                    const quint64 id = _modeRequestId;
                    cancelGimbalModeRequest();
                    emit gimbalModeReceived(id, status.gimbalMotionMode);
                    return;
                }
            }
        }
    });
    const QByteArray packet = SiyiProtocol::requestCameraSystemStatusPacket();
    if (socket->writeDatagram(packet, _host, _port) != packet.size()) {
        cancelGimbalModeRequest();
        return false;
    }
    return true;
}

void SiyiSdk::cancelGimbalModeRequest()
{
    _modeRequestAge.invalidate();
    if (auto *socket = _modeSocket.data()) {
        _modeSocket.clear();
        socket->disconnect(this);
        // Reserve the old port beyond both the query and mode lifetimes so
        // delayed UDP replies cannot hit an immediately reused local port.
        // At the 2-second polling rate this is a small, bounded quarantine.
        connect(socket, &QUdpSocket::readyRead, socket, [socket]() {
            while (socket->hasPendingDatagrams()) {
                socket->readDatagram(nullptr, 0);
            }
        });
        QTimer::singleShot(4000, socket, &QObject::deleteLater);
    }
}

bool SiyiSdk::takePhoto()
{
    return _sendPacket(SiyiProtocol::takePhotoPacket());
}

bool SiyiSdk::toggleVideoRecording()
{
    return _sendPacket(SiyiProtocol::toggleVideoRecordingPacket());
}

bool SiyiSdk::setGimbalYawLock(bool locked)
{
    return _sendPacket(SiyiProtocol::gimbalYawLockPacket(locked));
}

void SiyiSdk::_readPendingDatagrams()
{
    while (_socket.hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(static_cast<int>(_socket.pendingDatagramSize()));
        QHostAddress sender;
        const qint64 bytesRead =
            _socket.readDatagram(datagram.data(), datagram.size(), &sender);
        if (bytesRead <= 0) {
            continue;
        }
        datagram.resize(static_cast<int>(bytesRead));

        // A dual-stack QUdpSocket can report an IPv4 sender as IPv4-mapped IPv6
        // (for example ::ffff:192.168.144.25). operator== uses strict conversion;
        // isEqual uses tolerant conversion and compares the logical IP address.
        if (!sender.isEqual(_host)) {
            continue;
        }

        QList<SiyiProtocol::DecodedPacket> decodedPackets;
        if (!SiyiProtocol::decodeDatagram(datagram, &decodedPackets)) {
            continue;
        }

        for (const SiyiProtocol::DecodedPacket& decoded : decodedPackets) {
            if (!SiyiProtocol::isAckPacket(decoded)) {
                continue;
            }

            _dispatchAck(decoded.command, decoded.payload);
        }
    }
}

void SiyiSdk::_dispatchAck(quint8 command, const QByteArray& payload)
{
    if (command == SiyiProtocol::CommandManualZoom) {
        double zoomLevel = 0.0;
        if (SiyiProtocol::parseManualZoomAckPayload(payload, &zoomLevel)
            && zoomLevel >= _minimumZoom
            && zoomLevel <= _maximumZoom) {
            emit packetReceived();
            emit manualZoomReceived(zoomLevel);
        } else {
            qCWarning(SiyiSdkLog) << "Rejected invalid SIYI manual zoom ACK payload"
                                  << payload.toHex(' ');
        }
    } else if (command == SiyiProtocol::CommandAbsoluteZoom) {
        bool accepted = false;
        if (SiyiProtocol::parseAbsoluteZoomAckPayload(payload, &accepted)) {
            emit packetReceived();
            emit absoluteZoomFeedbackReceived(accepted);
        } else {
            qCWarning(SiyiSdkLog) << "Rejected invalid SIYI absolute zoom ACK payload"
                                  << payload.toHex(' ');
        }
    } else if (command == SiyiProtocol::CommandCameraSystemInfo) {
        SiyiProtocol::CameraSystemStatus status;
        if (SiyiProtocol::parseCameraSystemStatusPayload(payload, &status)) {
            emit packetReceived();
            emit cameraSystemStatusReceived(status.hdrStatus,
                                            status.recordingStatus,
                                            status.gimbalMotionMode,
                                            status.gimbalMountingDirection,
                                            status.videoOutputStatus,
                                            status.zoomLinkage);
        }
    } else if (command == SiyiProtocol::CommandFunctionFeedback) {
        quint8 infoType = 0;
        if (SiyiProtocol::parseFunctionFeedbackPayload(payload, &infoType)) {
            emit packetReceived();
            emit functionFeedbackReceived(infoType);
        }
    } else if (command == SiyiProtocol::CommandMaximumZoomValue) {
        double zoomLevel = 0.0;
        if (SiyiProtocol::parseMaximumZoomPayload(payload,
                                                  _maximumZoom,
                                                  &zoomLevel)) {
            emit packetReceived();
            emit maximumZoomReceived(zoomLevel);
        } else {
            qCWarning(SiyiSdkLog) << "Rejected invalid SIYI maximum zoom payload"
                                  << payload.toHex(' ')
                                  << "accepted range" << 1.0 << _maximumZoom;
        }
    } else if (command == SiyiProtocol::CommandCurrentZoomValue) {
        double zoomLevel = 0.0;
        if (SiyiProtocol::parseCurrentZoomPayload(payload,
                                                  _minimumZoom,
                                                  _maximumZoom,
                                                  &zoomLevel)) {
            emit packetReceived();
            emit currentZoomReceived(zoomLevel);
        } else {
            qCWarning(SiyiSdkLog) << "Rejected invalid SIYI current zoom payload"
                                  << payload.toHex(' ')
                                  << "accepted range" << _minimumZoom << _maximumZoom;
        }
    } else if (command == SiyiProtocol::CommandCameraEncodingParameters) {
        SiyiProtocol::CameraEncodingParameters parameters;
        if (SiyiProtocol::parseCameraEncodingParametersPayload(
                payload, &parameters)) {
            emit packetReceived();
            if (parameters.streamType
                == SiyiProtocol::CameraStreamRecording) {
                emit recordingStreamParametersReceived(
                    parameters.videoEncodingType,
                    parameters.width,
                    parameters.height,
                    parameters.bitrateKbps,
                    parameters.frameRate);
            }
        } else {
            qCWarning(SiyiSdkLog)
                << "Rejected invalid SIYI camera encoding parameters payload"
                << payload.toHex(' ');
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
