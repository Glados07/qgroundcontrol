// Host integration only: real GStreamer/parser/decodebin and production
// adapters; vendor MediaCodec is replaced by a strict Annex-B-only test bin
// backed by libav. Output frames here are NOT Android hardware evidence.
#include <QtCore/QCoreApplication>
#include <QtCore/QLoggingCategory>
#include <QtCore/QTemporaryFile>
#include "SyntheticHevc.h"
#include <gst/gst.h>
#include <atomic>
#include <iostream>
#include <stdexcept>
#include "AndroidVideoDecoderPolicy.h"
#include "AndroidH264HardwareDecoderAdapter.h"
#include "AndroidH265HardwareDecoderAdapter.h"

static std::atomic<int> avcBuffers{0}, hevcBuffers{0}, avcAdapters{0}, hevcAdapters{0};
static std::atomic<bool> inputInvalid{false}, sawSps{false}, sawPps{false}, sawIdr{false};
static bool failAvcCreate = false;
static QString hevcInputPath;
static void require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }

struct GstAuditAvc { GstBin parent; };
struct GstAuditAvcClass { GstBinClass parent; };
struct GstAuditHevc { GstBin parent; };
struct GstAuditHevcClass { GstBinClass parent; };
G_DEFINE_TYPE(GstAuditAvc, gst_audit_avc, GST_TYPE_BIN)
G_DEFINE_TYPE(GstAuditHevc, gst_audit_hevc, GST_TYPE_BIN)
static GstStaticPadTemplate avcSink = GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS("video/x-h264,stream-format=byte-stream,alignment=au,parsed=true,framerate=(fraction)[0/1,2147483647/1]"));
static GstStaticPadTemplate hevcSink = GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS("video/x-h265,stream-format=byte-stream,alignment=au,parsed=true,framerate=(fraction)[0/1,2147483647/1]"));
static GstStaticPadTemplate rawSrc = GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS("video/x-raw(ANY)"));

static GstPadProbeReturn inspectAvc(GstPad *pad, GstPadProbeInfo *info, gpointer)
{
    auto *buffer = gst_pad_probe_info_get_buffer(info);
    if (!buffer) return GST_PAD_PROBE_OK;
    ++avcBuffers;
    auto *caps = gst_pad_get_current_caps(pad);
    const auto *structure = caps ? gst_caps_get_structure(caps, 0) : nullptr;
    const char *format = structure ? gst_structure_get_string(structure, "stream-format") : nullptr;
    if (!format || g_strcmp0(format, "byte-stream")) inputInvalid = true;
    if (caps) gst_caps_unref(caps);
    GstMapInfo map{};
    if (!gst_buffer_map(buffer, &map, GST_MAP_READ)) { inputInvalid = true; return GST_PAD_PROBE_OK; }
    if (map.size < 5 || map.data[0] || map.data[1] || (map.data[2] != 1 && (map.data[2] || map.data[3] != 1))) inputInvalid = true;
    for (gsize i = 0; i + 4 < map.size; ++i) {
        if (!map.data[i] && !map.data[i + 1] && map.data[i + 2] == 1) {
            const int nal = map.data[i + 3] & 31;
            if (nal == 7) sawSps = true;
            if (nal == 8) sawPps = true;
            if (nal == 5) sawIdr = true;
        }
    }
    gst_buffer_unmap(buffer, &map);
    return GST_PAD_PROBE_OK;
}
static GstPadProbeReturn inspectHevc(GstPad *, GstPadProbeInfo *info, gpointer)
{ if (gst_pad_probe_info_get_buffer(info)) ++hevcBuffers; return GST_PAD_PROBE_OK; }

static void buildFake(GstElement *self, bool avc)
{
    GstElement *filter = gst_element_factory_make("capsfilter", nullptr);
    GstElement *decoder = gst_element_factory_make(avc ? "avdec_h264" : "avdec_h265", nullptr);
    require(filter && decoder, "libav test decoder unavailable");
    auto *caps = gst_static_pad_template_get_caps(avc ? &avcSink : &hevcSink);
    g_object_set(filter, "caps", caps, nullptr);
    gst_caps_unref(caps);
    gst_bin_add_many(GST_BIN(self), filter, decoder, nullptr);
    require(gst_element_link(filter, decoder), "fake vendor topology cannot link");
    auto *sink = gst_element_get_static_pad(filter, "sink");
    auto *src = gst_element_get_static_pad(decoder, "src");
    gst_element_add_pad(self, gst_ghost_pad_new("sink", sink));
    gst_element_add_pad(self, gst_ghost_pad_new("src", src));
    gst_pad_add_probe(sink, GST_PAD_PROBE_TYPE_BUFFER, avc ? inspectAvc : inspectHevc, nullptr, nullptr);
    gst_object_unref(sink); gst_object_unref(src);
}
static void configureFake(GstElementClass *klass, bool avc)
{
    gst_element_class_set_static_metadata(klass, "TEST ONLY strict byte-stream decoder", "Codec/Decoder/Video/Hardware",
        "libav stand-in, not MediaCodec", "QGC test");
    gst_element_class_add_static_pad_template(klass, avc ? &avcSink : &hevcSink);
    gst_element_class_add_static_pad_template(klass, &rawSrc);
}
static void gst_audit_avc_class_init(GstAuditAvcClass *klass) { configureFake(GST_ELEMENT_CLASS(klass), true); }
static void gst_audit_hevc_class_init(GstAuditHevcClass *klass) { configureFake(GST_ELEMENT_CLASS(klass), false); }
static void gst_audit_avc_init(GstAuditAvc *self) { if (!failAvcCreate) buildFake(GST_ELEMENT(self), true); }
static void gst_audit_hevc_init(GstAuditHevc *self) { buildFake(GST_ELEMENT(self), false); }
static gboolean initPlugin(GstPlugin *plugin)
{
    return gst_element_register(plugin, "amcviddec-auditavc", GST_RANK_PRIMARY, gst_audit_avc_get_type())
        && gst_element_register(plugin, "amcviddec-audithevc", GST_RANK_PRIMARY, gst_audit_hevc_get_type())
        && gst_element_register(plugin, "amcviddec-omxgoogleh264decoder", GST_RANK_PRIMARY, gst_audit_avc_get_type());
}
static void deepAdded(GstBin *, GstBin *, GstElement *element, gpointer)
{
    auto *factory = gst_element_get_factory(element);
    const char *name = factory ? gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory)) : "";
    if (!g_strcmp0(name, "qgcandroidh264hwdec")) ++avcAdapters;
    if (!g_strcmp0(name, "qgcandroidh265hwdec")) ++hevcAdapters;
}
static void handoff(GstElement *, GstBuffer *, GstPad *, gpointer pointer) { ++*static_cast<int *>(pointer); }
static GstElement *pipeline(bool avc, int *frames)
{
    const char *description = avc
        ? "videotestsrc num-buffers=12 ! video/x-raw,format=I420,width=1280,height=720,framerate=25/1 ! x264enc tune=zerolatency key-int-max=1 ! h264parse ! video/x-h264,stream-format=avc,alignment=au ! decodebin3 ! fakesink name=out sync=false signal-handoffs=true"
        : "filesrc name=source ! h265parse ! video/x-h265,stream-format=hvc1,alignment=au ! decodebin3 ! fakesink name=out sync=false signal-handoffs=true";
    GError *error = nullptr;
    auto *result = gst_parse_launch(description, &error);
    if (error) {
        std::cerr << error->message << '\n'; g_clear_error(&error);
        if (result) gst_object_unref(result);
        throw std::runtime_error("required test plugin missing or pipeline invalid");
    }
    require(result, "pipeline construction failed");
    if (!avc) {
        auto *source = gst_bin_get_by_name(GST_BIN(result), "source");
        g_object_set(source, "location", hevcInputPath.toUtf8().constData(), nullptr);
        gst_object_unref(source);
    }
    g_signal_connect(result, "deep-element-added", G_CALLBACK(deepAdded), nullptr);
    auto *sink = gst_bin_get_by_name(GST_BIN(result), "out");
    require(sink, "output sink absent");
    g_signal_connect(sink, "handoff", G_CALLBACK(handoff), frames);
    gst_object_unref(sink);
    return result;
}
static void awaitEos(GstElement *pipeline)
{
    auto *bus = gst_element_get_bus(pipeline);
    auto *message = gst_bus_timed_pop_filtered(bus, 25 * GST_SECOND,
        static_cast<GstMessageType>(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));
    gst_object_unref(bus);
    require(message, "pipeline timed out");
    if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR) {
        GError *error = nullptr; gchar *debug = nullptr; gst_message_parse_error(message, &error, &debug);
        std::cerr << error->message << " | " << (debug ? debug : "") << '\n';
        g_clear_error(&error); g_free(debug); gst_message_unref(message);
        throw std::runtime_error("pipeline failed");
    }
    gst_message_unref(message);
}
int main(int argc, char **argv)
{
    qInstallMessageHandler([](QtMsgType, const QMessageLogContext &, const QString &message) { std::cerr << message.toStdString() << '\n'; });
    QCoreApplication app(argc, argv); gst_init(&argc, &argv);
    try {
        QTemporaryFile sample;
        require(sample.open(), "cannot create synthetic H.265 input");
        const auto hevcBytes = QByteArray::fromBase64(syntheticHevcBase64);
        require(sample.write(hevcBytes) == hevcBytes.size() && sample.flush(), "cannot write synthetic H.265 input");
        hevcInputPath = sample.fileName();
        require(gst_plugin_register_static(GST_VERSION_MAJOR, GST_VERSION_MINOR, "androidmedia", "TEST vendor stand-in",
            initPlugin, "1.0", "LGPL", "audit", "audit", "https://example.invalid"), "fake plugin registration");
        auto *factory = gst_element_factory_find("amcviddec-auditavc");
        auto *avcCaps = gst_caps_from_string("video/x-h264,stream-format=avc,alignment=au");
        require(!gst_element_factory_can_sink_all_caps(factory, avcCaps), "fixture must reject native AVC");
        gst_caps_unref(avcCaps); gst_object_unref(factory);
        AndroidVideoDecoderPolicy::apply(true);
        factory = gst_element_factory_find("qgcandroidh264hwdec");
        require(factory && gst_plugin_feature_get_rank(GST_PLUGIN_FEATURE(factory)) == GST_RANK_PRIMARY + 100,
                "AVC adapter must be registered at preferred rank");
        gst_object_unref(factory);
        for (const char *name : {"avdec_h264", "openh264dec", "amcviddec-omxgoogleh264decoder"}) {
            factory = gst_element_factory_find(name);
            if (factory) { require(gst_plugin_feature_get_rank(GST_PLUGIN_FEATURE(factory)) == 0, "software rank enabled"); gst_object_unref(factory); }
        }
        std::cout << "PASS strict hardware policy keeps software disabled and registers AVC adapter\n";
        // Reproduce the field failure with the converter excluded: the only
        // vendor decoder takes Annex-B, while the source contract is AVC.
        factory = gst_element_factory_find("qgcandroidh264hwdec");
        gst_plugin_feature_set_rank(GST_PLUGIN_FEATURE(factory), GST_RANK_NONE);
        int rejectedFrames = 0;
        auto *rejected = pipeline(true, &rejectedFrames);
        (void) gst_element_set_state(rejected, GST_STATE_PLAYING);
        auto *rejectedBus = gst_element_get_bus(rejected);
        auto *rejectedMessage = gst_bus_timed_pop_filtered(rejectedBus, 25 * GST_SECOND,
            static_cast<GstMessageType>(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));
        const bool rejectedWithError = rejectedMessage && GST_MESSAGE_TYPE(rejectedMessage) == GST_MESSAGE_ERROR;
        if (rejectedMessage) gst_message_unref(rejectedMessage);
        gst_object_unref(rejectedBus);
        gst_element_set_state(rejected, GST_STATE_NULL);
        gst_object_unref(rejected);
        gst_plugin_feature_set_rank(GST_PLUGIN_FEATURE(factory), GST_RANK_PRIMARY + 100);
        gst_object_unref(factory);
        require(rejectedWithError && rejectedFrames == 0 && avcAdapters == 0,
                "AVC without adapter must reproduce missing-decoder failure");
        std::cout << "PASS AVC without adapter reproduces no-output negotiation failure\n";
        int avcFrames = 0, hevcFrames = 0;
        auto *avc = pipeline(true, &avcFrames); auto *hevc = pipeline(false, &hevcFrames);
        require(gst_element_set_state(avc, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE, "AVC start failed");
        require(gst_element_set_state(hevc, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE, "HEVC start failed");
        awaitEos(avc); awaitEos(hevc);
        gst_element_set_state(avc, GST_STATE_NULL); gst_element_set_state(hevc, GST_STATE_NULL);
        gst_object_unref(avc); gst_object_unref(hevc);
        require(avcFrames > 0 && hevcFrames > 0 && avcAdapters == 1 && hevcAdapters == 1, "both adapter paths must output");
        require(avcBuffers > 0 && hevcBuffers > 0 && !inputInvalid && sawSps && sawPps && sawIdr, "Annex-B conversion/bootstrap missing");
        std::cout << "PASS concurrent AVC/HVC1 real parse/decode output: " << avcFrames << '/' << hevcFrames << " frames\n";
        auto *first = gst_element_factory_make("qgcandroidh264hwdec", nullptr);
        auto *second = gst_element_factory_make("qgcandroidh264hwdec", nullptr);
        auto *firstDecoder = gst_bin_get_by_name(GST_BIN(first), "android_h264_hardware_decoder");
        auto *secondDecoder = gst_bin_get_by_name(GST_BIN(second), "android_h264_hardware_decoder");
        require(firstDecoder && secondDecoder && firstDecoder != secondDecoder, "decoder shared across receivers");
        gst_object_unref(firstDecoder); gst_object_unref(secondDecoder); gst_object_unref(first); gst_object_unref(second);
        std::cout << "PASS receiver instances own separate decoder objects\n";
        failAvcCreate = true;
        auto *invalid = gst_element_factory_make("qgcandroidh264hwdec", nullptr);
        auto *sink = gst_element_get_static_pad(invalid, "sink"); auto *src = gst_element_get_static_pad(invalid, "src");
        require(sink && src && gst_element_set_state(invalid, GST_STATE_READY) == GST_STATE_CHANGE_FAILURE,
                "invalid instance must retain ALWAYS pads and reject READY");
        gst_object_unref(sink); gst_object_unref(src); gst_element_set_state(invalid, GST_STATE_NULL); gst_object_unref(invalid);
        std::cout << "PASS invalid adapter rejects READY with ALWAYS pads preserved\n";
        return 0;
    } catch (const std::exception &error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
}
