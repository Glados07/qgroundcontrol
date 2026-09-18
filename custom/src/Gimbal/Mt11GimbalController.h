/****************************************************************************
 *
 * UniRC CH11/CH12 speed control for the UniPod MT11 gimbal.
 *
 ****************************************************************************/

#pragma once

#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QTimer>

class Mt11Sdk;

class Mt11GimbalController : public QObject
{
    Q_OBJECT

public:
    explicit Mt11GimbalController(Mt11Sdk* sdk, QObject* parent = nullptr);
    ~Mt11GimbalController() override;

    // Availability follows the owning manager's enabled/responding state.
    // Disable before changing the SDK endpoint: all pending stop copies are
    // flushed synchronously to the old endpoint, then both axes are disarmed.
    void setAvailable(bool available);
    void updateChannels(qint16 channel11, qint16 channel12);
    void cancel();

private:
    static int _channelSpeed(qint16 value);
    void _stopMotion();
    void _sendStopCopy();

    QPointer<Mt11Sdk> _sdk;
    QTimer _inputWatchdog;
    QTimer _stopRetryTimer;
    bool _available = false;
    bool _armed = false;
    bool _moving = false;
    int _stopCopiesRemaining = 0;
};
