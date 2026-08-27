/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#pragma once

#include <QtCore/QLoggingCategory>
#include <QtCore/QMutex>
#include <QtCore/QQueue>
#include <QtCore/QThread>
#include <QtCore/QTimer>
#include <QtCore/QWaitCondition>

#include <glib.h>
#include <gst/gstelement.h>
#include <gst/gstpad.h>

#include <atomic>

#include "VideoReceiver.h"

Q_DECLARE_LOGGING_CATEGORY(GstVideoReceiverLog)

typedef std::function<void()> Task;

/*===========================================================================*/

class GstVideoWorker : public QThread
{
    Q_OBJECT

public:
    explicit GstVideoWorker(QObject *parent = nullptr);
    ~GstVideoWorker();
    bool needDispatch() const;
    void dispatch(Task task);
    void shutdown();

private:
    void run() final;

    QWaitCondition _taskQueueUpdate;
    QMutex _taskQueueSync;
    QQueue<Task> _taskQueue;
    bool _shutdown = false;
};

/*===========================================================================*/

typedef struct _GstElement GstElement;
typedef struct _GstBin GstBin;
typedef struct _GstRTSPMessage GstRTSPMessage;

class GstVideoReceiver : public VideoReceiver
{
    Q_OBJECT

public:
    explicit GstVideoReceiver(QObject *parent = nullptr);
    ~GstVideoReceiver();

public slots:
    void start(uint32_t timeout) override;
    void stop() override;
    void startDecoding(void *sink) override;
    void stopDecoding() override;
    void startRecording(const QString &videoFile, FILE_FORMAT format) override;
    void stopRecording() override;
    void takeScreenshot(const QString &imageFile) override;

private slots:
    void _watchdog();
    void _handleEOS();

private:
    void _start(uint32_t timeout,
                const QString &startUri,
                bool lowLatencyMode,
                quint64 generation,
                const QString &explicitH265DecoderFactory);
    GstElement *_makeSource(const QString &input);
    GstElement *_makeDecoder(GstCaps *caps = nullptr, GstElement *videoSink = nullptr);
    GstElement *_makeFileSink(const QString &videoFile, FILE_FORMAT format);

    void _onNewSourcePad(GstPad *pad);
    bool _onNewDecoderPad(GstPad *pad, bool syncSinkWithParent = true);
    bool _addDecoder(GstElement *src, GstCaps *capsHint = nullptr);
    bool _ensureVideoSinkInPipeline();
    bool _addVideoSink(GstPad *pad, bool syncWithParent = true);
    void _noteTeeFrame(const QString &uri, quint64 generation, int codec);
    void _noteVideoSinkFrame(const QString &uri, quint64 generation);
    void _noteEndOfStream();
    bool _advanceRtspOptionsCompatibility(const char *reason);
    /// -Unlink the branch from the src pad
    /// -Send an EOS event at the beginning of that branch
    bool _unlinkBranch(GstElement *from);
    void _shutdownDecodingBranch();
    void _shutdownRecordingBranch();

    bool _needDispatch();
    void _dispatchSignal(Task emitter);
    QString _pipelineUri() const;
    void _setPipelineUri(const QString &uri);
    void _resetPipelineDiagnostics();
    void _setDiagnosticDecoderRoot(GstElement *decoder,
                                   const QString &uri,
                                   quint64 generation);
    void _observeExplicitDecoder(GstElement *decoder,
                                 const QString &uri,
                                 quint64 generation);
    void _installDecoderOutputProbe(GstPad *srcPad,
                                    const QString &uri,
                                    quint64 generation,
                                    const QString &plugin,
                                    const QString &factory);
    void _clearDiagnosticDecoder();
    GstElement *_snapshotDiagnosticDecoder(quint64 generation,
                                           QString &plugin,
                                           QString &factory) const;

    static gboolean _onBusMessage(GstBus *bus, GstMessage *message, gpointer user_data);
    static gboolean _onRtspBeforeSend(GstElement *source, GstRTSPMessage *message, gpointer data);
    static void _onDecoderElementAdded(GstBin *bin, GstBin *subBin, GstElement *element, gpointer data);
    static GstPadProbeReturn _onDecoderOutput(GstPad *pad, GstPadProbeInfo *info, gpointer data);
    static void _onNewPad(GstElement *element, GstPad *pad, gpointer data);
    static void _wrapWithGhostPad(GstElement *element, GstPad *pad, gpointer data);
    static void _linkPad(GstElement *element, GstPad *pad, gpointer data);
    static gboolean _padProbe(GstElement *element, GstPad *pad, gpointer user_data);
    static gboolean _filterParserCaps(GstElement *bin, GstPad *pad, GstElement *element, GstQuery *query, gpointer data);
    static GstPadProbeReturn _teeProbe(GstPad *pad, GstPadProbeInfo *info, gpointer user_data);
    static GstPadProbeReturn _videoSinkProbe(GstPad *pad, GstPadProbeInfo *info, gpointer user_data);
    static GstPadProbeReturn _eosProbe(GstPad *pad, GstPadProbeInfo *info, gpointer user_data);
    static GstPadProbeReturn _keyframeWatch(GstPad *pad, GstPadProbeInfo *info, gpointer user_data);

    GstElement *_decoder = nullptr;
    GstElement *_decoderValve = nullptr;
    GstElement *_fileSink = nullptr;
    GstElement *_pipeline = nullptr;
    GstElement *_recorderValve = nullptr;
    GstElement *_source = nullptr;
    GstElement *_tee = nullptr;
    GstElement *_videoSink = nullptr;
    GstVideoWorker *_worker = nullptr;
    gulong _teeProbeId = 0;
    gulong _videoSinkProbeId = 0;
    QString _lastRtspUri;
    // 0: standard rtspsrc headers, 1: basic headers only, 2: skip OPTIONS.
    // Kept atomic because rtspsrc's before-send callback runs outside the
    // private VideoReceiver worker thread.
    std::atomic_int _rtspOptionsCompatibility{0};
    std::atomic_int _activeRtspOptionsCompatibility{0};
    bool _stopCompletionPending = false;
    mutable QMutex _activePipelineUriMutex;
    QString _activePipelineUri;
    std::atomic<quint64> _pipelineGenerationCounter{0};
    std::atomic<quint64> _activePipelineGeneration{0};
    std::atomic_bool _rtspTeardownPending{false};
    std::atomic_int _lastRtspMethod{0};
    std::atomic_int _actualVideoCodec{VIDEO_CODEC_UNKNOWN};
    std::atomic_bool _sourceFrameReceived{false};
    std::atomic_bool _decoderFrameReceived{false};
    std::atomic_bool _sinkFrameReceived{false};
    mutable QMutex _decoderDiagnosticsMutex;
    GstElement *_diagnosticDecoderRoot = nullptr;
    QString _diagnosticDecoderPlugin;
    QString _diagnosticDecoderFactory;
    // Keep one visible warning for each distinct recoverable RTSP failure
    // during an outage. Repeated retries remain available through debug logs.
    std::atomic_int _lastReportedRtspResourceError{-1};

    static constexpr const char *_kFileMux[FILE_FORMAT_MAX + 1] = {
        "matroskamux",
        "qtmux",
        "mp4mux"
    };
};
