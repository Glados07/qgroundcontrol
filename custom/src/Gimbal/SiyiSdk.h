/****************************************************************************
 *
 * 思翼云台相机 UDP SDK 封装。
 * 该类只负责和相机 SDK UDP 端口通信，不直接处理 QML 状态和业务按钮逻辑。
 *
 ****************************************************************************/

#pragma once

#include <QtCore/QObject>
#include <QtNetwork/QHostAddress>
#include <QtNetwork/QUdpSocket>

class SiyiSdk : public QObject
{
    Q_OBJECT

public:
    explicit SiyiSdk(QObject* parent = nullptr);

    void setEndpoint(const QString& host, quint16 port);
    void setZoomRange(double minimumZoom, double maximumZoom);
    bool sendManualZoom(qint8 direction);
    bool sendManualZoomTo(qint8 direction, const QString& host, quint16 port);
    bool sendAbsoluteZoom(double zoomLevel);
    bool requestCurrentZoom();
    bool requestCameraSystemStatus();
    bool takePhoto();
    bool toggleVideoRecording();

signals:
    void manualZoomReceived(double zoomLevel);
    void currentZoomReceived(double zoomLevel);
    void cameraSystemStatusReceived(quint8 hdrStatus,
                                    quint8 recordingStatus,
                                    quint8 gimbalMotionMode,
                                    quint8 gimbalMountingDirection,
                                    quint8 videoOutputStatus,
                                    quint8 zoomLinkage);
    void functionFeedbackReceived(quint8 infoType);
    void packetReceived();
    void communicationError(const QString& message);

private slots:
    void _readPendingDatagrams();

private:
    bool _sendPacket(const QByteArray& packet);
    bool _sendPacketTo(const QByteArray& packet, const QHostAddress& host, quint16 port);

    QUdpSocket _socket;
    QHostAddress _host = QHostAddress(QStringLiteral("192.168.144.25"));
    quint16 _port = 37260;
    double _minimumZoom = 1.0;
    double _maximumZoom = 5.5;
};
