/****************************************************************************
 *
 * Android video decoder selection policy.
 *
 ****************************************************************************/

#include "AndroidVideoDecoderPolicy.h"

#include "AndroidH265HardwareDecoderAdapter.h"
#include "QGCLoggingCategory.h"

#include <QtCore/QString>
#include <QtCore/QtGlobal>

#if defined(Q_OS_ANDROID) && defined(QGC_GST_STREAMING)
#include <gst/gst.h>
#endif

QGC_LOGGING_CATEGORY(AndroidVideoDecoderPolicyLog, "gcs.custom.video.androidvideodecoderpolicy")

#if defined(Q_OS_ANDROID) && defined(QGC_GST_STREAMING)
namespace {

// Native Force Software promotes avdec_h264/avdec_h265 to PRIMARY + 1. A
// vendor decoder which accepts QGC's parser output directly must rank one
// level higher so a saved Force Software setting cannot keep winning over
// this explicit policy.
constexpr guint kDirectHardwareDecoderRank = GST_RANK_PRIMARY + 3;

QString pluginAndFactoryName(GstElementFactory *factory);

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

bool isAdapterFactory(GstElementFactory *factory)
{
    const gchar *const name = factory
        ? gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory))
        : nullptr;
    return name && qstrcmp(name, AndroidH265HardwareDecoderAdapter::elementFactoryName()) == 0;
}

bool isAndroidMediaFactory(GstElementFactory *factory)
{
    if (!factory) {
        return false;
    }

    GstPlugin *const plugin =
        gst_plugin_feature_get_plugin(GST_PLUGIN_FEATURE(factory));
    if (!plugin) {
        return false;
    }

    const bool isAndroidMedia =
        qstrcmp(gst_plugin_get_name(plugin), "androidmedia") == 0;
    gst_object_unref(plugin);
    return isAndroidMedia;
}

bool isVendorMediaCodecDecoderFactory(GstElementFactory *factory)
{
    if (!factory) {
        return false;
    }
    if (isAdapterFactory(factory)) {
        return true;
    }
    if (!isAndroidMediaFactory(factory)) {
        return false;
    }

    const gchar *const factoryName = gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory));
    if (!factoryName) {
        return false;
    }

    return AndroidH265HardwareDecoderAdapter::isVendorHardwareDecoderFactoryName(factoryName);
}

int rankNativeVendorMediaCodecDecoders(const char *codecName,
                                       GstCaps *codecCaps,
                                       GstCaps *nativeStreamCaps,
                                       GstCaps *byteStreamCaps)
{
    GList *const decoderFactories = gst_element_factory_list_get_elements(
        static_cast<GstElementFactoryListType>(GST_ELEMENT_FACTORY_TYPE_DECODER |
                                               GST_ELEMENT_FACTORY_TYPE_MEDIA_VIDEO),
        GST_RANK_NONE);
    GList *const codecFactories = gst_element_factory_list_filter(
        decoderFactories, codecCaps, GST_PAD_SINK, FALSE);
    gst_plugin_feature_list_free(decoderFactories);

    if (!codecFactories) {
        qCWarning(AndroidVideoDecoderPolicyLog)
            << "No" << codecName << "decoder factory is available";
        return 0;
    }

    int directNativeVendorCount = 0;
    for (GList *node = codecFactories; node; node = node->next) {
        GstElementFactory *const factory = GST_ELEMENT_FACTORY(node->data);
        GstPluginFeature *const feature = GST_PLUGIN_FEATURE(factory);
        const bool adapter = isAdapterFactory(factory);
        const bool vendorMediaCodec =
            isVendorMediaCodecDecoderFactory(factory);
        const bool nativeCompatible = factoryCanSinkCaps(factory, nativeStreamCaps);
        const bool byteStreamCompatible = factoryCanSinkCaps(factory, byteStreamCaps);
        const guint oldRank = gst_plugin_feature_get_rank(feature);
        guint newRank = oldRank;

        if (!adapter && vendorMediaCodec && nativeCompatible) {
            ++directNativeVendorCount;
            newRank = qMax(oldRank, kDirectHardwareDecoderRank);
        }

        if (newRank != oldRank) {
            gst_plugin_feature_set_rank(feature, newRank);
        }

        qCInfo(AndroidVideoDecoderPolicyLog)
            << codecName << "decoder candidate" << pluginAndFactoryName(factory)
            << (adapter
                    ? "adapter"
                    : (vendorMediaCodec
                           ? "vendor MediaCodec candidate"
                           : "software/other"))
            << "nativeStreamCompatible" << nativeCompatible
            << "byteStreamCompatible" << byteStreamCompatible
            << "rank" << oldRank << "->" << newRank;
    }

    gst_plugin_feature_list_free(codecFactories);
    return directNativeVendorCount;
}

QString pluginAndFactoryName(GstElementFactory *factory)
{
    GstPluginFeature *const feature = GST_PLUGIN_FEATURE(factory);
    GstPlugin *const plugin = gst_plugin_feature_get_plugin(feature);
    const QString factoryName = QString::fromUtf8(gst_plugin_feature_get_name(feature));
    if (!plugin) {
        return QStringLiteral("unknown/") + factoryName;
    }

    const QString result = QString::fromUtf8(gst_plugin_get_name(plugin)) + QLatin1Char('/') + factoryName;
    gst_object_unref(plugin);
    return result;
}

} // namespace
#endif

void AndroidVideoDecoderPolicy::apply(bool forceHardwareDecoding)
{
#if defined(Q_OS_ANDROID) && defined(QGC_GST_STREAMING)
    if (!forceHardwareDecoding) {
        qCInfo(AndroidVideoDecoderPolicyLog) << "Android hardware decoder policy is disabled";
        return;
    }

    if (!gst_is_initialized()) {
        qCWarning(AndroidVideoDecoderPolicyLog)
            << "GStreamer is not initialized; decoder policy was not applied";
        return;
    }

    GstCaps *h264Caps = gst_caps_from_string("video/x-h264");
    GstCaps *avcCaps = gst_caps_from_string("video/x-h264,stream-format=(string)avc");
    GstCaps *h264ByteStreamCaps = gst_caps_from_string(
        "video/x-h264,stream-format=(string)byte-stream,alignment=(string)au");
    GstCaps *h265Caps = gst_caps_from_string("video/x-h265");
    GstCaps *hvc1Caps = gst_caps_from_string("video/x-h265,stream-format=(string)hvc1");
    GstCaps *h265ByteStreamCaps = gst_caps_from_string(
        "video/x-h265,stream-format=(string)byte-stream,alignment=(string)au");
    if (!h264Caps || !avcCaps || !h264ByteStreamCaps
        || !h265Caps || !hvc1Caps || !h265ByteStreamCaps) {
        qCWarning(AndroidVideoDecoderPolicyLog) << "Unable to create Android video decoder caps";
        gst_clear_caps(&h264Caps);
        gst_clear_caps(&avcCaps);
        gst_clear_caps(&h264ByteStreamCaps);
        gst_clear_caps(&h265Caps);
        gst_clear_caps(&hvc1Caps);
        gst_clear_caps(&h265ByteStreamCaps);
        return;
    }

    const int directH264VendorCount = rankNativeVendorMediaCodecDecoders(
        "H.264", h264Caps, avcCaps, h264ByteStreamCaps);
    const int directHvc1VendorCount = rankNativeVendorMediaCodecDecoders(
        "H.265", h265Caps, hvc1Caps, h265ByteStreamCaps);

    // Keep an Annex-B conversion fallback available, but rank it below a
    // direct hvc1-capable MediaCodec so it cannot hide the simpler path.
    const bool adapterRegistered =
        AndroidH265HardwareDecoderAdapter::registerElement();

    if (directH264VendorCount > 0) {
        qCInfo(AndroidVideoDecoderPolicyLog)
            << "Prioritized" << directH264VendorCount
            << "vendor Android MediaCodec H.264 decoder(s) that accept avc; "
               "software decoder ranks were preserved for fallback";
    } else {
        qCWarning(AndroidVideoDecoderPolicyLog)
            << "No vendor Android MediaCodec H.264 decoder accepting avc was found; "
               "H.264 decoder ranks were left unchanged";
    }

    if (directHvc1VendorCount > 0) {
        qCInfo(AndroidVideoDecoderPolicyLog)
            << "Prioritized" << directHvc1VendorCount
            << "vendor Android MediaCodec H.265 decoder(s) that accept hvc1 directly; "
               "the lower-ranked Annex-B adapter and software decoders remain available for fallback";
    } else if (adapterRegistered) {
        qCInfo(AndroidVideoDecoderPolicyLog)
            << "Android H.265 adapter is active; internal vendor MediaCodec candidate"
            << AndroidH265HardwareDecoderAdapter::selectedHardwareDecoderFactoryName()
            << "; software decoder ranks were preserved for fallback";
    } else {
        qCWarning(AndroidVideoDecoderPolicyLog)
            << "No compatible vendor Android MediaCodec H.265 path was found; decoder ranks were left unchanged";
    }

    gst_clear_caps(&h264Caps);
    gst_clear_caps(&avcCaps);
    gst_clear_caps(&h264ByteStreamCaps);
    gst_clear_caps(&h265Caps);
    gst_clear_caps(&hvc1Caps);
    gst_clear_caps(&h265ByteStreamCaps);
#else
    Q_UNUSED(forceHardwareDecoding)
#endif
}
