/****************************************************************************
 *
 * UniPod MT11 UDP SDK transport.
 *
 ****************************************************************************/

#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QElapsedTimer>
#include <QtCore/QHash>
#include <QtCore/QObject>
#include <QtNetwork/QHostAddress>
#include <QtNetwork/QUdpSocket>

class Mt11Sdk : public QObject
{
    Q_OBJECT

public:
    explicit Mt11Sdk(QObject* parent = nullptr);

    void setEndpoint(const QString& host, quint16 port);
    // Configures only command 0x0f absolute targets. The protocol itself
    // limits this range to 1.0x-30.0x.
    void setZoomRange(double minimumZoom, double maximumZoom);
    // Configures validation for hybrid zoom feedback from commands 0x05,
    // 0x16 and 0x18 independently of the absolute-command range.
    void setFeedbackZoomRange(double minimumZoom, double maximumZoom);
    void clearPendingRequests();

    bool sendManualZoom(qint8 direction);
    bool sendAbsoluteZoom(double zoomLevel);
    bool requestMaximumZoom();
    bool requestCurrentZoom();
    bool requestCameraSystemStatus();
    bool takePhoto();
    bool toggleVideoRecording();
    bool requestVideoMode();
    bool setThermalMode(bool enabled);

signals:
    void manualZoomReceived(double zoomLevel);
    void absoluteZoomFeedbackReceived(bool accepted);
    void maximumZoomReceived(double zoomLevel);
    void currentZoomReceived(double zoomLevel);
    void cameraSystemStatusReceived(quint8 hdrStatus,
                                    quint8 recordingStatus,
                                    quint8 gimbalMotionMode,
                                    quint8 gimbalMountingDirection,
                                    quint8 videoOutputStatus,
                                    quint8 zoomLinkage);
    void functionFeedbackReceived(quint8 infoType);
    void videoModeReceived(quint8 mainStream, quint8 subStream);
    void packetReceived();
    void communicationError(const QString& message);

private slots:
    void _readPendingDatagrams();

private:
    bool _sendPacket(const QByteArray& packet,
                     quint8 expectedAckCommand = 0xff);
    bool _takePendingCommand(quint8 command, qint64 *deadline);
    void _dispatchAck(quint8 command, const QByteArray& payload);

    QUdpSocket _socket;
    QHostAddress _host = QHostAddress(QStringLiteral("192.168.144.24"));
    quint16 _port = 37260;
    double _minimumAbsoluteZoom = 1.0;
    double _maximumAbsoluteZoom = 30.0;
    double _minimumFeedbackZoom = 1.0;
    double _maximumFeedbackZoom = 255.9;
    QElapsedTimer _pendingClock;
    QHash<quint8, qint64> _pendingCommandDeadlines;
    // At most one remembered payload per command keeps repeated firmware
    // responses quiet without allowing unbounded diagnostic state.
    QHash<quint8, QByteArray> _lastWarnedInvalidAckPayloads;
};
