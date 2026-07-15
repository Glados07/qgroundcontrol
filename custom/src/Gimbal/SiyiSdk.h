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
    bool sendAbsoluteZoom(double zoomLevel);
    bool requestCurrentZoom();

signals:
    void currentZoomReceived(double zoomLevel);
    void packetReceived();
    void communicationError(const QString& message);

private slots:
    void _readPendingDatagrams();

private:
    bool _sendPacket(const QByteArray& packet);

    QUdpSocket _socket;
    QHostAddress _host = QHostAddress(QStringLiteral("192.168.144.25"));
    quint16 _port = 37260;
};
