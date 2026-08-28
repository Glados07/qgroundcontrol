/****************************************************************************
 *
 * Android H.265 MediaCodec adapter for hvc1 or native Annex-B/AU input.
 *
 ****************************************************************************/

#include "AndroidH265HardwareDecoderAdapter.h"

#include "QGCLoggingCategory.h"

#include <QtCore/QByteArray>
#include <QtCore/QList>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QtGlobal>

#include <algorithm>
#include <utility>

#if defined(Q_OS_ANDROID) && defined(QGC_GST_STREAMING)
#include <gst/gst.h>
#endif

QGC_LOGGING_CATEGORY(AndroidH265HardwareDecoderAdapterLog,
                     "gcs.custom.video.androidh265hardwaredecoderadapter")

namespace {

constexpr const char *kAdapterFactoryName = "qgcandroidh265hwdec";

#if defined(Q_OS_ANDROID) && defined(QGC_GST_STREAMING)

// Restore the A8 Mini compatibility path as the unambiguous Android H.265
// preference. The wide gap keeps vendor factories with unusually high native
// ranks from bypassing hvc1 -> Annex-B/AU normalization.
constexpr guint kAdapterRank = GST_RANK_PRIMARY + 100;
constexpr const char *kByteStreamCaps =
    "video/x-h265,stream-format=(string)byte-stream,alignment=(string)au";

QByteArray s_hardwareDecoderFactoryName;
QStringList s_registeredElementFactoryNames;

struct AdapterRouteConfig {
    QByteArray elementFactoryName;
    QByteArray typeName;
    QByteArray hardwareDecoderFactoryName;
};

// GType class_data must remain valid for the process lifetime. Static
// GStreamer element registrations have the same lifetime, so these route
// records are intentionally retained after startup.
QList<AdapterRouteConfig *> s_alternativeAdapterRoutes;

struct HardwareDecoderCandidate {
    QByteArray factoryName;
    QString displayName;
    guint rank = GST_RANK_NONE;
    bool lowLatencyVariant = false;
};

bool preflightHardwareDecoder(const QByteArray &factoryName);

bool factoryCanSinkCaps(GstElementFactory *factory, const GstCaps *caps)
{
    if (!factory || !caps) {
        return false;
    }

    const GList *const padTemplates =
        gst_element_factory_get_static_pad_templates(factory);
    for (const GList *node = padTemplates; node; node = node->next) {
        auto *const padTemplate =
            static_cast<GstStaticPadTemplate *>(node->data);
        if (!padTemplate || padTemplate->direction != GST_PAD_SINK) {
            continue;
        }

        GstCaps *const templateCaps =
            gst_static_caps_get(&padTemplate->static_caps);
        const bool compatible = templateCaps &&
                                gst_caps_can_intersect(templateCaps, caps);
        if (templateCaps) {
            gst_caps_unref(templateCaps);
        }
        if (compatible) {
            return true;
        }
    }

    return false;
}

bool isLowLatencyDecoderVariant(const QString &factoryName)
{
    const QString normalized = factoryName.toLower();
    return normalized.contains(QStringLiteral("lowlatency")) ||
           normalized.contains(QStringLiteral("low_latency")) ||
           normalized.contains(QStringLiteral("low-latency"));
}

bool isVendorMediaCodecDecoder(GstElementFactory *factory)
{
    if (!factory) {
        return false;
    }

    GstPluginFeature *const feature = GST_PLUGIN_FEATURE(factory);
    GstPlugin *const plugin = gst_plugin_feature_get_plugin(feature);
    const bool isAndroidMedia = plugin
        && qstrcmp(gst_plugin_get_name(plugin), "androidmedia") == 0;
    if (plugin) {
        gst_object_unref(plugin);
    }
    if (!isAndroidMedia) {
        return false;
    }

    const gchar *const factoryName = gst_plugin_feature_get_name(feature);
    return AndroidH265HardwareDecoderAdapter::isVendorHardwareDecoderFactoryName(factoryName);
}

QString pluginAndFactoryName(GstElementFactory *factory)
{
    if (!factory) {
        return QStringLiteral("unknown");
    }

    const QString factoryName =
        QString::fromUtf8(gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory)));
    GstPlugin *const plugin = gst_plugin_feature_get_plugin(GST_PLUGIN_FEATURE(factory));
    if (!plugin) {
        return QStringLiteral("unknown/") + factoryName;
    }

    const QString result = QString::fromUtf8(gst_plugin_get_name(plugin)) + QLatin1Char('/') + factoryName;
    gst_object_unref(plugin);
    return result;
}

QList<HardwareDecoderCandidate> findByteStreamHardwareDecoderCandidates()
{
    GstCaps *const byteStreamCaps = gst_caps_from_string(kByteStreamCaps);
    if (!byteStreamCaps) {
        return {};
    }

    GList *const decoderFactories = gst_element_factory_list_get_elements(
        static_cast<GstElementFactoryListType>(GST_ELEMENT_FACTORY_TYPE_DECODER |
                                               GST_ELEMENT_FACTORY_TYPE_MEDIA_VIDEO),
        GST_RANK_NONE);

    QList<HardwareDecoderCandidate> candidates;
    for (GList *node = decoderFactories; node; node = node->next) {
        GstElementFactory *const factory = GST_ELEMENT_FACTORY(node->data);
        if (!isVendorMediaCodecDecoder(factory) ||
            !factoryCanSinkCaps(factory, byteStreamCaps)) {
            continue;
        }

        const QByteArray factoryName(
            gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory)));
        candidates.append(HardwareDecoderCandidate{
            factoryName,
            pluginAndFactoryName(factory),
            gst_plugin_feature_get_rank(GST_PLUGIN_FEATURE(factory)),
            isLowLatencyDecoderVariant(QString::fromUtf8(factoryName)),
        });
    }

    gst_plugin_feature_list_free(decoderFactories);
    gst_caps_unref(byteStreamCaps);

    std::sort(candidates.begin(), candidates.end(), [](const auto &left, const auto &right) {
        // Some vendors expose a dedicated low-latency MediaCodec component.
        // Prefer it even when Android assigned the generic component the same
        // or a slightly higher rank; this option is specifically for live FPV.
        if (left.lowLatencyVariant != right.lowLatencyVariant) {
            return left.lowLatencyVariant;
        }
        if (left.rank != right.rank) {
            return left.rank > right.rank;
        }
        return left.factoryName < right.factoryName;
    });

    return candidates;
}

bool preflightHardwareDecoder(const QByteArray &factoryName)
{
    GstElement *bin = gst_bin_new(nullptr);
    GstElement *parser = gst_element_factory_make("h265parse", nullptr);
    GstElement *capsFilter = gst_element_factory_make("capsfilter", nullptr);
    GstElement *decoder = gst_element_factory_make(factoryName.constData(), nullptr);
    GstElement *outputQueue = gst_element_factory_make("queue", nullptr);

    if (!bin || !parser || !capsFilter || !decoder || !outputQueue) {
        qCWarning(AndroidH265HardwareDecoderAdapterLog)
            << "Unable to construct the Android H.265 adapter preflight pipeline for"
            << factoryName;
        gst_clear_object(&bin);
        gst_clear_object(&parser);
        gst_clear_object(&capsFilter);
        gst_clear_object(&decoder);
        gst_clear_object(&outputQueue);
        return false;
    }

    GstCaps *const caps = gst_caps_from_string(kByteStreamCaps);
    if (!caps) {
        gst_object_unref(bin);
        gst_object_unref(parser);
        gst_object_unref(capsFilter);
        gst_object_unref(decoder);
        gst_object_unref(outputQueue);
        return false;
    }

    g_object_set(capsFilter, "caps", caps, nullptr);
    gst_caps_unref(caps);

    gst_bin_add_many(GST_BIN(bin), parser, capsFilter, decoder, outputQueue, nullptr);
    const bool linked = gst_element_link_many(parser, capsFilter, decoder, outputQueue, nullptr);
    if (!linked) {
        qCWarning(AndroidH265HardwareDecoderAdapterLog)
            << "The Android H.265 adapter preflight pipeline could not link to"
            << factoryName;
    }

    bool opened = false;
    if (linked) {
        const GstStateChangeReturn stateChange =
            gst_element_set_state(bin, GST_STATE_READY);
        opened = stateChange != GST_STATE_CHANGE_FAILURE;
        if (!opened) {
            qCWarning(AndroidH265HardwareDecoderAdapterLog)
                << "The Android H.265 hardware candidate could not enter READY"
                << factoryName;
        }
        (void) gst_element_set_state(bin, GST_STATE_NULL);
    }

    gst_object_unref(bin);
    return linked && opened;
}

#endif

} // namespace

#if defined(Q_OS_ANDROID) && defined(QGC_GST_STREAMING)

typedef struct _GstQgcAndroidH265HardwareDecoder GstQgcAndroidH265HardwareDecoder;
typedef struct _GstQgcAndroidH265HardwareDecoderClass GstQgcAndroidH265HardwareDecoderClass;

struct _GstQgcAndroidH265HardwareDecoder {
    GstBin parent;
    GstElement *parser;
    GstElement *capsFilter;
    GstElement *decoder;
    GstElement *outputQueue;
    GstPad *sinkGhostPad;
    GstPad *srcGhostPad;
    gboolean valid;
    gboolean inputCapsLogged;
    gboolean outputFrameLogged;
};

struct _GstQgcAndroidH265HardwareDecoderClass {
    GstBinClass parentClass;
    const AdapterRouteConfig *routeConfig;
};

#define GST_TYPE_QGC_ANDROID_H265_HARDWARE_DECODER \
    (gst_qgc_android_h265_hardware_decoder_get_type())

G_DEFINE_TYPE(GstQgcAndroidH265HardwareDecoder,
              gst_qgc_android_h265_hardware_decoder,
              GST_TYPE_BIN)

namespace {

GstStaticPadTemplate s_sinkPadTemplate = GST_STATIC_PAD_TEMPLATE(
    "sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS(
        "video/x-h265,stream-format=(string)hvc1; "
        "video/x-h265,stream-format=(string)byte-stream,alignment=(string)au"));

GstStaticPadTemplate s_srcPadTemplate = GST_STATIC_PAD_TEMPLATE(
    "src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS("video/x-raw(ANY)"));

GstPadProbeReturn logDecoderInputCaps(GstPad *, GstPadProbeInfo *info, gpointer userData)
{
    auto *const self = static_cast<GstQgcAndroidH265HardwareDecoder *>(userData);
    if (!self || self->inputCapsLogged || !(GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM)) {
        return GST_PAD_PROBE_OK;
    }

    GstEvent *const event = gst_pad_probe_info_get_event(info);
    if (!event || GST_EVENT_TYPE(event) != GST_EVENT_CAPS) {
        return GST_PAD_PROBE_OK;
    }

    GstCaps *caps = nullptr;
    gst_event_parse_caps(event, &caps);
    gchar *const capsText = caps ? gst_caps_to_string(caps) : nullptr;

    GstElementFactory *const factory = self->decoder ? gst_element_get_factory(self->decoder) : nullptr;
    qCInfo(AndroidH265HardwareDecoderAdapterLog)
        << "Android H.265 adapter selected actual decoder"
        << pluginAndFactoryName(factory)
        << "vendor MediaCodec candidate"
        << "negotiated sink caps" << (capsText ? capsText : "unknown");

    g_free(capsText);
    self->inputCapsLogged = TRUE;
    return GST_PAD_PROBE_OK;
}

GstPadProbeReturn logDecoderFirstOutputFrame(GstPad *pad, GstPadProbeInfo *info, gpointer userData)
{
    auto *const self = static_cast<GstQgcAndroidH265HardwareDecoder *>(userData);
    if (!self || self->outputFrameLogged || !(GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_BUFFER)) {
        return GST_PAD_PROBE_OK;
    }

    GstBuffer *const buffer = gst_pad_probe_info_get_buffer(info);
    if (!buffer) {
        return GST_PAD_PROBE_OK;
    }

    GstCaps *const caps = gst_pad_get_current_caps(pad);
    gchar *const capsText = caps ? gst_caps_to_string(caps) : nullptr;
    bool glMemoryOutput = false;
    if (caps && gst_caps_get_size(caps) > 0) {
        const GstCapsFeatures *const features = gst_caps_get_features(caps, 0);
        glMemoryOutput = features &&
                         gst_caps_features_contains(features, "memory:GLMemory");
    }
    GstElementFactory *const factory = self->decoder ? gst_element_get_factory(self->decoder) : nullptr;
    qCInfo(AndroidH265HardwareDecoderAdapterLog)
        << "Android H.265 decoder produced its first raw frame"
        << pluginAndFactoryName(factory)
        << "vendor MediaCodec candidate"
        << "glMemoryOutput" << glMemoryOutput
        << "bytes" << gst_buffer_get_size(buffer)
        << "pts" << static_cast<qulonglong>(GST_BUFFER_PTS(buffer))
        << "src caps" << (capsText ? capsText : "unknown");

    g_free(capsText);
    if (caps) {
        gst_caps_unref(caps);
    }
    self->outputFrameLogged = TRUE;
    return GST_PAD_PROBE_REMOVE;
}

bool linkDecoder(GstQgcAndroidH265HardwareDecoder *self, GstElement *decoder)
{
    // This function consumes the caller's reference on both success and
    // failure. On success the bin owns the element.
    if (!gst_bin_add(GST_BIN(self), decoder)) {
        gst_object_unref(decoder);
        return false;
    }

    if (!gst_element_link(self->capsFilter, decoder) ||
        !gst_element_link(decoder, self->outputQueue)) {
        gst_element_unlink(self->capsFilter, decoder);
        gst_element_unlink(decoder, self->outputQueue);
        gst_bin_remove(GST_BIN(self), decoder);
        return false;
    }

    self->decoder = decoder;
    GstPad *const decoderSinkPad = gst_element_get_static_pad(decoder, "sink");
    if (decoderSinkPad) {
        gst_pad_add_probe(decoderSinkPad,
                          GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
                          logDecoderInputCaps,
                          self,
                          nullptr);
        gst_object_unref(decoderSinkPad);
    }

    GstPad *const decoderSrcPad = gst_element_get_static_pad(decoder, "src");
    if (decoderSrcPad) {
        gst_pad_add_probe(decoderSrcPad,
                          GST_PAD_PROBE_TYPE_BUFFER,
                          logDecoderFirstOutputFrame,
                          self,
                          nullptr);
        gst_object_unref(decoderSrcPad);
    }
    return true;
}

bool addUnboundGhostPads(GstQgcAndroidH265HardwareDecoder *self)
{
    GstPadTemplate *const sinkTemplate =
        gst_element_class_get_pad_template(GST_ELEMENT_GET_CLASS(self), "sink");
    GstPadTemplate *const srcTemplate =
        gst_element_class_get_pad_template(GST_ELEMENT_GET_CLASS(self), "src");
    if (!sinkTemplate || !srcTemplate) {
        return false;
    }

    self->sinkGhostPad =
        gst_ghost_pad_new_no_target_from_template("sink", sinkTemplate);
    self->srcGhostPad =
        gst_ghost_pad_new_no_target_from_template("src", srcTemplate);
    if (!self->sinkGhostPad || !self->srcGhostPad) {
        gst_clear_object(&self->sinkGhostPad);
        gst_clear_object(&self->srcGhostPad);
        return false;
    }

    if (!gst_element_add_pad(GST_ELEMENT(self), self->sinkGhostPad)) {
        gst_clear_object(&self->sinkGhostPad);
        gst_clear_object(&self->srcGhostPad);
        return false;
    }
    if (!gst_element_add_pad(GST_ELEMENT(self), self->srcGhostPad)) {
        gst_element_remove_pad(GST_ELEMENT(self), self->sinkGhostPad);
        self->sinkGhostPad = nullptr;
        gst_clear_object(&self->srcGhostPad);
        return false;
    }

    return true;
}

bool bindGhostPadTargets(GstQgcAndroidH265HardwareDecoder *self)
{
    if (!self->sinkGhostPad || !self->srcGhostPad
        || !self->parser || !self->outputQueue) {
        return false;
    }

    GstPad *parserSinkPad =
        gst_element_get_static_pad(self->parser, "sink");
    GstPad *queueSrcPad =
        gst_element_get_static_pad(self->outputQueue, "src");
    if (!parserSinkPad || !queueSrcPad) {
        gst_clear_object(&parserSinkPad);
        gst_clear_object(&queueSrcPad);
        return false;
    }

    const bool sinkTargetSet = gst_ghost_pad_set_target(
        GST_GHOST_PAD(self->sinkGhostPad), parserSinkPad);
    const bool srcTargetSet = gst_ghost_pad_set_target(
        GST_GHOST_PAD(self->srcGhostPad), queueSrcPad);
    gst_object_unref(parserSinkPad);
    gst_object_unref(queueSrcPad);

    if (!sinkTargetSet || !srcTargetSet) {
        (void) gst_ghost_pad_set_target(
            GST_GHOST_PAD(self->sinkGhostPad), nullptr);
        (void) gst_ghost_pad_set_target(
            GST_GHOST_PAD(self->srcGhostPad), nullptr);
        return false;
    }

    return true;
}

GstStateChangeReturn adapterChangeState(GstElement *element,
                                        GstStateChange transition)
{
    auto *const self =
        reinterpret_cast<GstQgcAndroidH265HardwareDecoder *>(element);
    if (transition == GST_STATE_CHANGE_NULL_TO_READY && !self->valid) {
        qCCritical(AndroidH265HardwareDecoderAdapterLog)
            << "Refusing to start an invalid Android H.265 adapter instance"
            << GST_ELEMENT_NAME(element);
        return GST_STATE_CHANGE_FAILURE;
    }

    auto *const parentClass = GST_ELEMENT_CLASS(
        g_type_class_peek_parent(GST_ELEMENT_GET_CLASS(element)));
    if (!parentClass || !parentClass->change_state) {
        qCCritical(AndroidH265HardwareDecoderAdapterLog)
            << "Android H.265 adapter has no parent state-change handler";
        return GST_STATE_CHANGE_FAILURE;
    }
    return parentClass->change_state(element, transition);
}

} // namespace

namespace {

void configureAdapterClass(GstQgcAndroidH265HardwareDecoderClass *klass,
                           const AdapterRouteConfig *routeConfig)
{
    klass->routeConfig = routeConfig;
    GstElementClass *const elementClass = GST_ELEMENT_CLASS(klass);
    gst_element_class_set_static_metadata(
        elementClass,
        "QGC Android H.265 hardware decoder adapter",
        "Codec/Decoder/Video/Hardware",
        "Normalizes H.265 to Annex-B access units for Android vendor MediaCodec",
        "QGroundControl custom build");
    gst_element_class_add_static_pad_template(elementClass, &s_sinkPadTemplate);
    gst_element_class_add_static_pad_template(elementClass, &s_srcPadTemplate);
    elementClass->change_state = adapterChangeState;
}

void initializeAdapterInstance(
    GstQgcAndroidH265HardwareDecoder *self,
    const QByteArray &adapterFactoryName,
    const QByteArray &hardwareDecoderFactoryName)
{
    self->parser = nullptr;
    self->capsFilter = nullptr;
    self->outputQueue = nullptr;
    self->decoder = nullptr;
    self->sinkGhostPad = nullptr;
    self->srcGhostPad = nullptr;
    self->valid = FALSE;
    self->inputCapsLogged = FALSE;
    self->outputFrameLogged = FALSE;

    // GstPadPresence is ALWAYS for both external pads. GObject instance
    // construction cannot report failure, so publish unbound ghost pads before
    // any fallible child construction and reject invalid instances at READY.
    if (!addUnboundGhostPads(self)) {
        qCCritical(AndroidH265HardwareDecoderAdapterLog)
            << "Unable to create required Android H.265 adapter ghost pads"
            << adapterFactoryName;
        return;
    }

    if (hardwareDecoderFactoryName.isEmpty()) {
        qCCritical(AndroidH265HardwareDecoderAdapterLog)
            << "Android H.265 adapter has no bound vendor decoder factory"
            << adapterFactoryName;
        return;
    }

    self->parser = gst_element_factory_make("h265parse", "h265_annexb_normalizer");
    self->capsFilter = gst_element_factory_make("capsfilter", "h265_annexb_caps");
    self->outputQueue = gst_element_factory_make("queue", "latest_decoded_frame_queue");

    if (!self->parser || !self->capsFilter || !self->outputQueue) {
        qCCritical(AndroidH265HardwareDecoderAdapterLog)
            << "Unable to construct required Android H.265 adapter elements"
            << "adapterFactory" << adapterFactoryName
            << "decoderFactory" << hardwareDecoderFactoryName;
        gst_clear_object(&self->parser);
        gst_clear_object(&self->capsFilter);
        gst_clear_object(&self->outputQueue);
        return;
    }

    GstCaps *const byteStreamCaps = gst_caps_from_string(kByteStreamCaps);
    if (!byteStreamCaps) {
        qCCritical(AndroidH265HardwareDecoderAdapterLog)
            << "Unable to create byte-stream/au caps for the Android H.265 adapter"
            << adapterFactoryName;
        gst_clear_object(&self->parser);
        gst_clear_object(&self->capsFilter);
        gst_clear_object(&self->outputQueue);
        return;
    }
    g_object_set(self->parser,
                 "config-interval", -1,
                 nullptr);
    g_object_set(self->capsFilter,
                 "caps", byteStreamCaps,
                 nullptr);
    gst_caps_unref(byteStreamCaps);

    // Decouple the decoder from a slow GL sink and keep only the newest raw
    // frames. This prevents presentation back-pressure from growing latency.
    g_object_set(self->outputQueue,
                 "leaky", 2,
                 "max-size-buffers", 2u,
                 "max-size-bytes", 0u,
                 "max-size-time", static_cast<guint64>(0),
                 "flush-on-eos", TRUE,
                 "silent", TRUE,
                 nullptr);

    gst_bin_add_many(GST_BIN(self), self->parser, self->capsFilter, self->outputQueue, nullptr);
    if (!gst_element_link(self->parser, self->capsFilter)) {
        qCCritical(AndroidH265HardwareDecoderAdapterLog)
            << "Unable to link h265parse to the Annex-B caps filter"
            << adapterFactoryName;
        return;
    }

    GstElement *hardwareDecoder = gst_element_factory_make(
        hardwareDecoderFactoryName.constData(), "android_h265_hardware_decoder");
    if (!hardwareDecoder) {
        qCCritical(AndroidH265HardwareDecoderAdapterLog)
            << "Unable to instantiate vendor decoder"
            << hardwareDecoderFactoryName
            << "for adapter" << adapterFactoryName;
        return;
    }
    if (!linkDecoder(self, hardwareDecoder)) {
        qCCritical(AndroidH265HardwareDecoderAdapterLog)
            << "Unable to link vendor decoder"
            << hardwareDecoderFactoryName
            << "for adapter" << adapterFactoryName;
        return;
    }

    // GstVideoReceiver's explicit route enumerates the static src pad before
    // synchronizing element state. Its ALWAYS pad already exists; bind both
    // targets only after the complete internal topology is ready.
    if (!bindGhostPadTargets(self)) {
        qCCritical(AndroidH265HardwareDecoderAdapterLog)
            << "Android H.265 adapter could not bind its external pad targets"
            << adapterFactoryName;
        return;
    }

    self->valid = TRUE;

    qCInfo(AndroidH265HardwareDecoderAdapterLog)
        << "Android H.265 adapter instance created"
        << "adapterFactory" << adapterFactoryName
        << "requestedDecoder" << hardwareDecoderFactoryName
        << "actualDecoder"
        << pluginAndFactoryName(gst_element_get_factory(self->decoder))
        << "vendor MediaCodec candidate";
}

void alternativeAdapterClassInit(gpointer classPointer,
                                 gpointer classData)
{
    configureAdapterClass(
        static_cast<GstQgcAndroidH265HardwareDecoderClass *>(classPointer),
        static_cast<const AdapterRouteConfig *>(classData));
}

void alternativeAdapterInstanceInit(GTypeInstance *instance,
                                    gpointer classPointer)
{
    auto *const self =
        reinterpret_cast<GstQgcAndroidH265HardwareDecoder *>(instance);
    const auto *const klass =
        static_cast<const GstQgcAndroidH265HardwareDecoderClass *>(classPointer);
    const AdapterRouteConfig *const routeConfig = klass->routeConfig;
    if (!routeConfig) {
        qCCritical(AndroidH265HardwareDecoderAdapterLog)
            << "Alternative Android H.265 adapter has no immutable route configuration";
        initializeAdapterInstance(self, QByteArray(), QByteArray());
        return;
    }

    initializeAdapterInstance(self,
                              routeConfig->elementFactoryName,
                              routeConfig->hardwareDecoderFactoryName);
}

bool registerAlternativeAdapterRoute(const HardwareDecoderCandidate &candidate,
                                     int alternativeIndex)
{
    auto *const routeConfig = new AdapterRouteConfig;
    routeConfig->elementFactoryName = QByteArray(kAdapterFactoryName)
        + "-alt" + QByteArray::number(alternativeIndex);
    routeConfig->typeName = "QgcAndroidH265HardwareDecoderAlternative"
        + QByteArray::number(alternativeIndex);
    routeConfig->hardwareDecoderFactoryName = candidate.factoryName;

    const GTypeInfo typeInfo = {
        sizeof(GstQgcAndroidH265HardwareDecoderClass),
        nullptr,
        nullptr,
        alternativeAdapterClassInit,
        nullptr,
        routeConfig,
        sizeof(GstQgcAndroidH265HardwareDecoder),
        0,
        alternativeAdapterInstanceInit,
        nullptr,
    };
    const GType adapterType = g_type_register_static(
        GST_TYPE_BIN,
        routeConfig->typeName.constData(),
        &typeInfo,
        static_cast<GTypeFlags>(0));
    if (adapterType == G_TYPE_INVALID
        || !gst_element_register(nullptr,
                                 routeConfig->elementFactoryName.constData(),
                                 GST_RANK_NONE,
                                 adapterType)) {
        // The GType can retain class_data after registration. Keep the route
        // record alive even when its element factory could not be published.
        s_alternativeAdapterRoutes.append(routeConfig);
        qCCritical(AndroidH265HardwareDecoderAdapterLog)
            << "Failed to register alternative Android H.265 adapter route"
            << routeConfig->elementFactoryName
            << "decoder" << routeConfig->hardwareDecoderFactoryName;
        return false;
    }

    s_alternativeAdapterRoutes.append(routeConfig);
    s_registeredElementFactoryNames.append(
        QString::fromUtf8(routeConfig->elementFactoryName));
    qCInfo(AndroidH265HardwareDecoderAdapterLog)
        << "Registered receiver-specific Android H.265 adapter"
        << routeConfig->elementFactoryName
        << "at rank" << GST_RANK_NONE
        << "with internal vendor decoder"
        << routeConfig->hardwareDecoderFactoryName
        << "and hvc1 -> byte-stream/au conversion";
    return true;
}

} // namespace

static void gst_qgc_android_h265_hardware_decoder_class_init(
    GstQgcAndroidH265HardwareDecoderClass *klass)
{
    configureAdapterClass(klass, nullptr);
}

static void gst_qgc_android_h265_hardware_decoder_init(
    GstQgcAndroidH265HardwareDecoder *self)
{
    initializeAdapterInstance(self,
                              QByteArray(kAdapterFactoryName),
                              s_hardwareDecoderFactoryName);
}

#endif

bool AndroidH265HardwareDecoderAdapter::registerElement()
{
#if defined(Q_OS_ANDROID) && defined(QGC_GST_STREAMING)
    if (!gst_is_initialized()) {
        qCWarning(AndroidH265HardwareDecoderAdapterLog)
            << "GStreamer is not initialized; Android H.265 adapter was not registered";
        return false;
    }

    GstElementFactory *const existingFactory = gst_element_factory_find(kAdapterFactoryName);
    if (existingFactory) {
        const bool usable = !s_hardwareDecoderFactoryName.isEmpty();
        gst_plugin_feature_set_rank(
            GST_PLUGIN_FEATURE(existingFactory),
            usable ? kAdapterRank : static_cast<guint>(GST_RANK_NONE));
        gst_object_unref(existingFactory);
        return usable;
    }

    const QList<HardwareDecoderCandidate> hardwareCandidates =
        findByteStreamHardwareDecoderCandidates();
    if (hardwareCandidates.isEmpty()) {
        qCWarning(AndroidH265HardwareDecoderAdapterLog)
            << "No vendor Android MediaCodec H.265 decoder accepts Annex-B access units; "
               "the adapter was not registered";
        return false;
    }

    const int hardwareCandidateCount =
        static_cast<int>(hardwareCandidates.size());
    int preferredCandidateIndex = -1;
    for (int index = 0; index < hardwareCandidateCount; ++index) {
        const HardwareDecoderCandidate &candidate = hardwareCandidates.at(index);
        qCInfo(AndroidH265HardwareDecoderAdapterLog)
            << "Preflighting preferred Android H.265 Annex-B hardware candidate"
            << candidate.displayName
            << "lowLatencyVariant" << candidate.lowLatencyVariant
            << "rank" << candidate.rank;
        if (preflightHardwareDecoder(candidate.factoryName)) {
            preferredCandidateIndex = index;
            break;
        }
        qCWarning(AndroidH265HardwareDecoderAdapterLog)
            << "Android H.265 hardware candidate failed preferred-route preflight; trying the next candidate"
            << candidate.displayName;
    }
    if (preferredCandidateIndex < 0) {
        qCWarning(AndroidH265HardwareDecoderAdapterLog)
            << "No vendor Android MediaCodec H.265 decoder passed the preferred Annex-B adapter preflight";
        return false;
    }

    s_hardwareDecoderFactoryName =
        hardwareCandidates.at(preferredCandidateIndex).factoryName;
    if (!gst_element_register(nullptr,
                              kAdapterFactoryName,
                              kAdapterRank,
                              GST_TYPE_QGC_ANDROID_H265_HARDWARE_DECODER)) {
        qCCritical(AndroidH265HardwareDecoderAdapterLog)
            << "Failed to register Android H.265 hardware decoder adapter";
        s_hardwareDecoderFactoryName.clear();
        return false;
    }

    s_registeredElementFactoryNames = {
        QString::fromLatin1(kAdapterFactoryName),
    };
    qCInfo(AndroidH265HardwareDecoderAdapterLog)
        << "Registered" << kAdapterFactoryName << "at rank" << kAdapterRank
        << "with internal vendor decoder" << s_hardwareDecoderFactoryName
        << "and hvc1 -> byte-stream/au conversion";

    int alternativeIndex = 1;
    for (int index = preferredCandidateIndex + 1;
         index < hardwareCandidateCount;
         ++index) {
        // Preserve the proven A8 startup path exactly: only its preferred
        // candidate is preflighted eagerly. Later compatible candidates are
        // rank-NONE and are constructed only if one receiver needs recovery.
        (void) registerAlternativeAdapterRoute(hardwareCandidates.at(index),
                                               alternativeIndex++);
    }
    qCInfo(AndroidH265HardwareDecoderAdapterLog)
        << "Registered ordered A8-style Android H.265 adapter routes"
        << s_registeredElementFactoryNames;
    return true;
#else
    return false;
#endif
}

const char *AndroidH265HardwareDecoderAdapter::elementFactoryName()
{
    return kAdapterFactoryName;
}

const char *AndroidH265HardwareDecoderAdapter::selectedHardwareDecoderFactoryName()
{
#if defined(Q_OS_ANDROID) && defined(QGC_GST_STREAMING)
    return s_hardwareDecoderFactoryName.constData();
#else
    return "";
#endif
}

QStringList AndroidH265HardwareDecoderAdapter::registeredElementFactoryNames()
{
#if defined(Q_OS_ANDROID) && defined(QGC_GST_STREAMING)
    return s_registeredElementFactoryNames;
#else
    return {};
#endif
}

QStringList AndroidH265HardwareDecoderAdapter::alternativeElementFactoryNames()
{
    QStringList factories = registeredElementFactoryNames();
    if (!factories.isEmpty()
        && factories.constFirst() == QString::fromLatin1(kAdapterFactoryName)) {
        factories.removeFirst();
    }
    return factories;
}

bool AndroidH265HardwareDecoderAdapter::isAdapterElementFactoryName(
    const QString &factoryName)
{
    return registeredElementFactoryNames().contains(factoryName);
}

QString AndroidH265HardwareDecoderAdapter::hardwareDecoderFactoryNameForElementFactory(
    const QString &factoryName)
{
#if defined(Q_OS_ANDROID) && defined(QGC_GST_STREAMING)
    if (factoryName == QString::fromLatin1(kAdapterFactoryName)) {
        return QString::fromUtf8(s_hardwareDecoderFactoryName);
    }
    for (const AdapterRouteConfig *const routeConfig :
         std::as_const(s_alternativeAdapterRoutes)) {
        if (routeConfig
            && factoryName == QString::fromUtf8(routeConfig->elementFactoryName)
            && s_registeredElementFactoryNames.contains(factoryName)) {
            return QString::fromUtf8(
                routeConfig->hardwareDecoderFactoryName);
        }
    }
#else
    Q_UNUSED(factoryName)
#endif
    return {};
}

bool AndroidH265HardwareDecoderAdapter::adapterRouteContainsFactory(
    const QString &adapterFactoryName,
    const QString &reportedFactoryName)
{
    if (!isAdapterElementFactoryName(adapterFactoryName)
        || reportedFactoryName.isEmpty()) {
        return false;
    }

    return reportedFactoryName == adapterFactoryName
        || reportedFactoryName
            == hardwareDecoderFactoryNameForElementFactory(
                   adapterFactoryName);
}

bool AndroidH265HardwareDecoderAdapter::isVendorHardwareDecoderFactoryName(
    const char *factoryName)
{
    if (!factoryName) {
        return false;
    }

    const QString name = QString::fromUtf8(factoryName).toLower();
    if (!name.startsWith(QStringLiteral("amcviddec-")) ||
        name.contains(QStringLiteral("secure")) ||
        name.contains(QStringLiteral("soft")) ||
        name.contains(QStringLiteral("software")) ||
        name.contains(QStringLiteral("ffmpeg")) ||
        name.contains(QStringLiteral("swdec")) ||
        name.contains(QStringLiteral("swvdec"))) {
        return false;
    }

    // androidmedia exposes Android/Google reference codecs using the same
    // amcviddec prefix. They are CPU codecs and must not be treated as hardware.
    return !name.startsWith(QStringLiteral("amcviddec-omxgoogle")) &&
           !name.startsWith(QStringLiteral("amcviddec-c2android")) &&
           !name.startsWith(QStringLiteral("amcviddec-c2google")) &&
           !name.startsWith(QStringLiteral("amcviddec-c2goldfish"));
}
