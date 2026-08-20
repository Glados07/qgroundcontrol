/****************************************************************************
 *
 * UniPod MT11 UDP SDK transport.
 *
 ****************************************************************************/

#include "Mt11Sdk.h"

#include "Mt11Protocol.h"
#include "QGCLoggingCategory.h"

#include <QtCore/QtMath>

namespace {

QGC_LOGGING_CATEGORY(Mt11SdkLog, "gcs.custom.gimbal.mt11.sdk")

} // namespace

Mt11Sdk::Mt11Sdk(QObject* parent)
    : QObject(parent)
{
    _pendingClock.start();
    connect(&_socket,
            &QUdpSocket::readyRead,
            this,
            &Mt11Sdk::_readPendingDatagrams);
}

void Mt11Sdk::setEndpoint(const QString& host, quint16 port)
{
    const QHostAddress address(host);
    if (address.isNull() || port == 0) {
        const bool endpointChanged = !_host.isNull() || (_port != 0);
        _host = {};
        _port = 0;
        _pendingCommandDeadlines.clear();
        if (endpointChanged) {
            _lastWarnedInvalidAckPayloads.clear();
        }
        emit communicationError(
            tr("Invalid MT11 SDK endpoint: %1:%2").arg(host).arg(port));
        return;
    }

    // Polling and command paths both refresh the configured endpoint. Keep
    // outstanding ACK windows when the endpoint did not actually change;
    // otherwise one request in a polling batch could invalidate its peers.
    if (_host.isEqual(address) && (_port == port)) {
        return;
    }

    _host = address;
    _port = port;
    _pendingCommandDeadlines.clear();
    _lastWarnedInvalidAckPayloads.clear();
}

void Mt11Sdk::setZoomRange(double minimumZoom, double maximumZoom)
{
    if (!qIsFinite(minimumZoom) || !qIsFinite(maximumZoom)
        || minimumZoom < Mt11Protocol::MinimumZoom
        || maximumZoom > Mt11Protocol::MaximumAbsoluteZoom
        || minimumZoom > maximumZoom) {
        emit communicationError(
            tr("Invalid MT11 absolute zoom range: %1-%2.")
                .arg(minimumZoom)
                .arg(maximumZoom));
        return;
    }
    _minimumAbsoluteZoom = minimumZoom;
    _maximumAbsoluteZoom = maximumZoom;
}

void Mt11Sdk::setFeedbackZoomRange(double minimumZoom, double maximumZoom)
{
    if (!qIsFinite(minimumZoom) || !qIsFinite(maximumZoom)
        || minimumZoom < Mt11Protocol::MinimumZoom
        || maximumZoom > Mt11Protocol::MaximumFeedbackZoom
        || minimumZoom > maximumZoom) {
        emit communicationError(
            tr("Invalid MT11 feedback zoom range: %1-%2.")
                .arg(minimumZoom)
                .arg(maximumZoom));
        return;
    }
    _minimumFeedbackZoom = minimumZoom;
    _maximumFeedbackZoom = maximumZoom;
}

void Mt11Sdk::clearPendingRequests()
{
    _pendingCommandDeadlines.clear();
}

bool Mt11Sdk::sendManualZoom(qint8 direction)
{
    if (direction < -1 || direction > 1) {
        emit communicationError(
            tr("Invalid MT11 manual zoom direction: %1")
                .arg(static_cast<int>(direction)));
        return false;
    }
    return _sendPacket(Mt11Protocol::manualZoomPacket(direction),
                       Mt11Protocol::CommandManualZoom);
}

bool Mt11Sdk::sendAbsoluteZoom(double zoomLevel)
{
    if (!qIsFinite(zoomLevel)
        || zoomLevel < _minimumAbsoluteZoom
        || zoomLevel > _maximumAbsoluteZoom) {
        emit communicationError(
            tr("MT11 absolute zoom value is outside the configured range: %1")
                .arg(zoomLevel));
        return false;
    }
    return _sendPacket(Mt11Protocol::absoluteZoomPacket(zoomLevel),
                       Mt11Protocol::CommandAbsoluteZoom);
}

bool Mt11Sdk::requestMaximumZoom()
{
    return _sendPacket(Mt11Protocol::requestMaximumZoomPacket(),
                       Mt11Protocol::CommandMaximumZoomValue);
}

bool Mt11Sdk::requestCurrentZoom()
{
    return _sendPacket(Mt11Protocol::requestCurrentZoomPacket(),
                       Mt11Protocol::CommandCurrentZoomValue);
}

bool Mt11Sdk::requestCameraSystemStatus()
{
    return _sendPacket(Mt11Protocol::requestCameraSystemStatusPacket(),
                       Mt11Protocol::CommandCameraSystemInfo);
}

bool Mt11Sdk::takePhoto()
{
    return _sendPacket(Mt11Protocol::takePhotoPacket());
}

bool Mt11Sdk::toggleVideoRecording()
{
    return _sendPacket(Mt11Protocol::toggleVideoRecordingPacket());
}

bool Mt11Sdk::requestVideoMode()
{
    return _sendPacket(Mt11Protocol::requestVideoModePacket(),
                       Mt11Protocol::CommandVideoMode);
}

bool Mt11Sdk::setThermalMode(bool enabled)
{
    return _sendPacket(Mt11Protocol::setThermalModePacket(enabled),
                       Mt11Protocol::CommandSetVideoMode);
}

void Mt11Sdk::_readPendingDatagrams()
{
    while (_socket.hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(static_cast<int>(_socket.pendingDatagramSize()));
        QHostAddress sender;
        quint16 senderPort = 0;
        const qint64 bytesRead = _socket.readDatagram(
            datagram.data(), datagram.size(), &sender, &senderPort);
        if (bytesRead <= 0) {
            continue;
        }
        datagram.resize(static_cast<int>(bytesRead));

        // Accept IPv4-mapped IPv6 notation for the configured IPv4 endpoint.
        if (!sender.isEqual(_host) || senderPort != _port) {
            continue;
        }

        QList<Mt11Protocol::DecodedPacket> packets;
        if (!Mt11Protocol::decodeDatagram(datagram, &packets)) {
            qCWarning(Mt11SdkLog)
                << "Rejected invalid MT11 UDP datagram" << datagram.toHex(' ');
            continue;
        }
        for (const Mt11Protocol::DecodedPacket& packet : packets) {
            // 0x0b is explicitly asynchronous in the MT11 SDK and firmware
            // may send it as an ordinary camera-originated packet. Other
            // messages must carry ACK ctrl and match a pending request.
            if (packet.command == Mt11Protocol::CommandFunctionFeedback
                || Mt11Protocol::isAckPacket(packet)) {
                _dispatchAck(packet.command, packet.payload);
            }
        }
    }
}

void Mt11Sdk::_dispatchAck(quint8 command, const QByteArray& payload)
{
    // 0x0b is the one documented unsolicited camera notification. Every
    // ordinary ACK must correspond to a recent request sent by this transport;
    // otherwise delayed/replayed UDP frames could roll the UI state backward.
    qint64 pendingDeadline = 0;
    if (command != Mt11Protocol::CommandFunctionFeedback
        && !_takePendingCommand(command, &pendingDeadline)) {
        qCDebug(Mt11SdkLog)
            << "Ignoring unmatched or expired MT11 ACK" << Qt::hex << command;
        return;
    }

    bool parsed = false;
    if (command == Mt11Protocol::CommandManualZoom) {
        double zoom = 0.0;
        parsed = Mt11Protocol::parseManualZoomAckPayload(payload, &zoom)
            && zoom >= _minimumFeedbackZoom
            && zoom <= _maximumFeedbackZoom;
        if (parsed) {
            emit packetReceived();
            emit manualZoomReceived(zoom);
        }
    } else if (command == Mt11Protocol::CommandAbsoluteZoom) {
        bool accepted = false;
        parsed = Mt11Protocol::parseAbsoluteZoomAckPayload(payload, &accepted);
        if (parsed) {
            emit packetReceived();
            emit absoluteZoomFeedbackReceived(accepted);
        }
    } else if (command == Mt11Protocol::CommandMaximumZoomValue
               || command == Mt11Protocol::CommandCurrentZoomValue) {
        double zoom = 0.0;
        parsed = Mt11Protocol::parseZoomValuePayload(
            payload,
            _minimumFeedbackZoom,
            _maximumFeedbackZoom,
            &zoom);
        if (parsed) {
            emit packetReceived();
            if (command == Mt11Protocol::CommandMaximumZoomValue) {
                emit maximumZoomReceived(zoom);
            } else {
                emit currentZoomReceived(zoom);
            }
        }
    } else if (command == Mt11Protocol::CommandCameraSystemInfo) {
        Mt11Protocol::CameraSystemStatus status;
        parsed = Mt11Protocol::parseCameraSystemStatusPayload(payload, &status);
        if (parsed) {
            emit packetReceived();
            emit cameraSystemStatusReceived(status.hdrStatus,
                                            status.recordingStatus,
                                            status.gimbalMotionMode,
                                            status.gimbalMountingDirection,
                                            status.videoOutputStatus,
                                            status.zoomLinkage);
        }
    } else if (command == Mt11Protocol::CommandFunctionFeedback) {
        quint8 infoType = 0;
        parsed = Mt11Protocol::parseFunctionFeedbackPayload(payload, &infoType);
        if (parsed) {
            emit packetReceived();
            emit functionFeedbackReceived(infoType);
        }
    } else if (command == Mt11Protocol::CommandVideoMode
               || command == Mt11Protocol::CommandSetVideoMode) {
        Mt11Protocol::VideoMode mode;
        parsed = Mt11Protocol::parseVideoModePayload(payload, &mode);
        if (parsed) {
            emit packetReceived();
            emit videoModeReceived(mode.mainStream, mode.subStream);
        }
    } else {
        return;
    }

    if (!parsed) {
        // A malformed response must not consume or extend the bounded request
        // window. Restore only the original absolute deadline.
        if (command != Mt11Protocol::CommandFunctionFeedback) {
            if (pendingDeadline >= _pendingClock.elapsed()) {
                _pendingCommandDeadlines.insert(command, pendingDeadline);
            }
        }
        const auto warnedPayload = _lastWarnedInvalidAckPayloads.constFind(command);
        if (warnedPayload == _lastWarnedInvalidAckPayloads.cend()
            || warnedPayload.value() != payload) {
            _lastWarnedInvalidAckPayloads.insert(command, payload);
            qCWarning(Mt11SdkLog)
                << "Rejected invalid MT11 ACK" << Qt::hex << command
                << payload.toHex(' ');
        }
    }
}

bool Mt11Sdk::_sendPacket(const QByteArray& packet,
                          quint8 expectedAckCommand)
{
    if (packet.isEmpty()) {
        emit communicationError(tr("Cannot send an empty MT11 SDK packet."));
        return false;
    }
    if (_host.isNull() || _port == 0) {
        emit communicationError(tr("MT11 SDK endpoint is not configured."));
        return false;
    }

    const qint64 written = _socket.writeDatagram(packet, _host, _port);
    if (written != packet.size()) {
        emit communicationError(tr("Failed to send MT11 SDK packet."));
        return false;
    }
    if (expectedAckCommand != 0xff) {
        // The protocol has no request identifier beyond CMD_ID (production
        // SEQ is fixed at zero), so one latest bounded generation per command
        // is the strongest correlation the wire format permits.
        _pendingCommandDeadlines.insert(
            expectedAckCommand, _pendingClock.elapsed() + 1500);
    }
    return true;
}

bool Mt11Sdk::_takePendingCommand(quint8 command, qint64 *deadline)
{
    if (!deadline) {
        return false;
    }
    const auto iterator = _pendingCommandDeadlines.find(command);
    if (iterator == _pendingCommandDeadlines.end()) {
        return false;
    }
    if (iterator.value() < _pendingClock.elapsed()) {
        _pendingCommandDeadlines.erase(iterator);
        return false;
    }
    *deadline = iterator.value();
    _pendingCommandDeadlines.erase(iterator);
    return true;
}
