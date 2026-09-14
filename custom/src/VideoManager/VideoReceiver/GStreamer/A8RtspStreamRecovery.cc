/****************************************************************************
 * Receiver-local A8 RTSP recovery; preserves the established decoder route.
 ****************************************************************************/

#include "A8RtspStreamRecovery.h"

#if defined(Q_OS_ANDROID) && defined(QGC_GST_STREAMING)

#include "A8RtspRecoveryPolicy.h"
#include "Fact.h"
#include "GimbalControlSettings.h"
#include "QGCLoggingCategory.h"
#include "VideoManager/VideoReceiver/VideoReceiver.h"

#include <QtCore/QPointer>
#include <QtCore/QTimer>
#include <QtCore/QUrl>
#include <QtGui/QGuiApplication>

#include <gst/gst.h>

#include <chrono>
#include <memory>
#include <mutex>
#include <vector>

QGC_LOGGING_CATEGORY(A8RtspRecoveryLog, "gcs.custom.video.a8rtsprecovery")

namespace {
constexpr const char *kObserverName = "customA8RtspStreamRecovery";

qint64 monotonicMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

// A probe never calls a QObject or stops GStreamer from its streaming thread.
// Shared ownership keeps its data alive during concurrent teardown/removal.
struct Observation {
    std::mutex mutex;
    A8RtspRecoveryPolicy::Progress progress;
    A8RtspRecoveryPolicy::SenderReport previous;
    A8RtspRecoveryPolicy::SenderReport jumpFrom;
    A8RtspRecoveryPolicy::SenderReport jumpTo;
    qint64 jumpIntervalMs = 0;
    qint64 previousArrivalMs = -1;
    quint32 ssrc = 0;
    bool active = true;
};

enum class ProbeKind { Rtp, Rtcp, Media };
struct ProbeData {
    std::shared_ptr<Observation> observation;
    ProbeKind kind;
};

void observeBuffer(ProbeData *data, GstBuffer *buffer)
{
    if (!buffer) {
        return;
    }
    const qint64 now = monotonicMs();
    const auto &state = data->observation;
    std::lock_guard<std::mutex> lock(state->mutex);
    if (!state->active) {
        return;
    }
    if (data->kind == ProbeKind::Media) {
        ++state->progress.mediaBuffers;
        state->progress.lastMediaMs = now;
        return;
    }
    GstMapInfo map = GST_MAP_INFO_INIT;
    if (!gst_buffer_map(buffer, &map, GST_MAP_READ)) {
        return;
    }
    if (data->kind == ProbeKind::Rtp) {
        if (map.size >= 12 && (map.data[0] >> 6) == 2) {
            ++state->progress.rtpPackets;
            state->progress.lastRtpMs = now;
        }
    } else {
        A8RtspRecoveryPolicy::SenderReport report;
        if (map.size <= static_cast<gsize>(G_MAXSSIZE)
            && A8RtspRecoveryPolicy::senderReport(map.data, static_cast<qsizetype>(map.size),
                                                  state->ssrc, report)) {
            if (A8RtspRecoveryPolicy::clockJump(state->previous.ntp, state->previous.rtp,
                                                state->previousArrivalMs, report.ntp, report.rtp, now)) {
                state->progress.lastClockJumpMs = now;
                state->jumpFrom = state->previous;
                state->jumpTo = report;
                state->jumpIntervalMs = now - state->previousArrivalMs;
            }
            state->previous = report;
            state->previousArrivalMs = now;
        }
    }
    gst_buffer_unmap(buffer, &map);
}

GstPadProbeReturn observePad(GstPad *, GstPadProbeInfo *info, gpointer userData)
{
    auto *data = static_cast<ProbeData *>(userData);
    if (GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_BUFFER) {
        observeBuffer(data, GST_PAD_PROBE_INFO_BUFFER(info));
    } else if (GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_BUFFER_LIST) {
        GstBufferList *list = GST_PAD_PROBE_INFO_BUFFER_LIST(info);
        for (guint i = 0; list && i < gst_buffer_list_length(list); ++i) {
            observeBuffer(data, gst_buffer_list_get(list, i));
        }
    }
    return GST_PAD_PROBE_OK;
}

GstElement *findFactory(GstBin *bin, const char *name, bool recursive = true)
{
    GstElement *result = nullptr;
    GstIterator *iterator = recursive ? gst_bin_iterate_recurse(bin) : gst_bin_iterate_elements(bin);
    GValue value = G_VALUE_INIT;
    // A changing pipeline is retried by the GUI timer; never busy-loop on RESYNC.
    while (gst_iterator_next(iterator, &value) == GST_ITERATOR_OK) {
        auto *element = GST_ELEMENT(g_value_get_object(&value));
        GstElementFactory *factory = gst_element_get_factory(element);
        if (factory && g_strcmp0(gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory)), name) == 0) {
            result = GST_ELEMENT(gst_object_ref(element));
            break;
        }
        g_value_reset(&value);
    }
    if (G_VALUE_TYPE(&value)) {
        g_value_unset(&value);
    }
    gst_iterator_free(iterator);
    return result;
}

bool videoSession(GstElement *source, QString &session, quint32 &ssrc)
{
    bool found = false;
    GstIterator *iterator = gst_element_iterate_src_pads(source);
    GValue value = G_VALUE_INIT;
    while (gst_iterator_next(iterator, &value) == GST_ITERATOR_OK) {
        auto *pad = GST_PAD(g_value_get_object(&value));
        GstCaps *caps = gst_pad_get_current_caps(pad);
        const GstStructure *structure = caps && gst_caps_get_size(caps)
            ? gst_caps_get_structure(caps, 0) : nullptr;
        int clockRate = 0;
        const char *encoding = structure ? gst_structure_get_string(structure, "encoding-name") : nullptr;
        const auto parts = QString::fromUtf8(GST_PAD_NAME(pad)).split(QLatin1Char('_'));
        if (structure && gst_structure_has_name(structure, "application/x-rtp")
            && g_strcmp0(gst_structure_get_string(structure, "media"), "video") == 0
            && gst_structure_get_int(structure, "clock-rate", &clockRate) && clockRate == 90000
            && (g_strcmp0(encoding, "H264") == 0 || g_strcmp0(encoding, "H265") == 0)
            && parts.size() == 6 && parts[0] == QStringLiteral("recv")
            && parts[1] == QStringLiteral("rtp") && parts[2] == QStringLiteral("src")) {
            bool sessionOk = false;
            bool ssrcOk = false;
            const uint index = parts[3].toUInt(&sessionOk);
            const uint sourceSsrc = parts[4].toUInt(&ssrcOk);
            if (sessionOk && ssrcOk) {
                session = QString::number(index);
                ssrc = sourceSsrc;
                found = true;
            }
        }
        gst_clear_caps(&caps);
        g_value_reset(&value);
        if (found) {
            break;
        }
    }
    if (G_VALUE_TYPE(&value)) {
        g_value_unset(&value);
    }
    gst_iterator_free(iterator);
    return found;
}

class Observer final : public QObject
{
public:
    Observer(VideoReceiver *receiver, GimbalControlSettings *settings)
        : QObject(receiver), _receiver(receiver), _settings(settings)
    {
        setObjectName(QString::fromLatin1(kObserverName));
        _timer.setInterval(250);
        connect(&_timer, &QTimer::timeout, this, [this]() { tick(); });
        connect(receiver, &VideoReceiver::videoPipelineGenerationStarted, this,
                [this](const QString &uri, quint64 generation) {
                    clear();
                    _uri = uri;
                    _generation = generation;
                    if (selected()) {
                        _policy.begin(uri, generation, monotonicMs());
                    }
                });
        connect(receiver, &VideoReceiver::sinkFrameReceived, this,
                [this](const QString &uri, quint64 generation) {
                    if (generation != _generation || uri != _uri || !selected()) {
                        return;
                    }
                    _policy.displayed(generation, monotonicMs());
                    _attachDeadlineMs = monotonicMs() + 5000;
                    _timer.start();
                });
        connect(receiver, &VideoReceiver::onStopComplete, this, [this]() { clear(); });
        connect(receiver, &VideoReceiver::onVideoPipelineError, this,
                [this](const QString &uri, quint64 generation) {
                    if (generation == _generation && uri == _uri) {
                        clear();
                    }
                });
        connect(receiver, &VideoReceiver::uriChanged, this, [this]() { clear(); });
        connect(receiver, &VideoReceiver::streamingChanged, this, [this](bool streaming) {
            if (!streaming) {
                clear();
            }
        });
        connect(receiver, &VideoReceiver::recordingChanged, this, [this](bool recording) { _recording = recording; });
        connect(receiver, &VideoReceiver::sinkChanged, this, [this](void *sink) {
            if (!sink) {
                _timer.stop();
                detachProbes();
            }
        });
        connect(settings->sdkHost(), &Fact::rawValueChanged, this, [this]() { clear(); });
        connect(settings->mt11SdkHost(), &Fact::rawValueChanged, this, [this]() { clear(); });
    }

    ~Observer() override { clear(); }

private:
    struct PadProbe { GstPad *pad; gulong id; };

    bool selected() const
    {
        return _receiver && _settings && !_receiver->isThermal()
            && _receiver->uri() == _uri
            && A8RtspRecoveryPolicy::matches(_uri,
                _settings->sdkHost()->rawValue().toString(),
                _settings->mt11SdkHost()->rawValue().toString());
    }

    void clear()
    {
        _timer.stop();
        _policy.stop();
        _generation = 0;
        detachProbes();
    }

    void detachProbes()
    {
        if (_observation) {
            std::lock_guard<std::mutex> lock(_observation->mutex);
            _observation->active = false;
        }
        for (const auto &probe : _probes) {
            gst_pad_remove_probe(probe.pad, probe.id);
            gst_object_unref(probe.pad);
        }
        _probes.clear();
        _observation.reset();
        _lastLoggedJumpMs = -1;
    }

    bool addProbe(GstPad *pad, ProbeKind kind)
    {
        if (!pad) {
            return false;
        }
        auto *data = new ProbeData{_observation, kind};
        const gulong id = gst_pad_add_probe(pad, static_cast<GstPadProbeType>(
            GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_BUFFER_LIST),
            observePad, data, [](gpointer p) { delete static_cast<ProbeData *>(p); });
        if (!id) {
            delete data;
            gst_object_unref(pad);
            return false;
        }
        _probes.push_back({pad, id}); // Takes the caller's pad reference.
        return true;
    }

    bool attach()
    {
        // Resolve from this receiver's sink, not a process-wide element hook.
        // The sink already produced a real frame in this exact generation.
        auto *sink = static_cast<GstElement *>(_receiver->sink());
        if (!sink) {
            return false;
        }
        GstObject *root = GST_OBJECT(gst_object_ref(sink));
        while (GstObject *parent = gst_object_get_parent(root)) {
            gst_object_unref(root);
            root = parent;
        }
        if (!GST_IS_PIPELINE(root)) {
            gst_object_unref(root);
            return false;
        }
        GstElement *source = findFactory(GST_BIN(root), "rtspsrc");
        // GstVideoReceiver's encoded-media tee is a direct pipeline child;
        // do not mistake a future decoder/sink's internal raw-video tee for it.
        GstElement *tee = findFactory(GST_BIN(root), "tee", false);
        GstElement *manager = source && GST_IS_BIN(source)
            ? findFactory(GST_BIN(source), "rtpbin") : nullptr;
        gchar *location = nullptr;
        if (source) {
            g_object_get(source, "location", &location, nullptr);
        }
        QString session;
        quint32 ssrc = 0;
        bool attached = source && tee && manager && QUrl(QString::fromUtf8(location)) == QUrl(_uri)
            && videoSession(source, session, ssrc);
        g_free(location);
        if (attached) {
            _observation = std::make_shared<Observation>();
            _observation->ssrc = ssrc;
            attached = addProbe(gst_element_get_static_pad(tee, "sink"), ProbeKind::Media)
                && addProbe(gst_element_get_static_pad(manager,
                    qPrintable(QStringLiteral("recv_rtp_sink_%1").arg(session))), ProbeKind::Rtp)
                && addProbe(gst_element_get_static_pad(manager,
                    qPrintable(QStringLiteral("recv_rtcp_sink_%1").arg(session))), ProbeKind::Rtcp);
        }
        gst_clear_object(&manager);
        gst_clear_object(&tee);
        gst_clear_object(&source);
        gst_object_unref(root);
        if (attached) {
            qCInfo(A8RtspRecoveryLog) << "A8 session observation attached" << "generation" << _generation
                << "host" << QUrl(_uri).host() << "rtpSession" << session << "ssrc" << ssrc;
        } else if (_observation) {
            // A partially attached observation must never make a health decision.
            detachProbes();
        }
        return attached;
    }

    void tick()
    {
        if (!selected()) {
            clear();
            return;
        }
        const qint64 now = monotonicMs();
        if (!_observation) {
            if (!attach() && now >= _attachDeadlineMs) {
                qCWarning(A8RtspRecoveryLog) << "A8 session observation unavailable; retaining native watchdog"
                    << "generation" << _generation;
                clear();
            }
            return;
        }
        A8RtspRecoveryPolicy::Progress progress;
        A8RtspRecoveryPolicy::SenderReport jumpFrom;
        A8RtspRecoveryPolicy::SenderReport jumpTo;
        qint64 jumpIntervalMs = 0;
        {
            std::lock_guard<std::mutex> lock(_observation->mutex);
            progress = _observation->progress;
            jumpFrom = _observation->jumpFrom;
            jumpTo = _observation->jumpTo;
            jumpIntervalMs = _observation->jumpIntervalMs;
        }
        if (progress.lastClockJumpMs >= 0 && progress.lastClockJumpMs != _lastLoggedJumpMs) {
            _lastLoggedJumpMs = progress.lastClockJumpMs;
            qCWarning(A8RtspRecoveryLog) << "A8 sender clock discontinuity observed (not itself a restart)"
                << "generation" << _generation << "rtpPackets" << progress.rtpPackets
                << "mediaBuffers" << progress.mediaBuffers << "ssrc" << _observation->ssrc
                << "previousNtp64" << jumpFrom.ntp << "ntp64" << jumpTo.ntp
                << "previousRtp" << jumpFrom.rtp << "rtp" << jumpTo.rtp
                << "srIntervalMs" << jumpIntervalMs;
        }
        const auto action = _policy.evaluate(progress, now,
            QGuiApplication::applicationState() == Qt::ApplicationActive, _recording);
        if (action == A8RtspRecoveryPolicy::Action::RateLimited) {
            qCWarning(A8RtspRecoveryLog) << "A8 fast recovery budget exhausted; retaining native watchdog"
                << "generation" << _generation;
        } else if (action != A8RtspRecoveryPolicy::Action::None) {
            qCWarning(A8RtspRecoveryLog) << "Recovering stalled A8 RTSP session"
                << "generation" << _generation << "clockJumpStall"
                << (action == A8RtspRecoveryPolicy::Action::ClockJumpStall)
                << "rtpIdleMs" << now - progress.lastRtpMs
                << "mediaIdleMs" << now - progress.lastMediaMs;
            clear();
            // Owner-managed stop completion performs the existing retry. Do not
            // start here, emit a synthetic completion, or advance decoder routes.
            _receiver->stop();
        }
    }

    QPointer<VideoReceiver> _receiver;
    QPointer<GimbalControlSettings> _settings;
    QString _uri;
    quint64 _generation = 0;
    bool _recording = false;
    qint64 _attachDeadlineMs = 0;
    qint64 _lastLoggedJumpMs = -1;
    QTimer _timer;
    A8RtspRecoveryPolicy _policy;
    std::shared_ptr<Observation> _observation;
    std::vector<PadProbe> _probes;
};
}
#endif

void A8RtspStreamRecovery::install(VideoReceiver *receiver, void *sink, GimbalControlSettings *settings)
{
#if defined(Q_OS_ANDROID) && defined(QGC_GST_STREAMING)
    if (receiver && sink && settings
        && !receiver->findChild<QObject *>(QString::fromLatin1(kObserverName), Qt::FindDirectChildrenOnly)) {
        new Observer(receiver, settings);
    }
#else
    (void) receiver;
    (void) sink;
    (void) settings;
#endif
}
