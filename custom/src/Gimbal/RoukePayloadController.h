/****************************************************************************
 * Rouke payload commands over an independent MAVLink 2 UDP endpoint.
 ****************************************************************************/
#pragma once

#include "MAVLinkLib.h"
#include "RoukeProtocol.h"

#include <QtCore/QElapsedTimer>
#include <QtCore/QHash>
#include <QtCore/QJsonObject>
#include <QtCore/QObject>
#include <QtCore/QSet>
#include <QtCore/QTimer>
#include <QtCore/QVariantMap>
#include <QtNetwork/QHostAddress>
#include <QtNetwork/QUdpSocket>

class RoukePayloadController : public QObject
{
    Q_OBJECT
public:
    explicit RoukePayloadController(QObject *parent = nullptr);
    void setConfiguration(bool enabled, const QString &host, quint16 port,
                          quint16 localPort, int payloadAddress);
    bool available() const;
    bool responding() const { return _responding; }
    bool pending() const { return !_pendingCommand.isEmpty(); }
    QString pendingCommand() const { return _pendingCommand; }
    bool recordingStopQueued() const { return _recordingStopQueued; }
    bool recording() const { return _recording; }
    bool recordingKnown() const { return _recordingKnown; }
    // Button direction survives an unconfirmed START/STOP and link loss.
    bool recordingStopRequired() const;
    int viewType() const { return _viewType; }
    bool viewKnown() const { return _viewKnown; }
    int photoCount() const { return _photoCount; }
    int discoveredPayloadAddress() const { return _discoveredPayloadAddress; }
    QString lastError() const { return _lastError.isEmpty() ? _gimbalError : _lastError; }
    quint16 boundPort() const { return _socket.localPort(); }
    bool gimbalCenterPending() const { return _gimbalCenterPending; }
    // Only values confirmed in this endpoint session are present. Missing keys
    // must be displayed as unknown, including the default-off trigger draft.
    QVariantMap cameraParameters() const { return _cameraParameters; }
    QString pendingParameter() const;
    bool sdCardStatusKnown() const { return _sdCardStatusKnown; }
    bool sdCardInserted() const { return _sdCardInserted; }
    bool sdCardQueryPending() const { return _sdCardQueryPending; }
    QString sdCardError() const { return _sdCardError; }
    // Read-only telemetry lane; never occupies or clears camera pending/error.
    bool requestSdCardStatus();

    bool takePhoto();
    bool setRecording(bool recording);
    bool setView(int viewType);
    bool autoFocus();
    bool setCameraParameter(const QString &key, const QVariant &value);
    bool requestCameraParameter(const QString &key);
    bool centerGimbal();
    // Latest velocity setpoint; never waits behind camera or gimbal ACKs.
    // Values are protocol integers, not assumed degrees per second.
    bool sendGimbalSpeed(int yaw, int pitch);
    // C++ transport entry point for documented commands. One complete inner
    // frame may span several consecutive 53022 messages; UI actions use this too.
    bool sendCommand(const QString &command, const QJsonObject &data = {});

signals:
    void stateChanged();
    void commandFinished(const QString &command, bool success);
    void gimbalCenterFinished(bool success);
    void gimbalControlError();

private:
    void _readDatagrams();
    void _handleMessage(const mavlink_message_t &message, quint64 requestSerial, quint64 centerSerial,
                        quint64 sdCardSerial);
    void _handleFrame(const QJsonObject &object, uint16_t payloadAddress,
                      quint64 requestSerial, quint64 centerSerial, quint64 sdCardSerial);
    bool _writeCommand(const QString &command, const QJsonObject &data, QString *error);
    void _reset();
    void _timeout();
    void _gimbalCenterTimeout();
    void _sendQueuedStop();
    void _setError(const QString &error);
    bool _applyParameterAck(const QString &command, const QJsonObject &request,
                            const QJsonObject &response);
    void _invalidateParameter(const QString &command);
    void _setSdCardState(bool known, bool inserted);
    void _handleSdCardAck(const QJsonObject &object, quint64 sdCardSerial);
    QString _recordingEndpointKey() const;

    QUdpSocket _socket;
    QHostAddress _host;
    quint16 _port = 0;
    quint16 _localPort = 0;
    int _payloadAddress = -1;
    bool _enabled = false;
    bool _responding = false;
    bool _recording = false;
    bool _recordingKnown = false;
    int _viewType = 4;
    bool _viewKnown = false;
    int _photoCount = 0;
    int _discoveredPayloadAddress = -1;
    QString _lastError;
    QString _gimbalError;
    bool _gimbalCenterPending = false;
    bool _speedMayBeActive = false;
    bool _speedErrorNotified = false;
    QString _pendingCommand;
    QJsonObject _pendingData;
    QVariantMap _cameraParameters;
    bool _recordingStopQueued = false;
    struct RecordingIntent {
        bool stopRequired = false;
        bool stopRequested = false;
    };
    // In-memory obligations belong to the physical camera, not the local UDP
    // socket. Switching away must not send a stop to an unrelated payload.
    QHash<QString, RecordingIntent> _recordingIntents;
    bool _pendingStartCreatedIntent = false;
    bool _recordingStartOutcomeUncertain = false;
    // A timed-out or partially sent request can still have an ambiguous outcome.
    // Without a transaction ID, subsequent same-command ACKs cannot establish
    // physical device state.
    QSet<QString> _uncertainCommands;
    QTimer _commandTimer;
    QTimer _responseTimer;
    QTimer _gimbalCenterTimer;
    QTimer _sdCardQueryTimer;
    QTimer _sdCardFreshnessTimer;
    bool _sdCardQueryPending = false;
    bool _sdCardStatusKnown = false;
    bool _sdCardInserted = false;
    QString _sdCardError;
    // This transport owns its MAVLink sequence/parser state. It must never
    // consume a QGC vehicle channel or alter the flight link's protocol flags.
    mavlink_status_t _txStatus{};
    mavlink_status_t _rxStatus{};
    mavlink_message_t _rxMessage{};
    struct ResponseStream {
        RoukeProtocol::StreamDecoder decoder;
        QElapsedTimer lastFragment;
        quint64 requestSerial = 0;
        quint64 centerSerial = 0;
        quint64 sdCardSerial = 0;
    };
    // The peer IP/port is checked before parsing. Keep different MAVLink senders
    // and payload addresses separate even when the AI box relays them together.
    QHash<quint64, ResponseStream> _responseStreams;
    quint64 _requestSerial = 0;
    quint64 _centerSerial = 0;
    quint64 _sdCardRequestSerial = 0;
    quint64 _configurationSerial = 0;
};
