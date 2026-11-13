/****************************************************************************
 * QGC's source parser emits AVC. AndroidMedia may only accept Annex-B/AU.
 * Keep that conversion inside the decoder branch, leaving recordings and
 * the working H.265 path unchanged.
 ****************************************************************************/
#include "AndroidH264HardwareDecoderAdapter.h"

#include "AndroidH265HardwareDecoderAdapter.h"
#include "QGCLoggingCategory.h"

#include <QtCore/QByteArray>
#include <QtCore/QList>
#include <algorithm>

#if defined(Q_OS_ANDROID) && defined(QGC_GST_STREAMING)
#include <gst/gst.h>
#endif

QGC_LOGGING_CATEGORY(AndroidH264HardwareDecoderAdapterLog,
                     "gcs.custom.video.androidh264hardwaredecoderadapter")

namespace {
constexpr const char *kAdapterFactoryName = "qgcandroidh264hwdec";

#if defined(Q_OS_ANDROID) && defined(QGC_GST_STREAMING)
constexpr guint kAdapterRank = GST_RANK_PRIMARY + 100;
constexpr const char *kDecoderCaps =
    "video/x-h264,stream-format=(string)byte-stream,alignment=(string)au,"
    "parsed=(boolean)true,framerate=(fraction)[0/1,2147483647/1]";
// Set before the factory is registered; immutable while either receiver runs.
QByteArray s_decoderFactory;

struct Candidate {
    QByteArray name;
    guint rank;
};

QList<Candidate> hardwareCandidates()
{
    GstCaps *const caps = gst_caps_from_string(kDecoderCaps);
    GList *const factories = gst_element_factory_list_get_elements(
        static_cast<GstElementFactoryListType>(GST_ELEMENT_FACTORY_TYPE_DECODER
                                               | GST_ELEMENT_FACTORY_TYPE_MEDIA_VIDEO),
        GST_RANK_NONE);
    GList *const compatible = gst_element_factory_list_filter(factories, caps, GST_PAD_SINK, FALSE);
    gst_plugin_feature_list_free(factories);
    gst_caps_unref(caps);
    QList<Candidate> candidates;
    for (GList *node = compatible; node; node = node->next) {
        auto *const feature = GST_PLUGIN_FEATURE(node->data);
        const char *const name = gst_plugin_feature_get_name(feature);
        GstPlugin *plugin = gst_plugin_feature_get_plugin(feature);
        const bool vendor = plugin && qstrcmp(gst_plugin_get_name(plugin), "androidmedia") == 0
            && AndroidH265HardwareDecoderAdapter::isVendorHardwareDecoderFactoryName(name);
        if (vendor) candidates.append({QByteArray(name), gst_plugin_feature_get_rank(feature)});
        gst_clear_object(&plugin);
    }
    gst_plugin_feature_list_free(compatible);
    std::sort(candidates.begin(), candidates.end(), [](const Candidate &a, const Candidate &b) {
        return a.rank != b.rank ? a.rank > b.rank : a.name < b.name;
    });
    return candidates;
}
#endif
} // namespace

#if defined(Q_OS_ANDROID) && defined(QGC_GST_STREAMING)
struct GstQgcAndroidH264HardwareDecoder {
    GstBin parent;
    gboolean valid;
};
struct GstQgcAndroidH264HardwareDecoderClass { GstBinClass parent; };

G_DEFINE_TYPE(GstQgcAndroidH264HardwareDecoder, gst_qgc_android_h264_hardware_decoder, GST_TYPE_BIN)

namespace {
GstStaticPadTemplate s_sinkTemplate = GST_STATIC_PAD_TEMPLATE(
    "sink", GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS("video/x-h264,stream-format=(string){avc,avc3,byte-stream},alignment=(string)au"));
GstStaticPadTemplate s_srcTemplate = GST_STATIC_PAD_TEMPLATE(
    "src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS("video/x-raw(ANY)"));

GstStateChangeReturn changeState(GstElement *element, GstStateChange transition)
{
    auto *const self = reinterpret_cast<GstQgcAndroidH264HardwareDecoder *>(element);
    if (transition == GST_STATE_CHANGE_NULL_TO_READY && !self->valid) {
        qCCritical(AndroidH264HardwareDecoderAdapterLog) << "Refusing invalid Android H.264 adapter";
        return GST_STATE_CHANGE_FAILURE;
    }
    return GST_ELEMENT_CLASS(gst_qgc_android_h264_hardware_decoder_parent_class)->change_state(element, transition);
}

bool addGhostPad(GstElement *element, const char *name)
{
    GstPadTemplate *const padTemplate = gst_element_class_get_pad_template(GST_ELEMENT_GET_CLASS(element), name);
    GstPad *const pad = gst_ghost_pad_new_no_target_from_template(name, padTemplate);
    if (!pad) return false;
    if (gst_element_add_pad(element, pad)) return true;
    gst_object_unref(pad);
    return false;
}

bool bindGhostPad(GstElement *element, const char *name, GstElement *child)
{
    GstPad *ghost = gst_element_get_static_pad(element, name);
    GstPad *target = gst_element_get_static_pad(child, name);
    const bool bound = ghost && target && gst_ghost_pad_set_target(GST_GHOST_PAD(ghost), target);
    gst_clear_object(&ghost);
    gst_clear_object(&target);
    return bound;
}

// Build without changing state. The bin owns all children after add_many;
// READY failures are propagated and NULL disposes MediaCodec on normal teardown.
bool buildDecoderBin(GstElement *bin, const QByteArray &factory, bool externalPads)
{
    GstElement *parser = gst_element_factory_make("h264parse", "h264_annexb_normalizer");
    GstElement *filter = gst_element_factory_make("capsfilter", "h264_annexb_caps");
    GstElement *decoder = gst_element_factory_make(factory.constData(), "android_h264_hardware_decoder");
    GstElement *queue = gst_element_factory_make("queue", "latest_decoded_frame_queue");
    if (!parser || !filter || !decoder || !queue) {
        gst_clear_object(&parser);
        gst_clear_object(&filter);
        gst_clear_object(&decoder);
        gst_clear_object(&queue);
        return false;
    }
    GstCaps *const caps = gst_caps_from_string(kDecoderCaps);
    // Repeat SPS/PPS on IDR for a decoder joining a live AVC stream. Including
    // a finite framerate range also satisfies AndroidMedia's subset query.
    g_object_set(parser, "config-interval", -1, "disable-passthrough", TRUE, nullptr);
    g_object_set(filter, "caps", caps, nullptr);
    gst_caps_unref(caps);
    g_object_set(queue, "leaky", 2, "max-size-buffers", 2u, "max-size-bytes", 0u,
                 "max-size-time", static_cast<guint64>(0), "flush-on-eos", TRUE, "silent", TRUE, nullptr);
    gst_bin_add_many(GST_BIN(bin), parser, filter, decoder, queue, nullptr);
    if (!gst_element_link_many(parser, filter, decoder, queue, nullptr)) return false;
    return !externalPads || (bindGhostPad(bin, "sink", parser) && bindGhostPad(bin, "src", queue));
}

bool preflight(const QByteArray &factory)
{
    GstElement *const bin = gst_bin_new(nullptr);
    if (!bin) return false;
    const bool linked = buildDecoderBin(bin, factory, false);
    const bool ready = linked && gst_element_set_state(bin, GST_STATE_READY) != GST_STATE_CHANGE_FAILURE;
    (void) gst_element_set_state(bin, GST_STATE_NULL);
    gst_object_unref(bin);
    return ready;
}
} // namespace

static void gst_qgc_android_h264_hardware_decoder_class_init(GstQgcAndroidH264HardwareDecoderClass *klass)
{
    GstElementClass *const elementClass = GST_ELEMENT_CLASS(klass);
    gst_element_class_set_static_metadata(elementClass, "QGC Android H.264 hardware decoder adapter",
        "Codec/Decoder/Video/Hardware", "Converts AVC to Annex-B/AU for vendor MediaCodec",
        "QGroundControl custom build");
    gst_element_class_add_static_pad_template(elementClass, &s_sinkTemplate);
    gst_element_class_add_static_pad_template(elementClass, &s_srcTemplate);
    elementClass->change_state = changeState;
}

static void gst_qgc_android_h264_hardware_decoder_init(GstQgcAndroidH264HardwareDecoder *self)
{
    self->valid = FALSE;
    GstElement *const element = GST_ELEMENT(self);
    // ALWAYS pads must exist even when child creation fails. Invalid instances
    // reject NULL->READY rather than exposing a successful but empty decoder.
    if (!addGhostPad(element, "sink") || !addGhostPad(element, "src")
        || s_decoderFactory.isEmpty() || !buildDecoderBin(element, s_decoderFactory, true)) {
        qCCritical(AndroidH264HardwareDecoderAdapterLog)
            << "Failed to build Android H.264 adapter with vendor decoder" << s_decoderFactory;
        return;
    }
    self->valid = TRUE;
    qCInfo(AndroidH264HardwareDecoderAdapterLog)
        << "Android H.264 adapter instance created" << "actualDecoder" << s_decoderFactory
        << "decoderInputContract" << kDecoderCaps;
    // GstVideoReceiver observes the inner decoder's first output frame and
    // the sink independently; successful construction alone proves neither.
}
#endif

bool AndroidH264HardwareDecoderAdapter::registerElement()
{
#if defined(Q_OS_ANDROID) && defined(QGC_GST_STREAMING)
    if (!gst_is_initialized()) return false;
    GstElementFactory *const existing = gst_element_factory_find(kAdapterFactoryName);
    if (existing) {
        const bool usable = !s_decoderFactory.isEmpty();
        gst_plugin_feature_set_rank(GST_PLUGIN_FEATURE(existing), usable ? kAdapterRank : guint(GST_RANK_NONE));
        gst_object_unref(existing);
        return usable;
    }
    const auto candidates = hardwareCandidates();
    for (const auto &candidate : candidates) {
        if (!preflight(candidate.name)) {
            qCWarning(AndroidH264HardwareDecoderAdapterLog)
                << "Android H.264 Annex-B hardware candidate failed preflight" << candidate.name;
            continue;
        }
        s_decoderFactory = candidate.name;
        if (gst_element_register(nullptr, kAdapterFactoryName, kAdapterRank,
                                 gst_qgc_android_h264_hardware_decoder_get_type())) {
            qCInfo(AndroidH264HardwareDecoderAdapterLog)
                << "Registered Android H.264 AVC-to-Annex-B adapter" << kAdapterFactoryName
                << "vendorDecoder" << s_decoderFactory << "rank" << kAdapterRank;
            return true;
        }
        s_decoderFactory.clear();
        break;
    }
    qCWarning(AndroidH264HardwareDecoderAdapterLog)
        << "No vendor Android H.264 Annex-B decoder passed adapter preflight";
#endif
    return false;
}

const char *AndroidH264HardwareDecoderAdapter::elementFactoryName()
{
    return kAdapterFactoryName;
}
