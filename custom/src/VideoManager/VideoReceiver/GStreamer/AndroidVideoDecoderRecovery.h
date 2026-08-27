/****************************************************************************
 *
 * Android main-video first-frame recovery.
 *
 ****************************************************************************/

#pragma once

#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QString>
#include <QtCore/QTimer>

class VideoReceiver;

class AndroidVideoDecoderRecovery final : public QObject
{
public:
    static void install(VideoReceiver *receiver);

private:
    explicit AndroidVideoDecoderRecovery(VideoReceiver *receiver);

    void _handleGenerationStarted(const QString &uri, quint64 generation);
    void _handleStartComplete(int status);
    void _handleStartDecodingComplete(int status);
    void _handleSourceFrameReceived(const QString &uri,
                                    quint64 generation,
                                    int videoCodec);
    void _handleDecoderSelected(const QString &uri,
                                quint64 generation,
                                int videoCodec,
                                const QString &plugin,
                                const QString &factory);
    void _handleDecoderFrameReceived(const QString &uri,
                                     quint64 generation,
                                     const QString &plugin,
                                     const QString &factory);
    void _handleSinkFrameReceived(const QString &uri, quint64 generation);
    void _handlePipelineError(const QString &uri,
                              quint64 generation,
                              const QString &errorPlugin,
                              const QString &errorFactory,
                              bool rtspSourceError,
                              bool decoderBranchError,
                              int videoCodec,
                              const QString &decoderPlugin,
                              const QString &decoderFactory,
                              bool sourceFrameReceived,
                              bool decoderFrameReceived,
                              bool sinkFrameReceived);
    void _handleStopComplete();
    void _armFirstFrameWatchdog();
    void _restartAfterDecoderFailure(const char *reason,
                                     bool confirmedDecoderBranchFailure);
    int _firstFrameTimeoutMs() const;

    QPointer<VideoReceiver> _receiver;
    QString _activeUri;
    QString _selectedDecoderPlugin;
    QString _selectedDecoderFactory;
    QTimer _firstFrameTimer;
    quint64 _activeGeneration = 0;
    int _videoCodec = 0;
    bool _startAccepted = false;
    bool _decodingRequested = false;
    bool _streaming = false;
    bool _sourceFrameReceived = false;
    bool _adapterSelected = false;
    bool _decoderFrameReceived = false;
    bool _sinkFrameReceived = false;
    bool _stopping = false;
};
