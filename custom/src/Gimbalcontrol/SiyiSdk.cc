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
        const qint64 bytesRead = _socket.readDatagram(datagram.data(), datagram.size(), &sender);
        if (bytesRead <= 0) {
            continue;
        }
        datagram.resize(static_cast<int>(bytesRead));

        // 只处理当前配置相机返回的数据，避免同一网段其他思翼设备污染倍率状态。
        if (sender != _host) {
            continue;
        }

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
