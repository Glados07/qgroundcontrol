/****************************************************************************
 * UniRC CH11/CH12 velocity control and CH14 centering for the Rouke payload.
 ****************************************************************************/
#pragma once

#include <QtCore/QElapsedTimer>
#include <QtCore/QObject>
#include <QtCore/QTimer>

class RoukePayloadController;

class RoukeGimbalController : public QObject
{
    Q_OBJECT
public:
    explicit RoukeGimbalController(RoukePayloadController *controller, QObject *parent = nullptr);
    ~RoukeGimbalController() override;

    // Speeds are protocol integers: the vendor document does not specify units.
    void setConfiguration(int yawLimit, int pitchLimit, bool yawReversed, bool pitchReversed);
    void updateChannels(qint16 channel11, qint16 channel12, qint16 channel14);
    void cancel();
    bool centerGimbal();

private:
    static bool _reasonable(qint16 value);
    static bool _neutral(qint16 value);
    static int _speed(qint16 value, int limit, bool reversed);
    void _tick();
    void _stop(bool repeat);

    RoukePayloadController *_controller = nullptr;
    QTimer _sendTimer;
    QTimer _inputWatchdog;
    QElapsedTimer _inputAge;
    int _yawLimit = 10;
    int _pitchLimit = 5;
    int _yawSpeed = 0;
    int _pitchSpeed = 0;
    int _stopRepeats = 0;
    bool _yawReversed = false;
    bool _pitchReversed = false;
    bool _axesArmed = false;
    bool _buttonArmed = false;
    bool _buttonPressed = false;
    bool _motionSent = false;
};
