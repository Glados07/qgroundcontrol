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
        emit communicationError(tr("Invalid SIYI SDK host: %1").arg(host));
        return;
    }

    _host = address;
    _port = port;
}

bool SiyiSdk::sendAbsoluteZoom(double zoomLevel)
{
    return _sendPacket(SiyiProtocol::absoluteZoomPacket(zoomLevel));
}

bool SiyiSdk::requestCurrentZoom()
{
    return _sendPacket(SiyiProtocol::requestCurrentZoomPacket());
}

void SiyiSdk::_readPendingDatagrams()
{
    while (_socket.hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(static_cast<int>(_socket.pendingDatagramSize()));
        QHostAddress sender;
        quint16 senderPort = 0;
        _socket.readDatagram(datagram.data(), datagram.size(), &sender, &senderPort);

        const auto decoded = SiyiProtocol::decodePacket(datagram);
        if (!decoded.valid) {
            continue;
        }

        emit packetReceived();
        if (decoded.command == SiyiProtocol::CommandCurrentZoomValue) {
            double zoomLevel = 0.0;
            if (SiyiProtocol::parseCurrentZoomPayload(decoded.payload, &zoomLevel)) {
                emit currentZoomReceived(zoomLevel);
            }
        }
    }
}

bool SiyiSdk::_sendPacket(const QByteArray& packet)
{
    if (_host.isNull() || _port == 0) {
        emit communicationError(tr("SIYI SDK endpoint is not configured."));
        return false;
    }

    const qint64 written = _socket.writeDatagram(packet, _host, _port);
    if (written != packet.size()) {
        emit communicationError(tr("Failed to send SIYI SDK packet."));
        return false;
    }

    return true;
}
