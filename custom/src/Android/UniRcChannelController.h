/****************************************************************************
 *
 * UniRC 10 Pro built-in SDK UART channel bridge.
 *
 ****************************************************************************/

#pragma once

#include "UniRcChannelPolicy.h"
#include "UniRcProtocol.h"

#include <QtCore/QByteArray>
#include <QtCore/QElapsedTimer>
#include <QtCore/QLoggingCategory>
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QString>
#include <QtCore/QTimer>

class GimbalCenterCoordinator;
class GimbalControlManager;
class GimbalControlSettings;

#ifdef Q_OS_ANDROID
class QSocketNotifier;
#endif

Q_DECLARE_LOGGING_CATEGORY(UniRcChannelLog)

class UniRcChannelController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool serialOpen READ serialOpen NOTIFY serialOpenChanged)
    Q_PROPERTY(bool channelInputActive READ channelInputActive NOTIFY channelInputActiveChanged)
    Q_PROPERTY(int channel9 READ channel9 NOTIFY channelsChanged)
    Q_PROPERTY(int channel10 READ channel10 NOTIFY channelsChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    explicit UniRcChannelController(GimbalControlSettings *settings,
                                    GimbalControlManager *gimbalControlManager,
                                    GimbalCenterCoordinator *gimbalCenterCoordinator,
                                    QObject *parent = nullptr);
    ~UniRcChannelController() override;

    bool serialOpen() const { return _serialOpen; }
    bool channelInputActive() const { return _channelInputActive; }
    int channel9() const { return _channel9; }
    int channel10() const { return _channel10; }
    QString lastError() const { return _lastError; }

    Q_INVOKABLE void shutdown();

signals:
    void serialOpenChanged();
    void channelInputActiveChanged();
    void channelsChanged();
    void lastErrorChanged();

private slots:
    void _settingsChanged();
    void _applicationStateChanged(Qt::ApplicationState state);
    void _reconcile();
    void _readAvailable();
    void _inputWatchdogExpired();
#ifdef Q_OS_ANDROID
    void _ownershipProbeExpired();
    void _runtimeSafetyCheck();
#endif

private:
    enum class OutputQueueResult {
        Confirmed,
        Unavailable,
        TimedOut,
        Error,
    };

    enum class ChannelRequestResult {
        Succeeded,
        WriteFailed,
        OutputQueueTimedOut,
        OutputQueueError,
    };

    bool _shouldRun() const;
    bool _openSerial();
    void _closeSerial(bool sendDisableRequest, const char *reason);
    bool _configureSerial(int fd, const QString &devicePath);
    bool _writeAll(const QByteArray &bytes, int timeoutMs);
    OutputQueueResult _waitForSerialOutputQueueEmpty(int timeoutMs,
                                                     QString *details);
    ChannelRequestResult _sendChannelRequest(quint8 frequencyCode);
    void _scheduleSerialFailure(const QString &message,
                                const char *stage,
                                const char *reason);
    void _handleChannelPacket(const UniRcProtocol::DecodedPacket &packet);
    void _resetReceiveDiagnostics();
    QString _receiveTimeoutMessage() const;
    void _applyZoomDirection(int direction, bool directionChanged);
    void _tryStartZoom(int direction);
    void _resetInput(bool normalZoomStop);
    void _setSerialOpen(bool open);
    void _setChannelInputActive(bool active);
    void _setLastError(const QString &message);

    static constexpr quint8 kChannelFrequencyCode20Hz = 5;
    static constexpr int kInitialFrameTimeoutMs = 1200;
    static constexpr int kActiveFrameTimeoutMs = 350;
    static constexpr int kReconnectDelayMs = 3000;
    static constexpr int kFailureRetryDelayMs = 10000;
    static constexpr int kBluetoothPollMs = 250;
    static constexpr int kBlockedBluetoothRetryMs = 2000;
    static constexpr int kBluetoothStableMs = 3000;
    static constexpr int kOwnershipProbeMs = 2000;
    static constexpr int kRuntimeSafetyPollMs = 500;
    static constexpr int kRepeatedFailureLogMs = 60000;
    static constexpr int kZoomStartRetryMs = 250;
    static constexpr int kWriteTimeoutMs = 100;
    static constexpr int kOutputQueueTimeoutMs = 100;
    static constexpr int kReceiveSampleMaxBytes = 64;

    QPointer<GimbalControlSettings> _settings;
    QPointer<GimbalControlManager> _gimbalControlManager;
    QPointer<GimbalCenterCoordinator> _gimbalCenterCoordinator;
    UniRcProtocol::StreamParser _parser;
    UniRcChannelPolicy _channelPolicy;
    QTimer _reconnectTimer;
    QTimer _inputWatchdog;
    QElapsedTimer _zoomStartRetryElapsed;
    QString _openedDevicePath;
    QString _lastError;
    QString _requestOutputQueueEvidence;
    QByteArray _receiveSample;
    QByteArray _lastRequestPacket;
    quint64 _receivedByteCount = 0;
    quint64 _decodedFrameCount = 0;
    quint64 _channelFrameCount = 0;
    quint64 _invalidChannelFrameCount = 0;
    quint64 _requestFrameCount = 0;
    quint64 _requestByteCount = 0;
    int _lastFramePayloadSize = -1;
    int _channel9 = 0;
    int _channel10 = 0;
    int _acceptedZoomDirection = 0;
    quint8 _lastFrameControl = 0;
    quint8 _lastFrameCommand = 0;
    bool _serialOpen = false;
    bool _channelInputActive = false;
    bool _invalidChannelWarningActive = false;
    bool _requestOutputQueueEmpty = false;
    bool _applicationActive = false;
    bool _serialFailureScheduled = false;
    bool _shuttingDown = false;

#ifdef Q_OS_ANDROID
    enum class SerialPhase {
        Idle,
        OwnershipProbe,
        AwaitingFirstFrame,
        Active,
    };

    struct SerialActivitySnapshot {
        QString termiosSignature;
        bool termiosAvailable = false;
        bool lineDisciplineAvailable = false;
        bool countersAvailable = false;
        qulonglong inputSpeed = 0;
        qulonglong outputSpeed = 0;
        int lineDiscipline = -1;
        int lineDisciplineError = 0;
        int countersError = 0;
        bool hardwareFlowControl = false;
        qint64 rxCount = 0;
        qint64 txCount = 0;
        qint64 frameCount = 0;
        qint64 overrunCount = 0;
        qint64 parityCount = 0;
        qint64 breakCount = 0;
        qint64 bufferOverrunCount = 0;
        int queuedBytes = -1;
        bool readable = false;
    };

    bool _captureSerialActivity(int fd,
                                SerialActivitySnapshot *snapshot,
                                QString *error) const;
    bool _drainOwnershipProbeInput(QByteArray *bytes,
                                   QString *error) const;
    bool _serialActivityChanged(const SerialActivitySnapshot &before,
                                const SerialActivitySnapshot &after,
                                QString *details) const;
    bool _configureAndRequestChannels();
    bool _serialLooksReleased(const SerialActivitySnapshot &snapshot,
                              QString *details) const;
    bool _serialConfigurationMatches(int fd, QString *details) const;
    bool _safeToWriteDisableRequest() const;
    void _startAttempt(const QString &devicePath,
                       const QString &bluetoothEvidence);
    void _failAttempt(const char *stage,
                      const char *reason,
                      const QString &message,
                      bool closeDescriptor);
    void _logPreflightTransition(const QString &key,
                                 const QString &message,
                                 const QString &evidence,
                                 bool warning);
    void _logAttemptFailure(const char *stage,
                            const char *reason,
                            const QString &message);

    QTimer _ownershipProbeTimer;
    QTimer _runtimeSafetyTimer;
    QElapsedTimer _bluetoothFullOffElapsed;
    QElapsedTimer _attemptElapsed;
    QElapsedTimer _requestElapsed;
    QElapsedTimer _failureLogElapsed;
    SerialActivitySnapshot _ownershipProbeStart;
    SerialPhase _serialPhase = SerialPhase::Idle;
    QString _lastBluetoothEvidence;
    QString _lastPreflightLogKey;
    QString _lastFailureLogKey;
    quint64 _attemptSequence = 0;
    quint64 _activeAttemptId = 0;
    quint64 _suppressedFailureCount = 0;
    int _nextRetryDelayMs = kReconnectDelayMs;
    bool _firstRxLogged = false;
    bool _ttyExclusiveClaimed = false;
    bool _sharedBluetoothUart = false;
    int _serialFd = -1;
    QSocketNotifier *_readNotifier = nullptr;
#endif
};
