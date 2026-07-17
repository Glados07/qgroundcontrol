/****************************************************************************
 *
 * Android H.265 video decoder selection policy.
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

// Native Force Software promotes avdec_h265 to PRIMARY + 1. A vendor
// decoder which accepts hvc1 directly must rank one level higher so a saved
// Force Software setting cannot keep winning over this explicit policy.
constexpr guint kDirectHardwareDecoderRank = GST_RANK_PRIMARY + 2;

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

bool isHardwareDecoderFactory(GstElementFactory *factory)
{
    if (!factory) {
        return false;
    }
    if (isAdapterFactory(factory)) {
        return true;
    }

    const gchar *const factoryName = gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory));
    if (!factoryName) {
        return false;
    }

    return AndroidH265HardwareDecoderAdapter::isVendorHardwareDecoderFactoryName(factoryName);
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
        qCInfo(AndroidVideoDecoderPolicyLog) << "Android H.265 hardware decoder policy is disabled";
        return;
    }

    if (!gst_is_initialized()) {
        qCWarning(AndroidVideoDecoderPolicyLog)
            << "GStreamer is not initialized; decoder policy was not applied";
        return;
    }

    // QGC 5/GStreamer 1.22 presents hvc1 to decodebin3. Many Android vendor
    // MediaCodec decoders only accept Annex-B byte-stream, so register a decoder
    // bin which performs the format conversion before the vendor decoder.
    const bool adapterRegistered = AndroidH265HardwareDecoderAdapter::registerElement();

    GstCaps *h265Caps = gst_caps_from_string("video/x-h265");
    GstCaps *hvc1Caps = gst_caps_from_string("video/x-h265,stream-format=(string)hvc1");
    GstCaps *byteStreamCaps = gst_caps_from_string(
        "video/x-h265,stream-format=(string)byte-stream,alignment=(string)au");
    if (!h265Caps || !hvc1Caps || !byteStreamCaps) {
        qCWarning(AndroidVideoDecoderPolicyLog) << "Unable to create H.265 caps";
        gst_clear_caps(&h265Caps);
        gst_clear_caps(&hvc1Caps);
        gst_clear_caps(&byteStreamCaps);
        return;
    }

    GList *const decoderFactories = gst_element_factory_list_get_elements(
        static_cast<GstElementFactoryListType>(GST_ELEMENT_FACTORY_TYPE_DECODER |
                                               GST_ELEMENT_FACTORY_TYPE_MEDIA_VIDEO),
        GST_RANK_NONE);
    GList *const h265Factories = gst_element_factory_list_filter(
        decoderFactories, h265Caps, GST_PAD_SINK, FALSE);
    gst_plugin_feature_list_free(decoderFactories);

    if (!h265Factories) {
        qCWarning(AndroidVideoDecoderPolicyLog) << "No H.265 decoder factory is available";
        gst_clear_caps(&h265Caps);
        gst_clear_caps(&hvc1Caps);
        gst_clear_caps(&byteStreamCaps);
        return;
    }

    int directHvc1HardwareCount = 0;
    for (GList *node = h265Factories; node; node = node->next) {
        GstElementFactory *const factory = GST_ELEMENT_FACTORY(node->data);
        GstPluginFeature *const feature = GST_PLUGIN_FEATURE(factory);
        const bool adapter = isAdapterFactory(factory);
        const bool hardware = isHardwareDecoderFactory(factory);
        const bool hvc1Compatible = factoryCanSinkCaps(factory, hvc1Caps);
        const bool byteStreamCompatible = factoryCanSinkCaps(factory, byteStreamCaps);
        const guint oldRank = gst_plugin_feature_get_rank(feature);
        guint newRank = oldRank;

        if (adapter && adapterRegistered) {
            // registerElement() assigns PRIMARY+100. Keep that rank so this bin
            // wins over avdec_h265 while software remains available as fallback.
            newRank = qMax(oldRank, static_cast<guint>(GST_RANK_PRIMARY + 100));
        } else if (!adapter && hardware && hvc1Compatible) {
            ++directHvc1HardwareCount;
            newRank = qMax(oldRank, kDirectHardwareDecoderRank);
        }

        if (newRank != oldRank) {
            gst_plugin_feature_set_rank(feature, newRank);
        }

        qCInfo(AndroidVideoDecoderPolicyLog)
            << "H.265 decoder candidate" << pluginAndFactoryName(factory)
            << (adapter ? "adapter" : (hardware ? "hardware" : "software"))
            << "hvc1Compatible" << hvc1Compatible
            << "byteStreamCompatible" << byteStreamCompatible
            << "rank" << oldRank << "->" << newRank;
    }

    if (adapterRegistered) {
        qCInfo(AndroidVideoDecoderPolicyLog)
            << "Android H.265 adapter is active; internal hardware decoder"
            << AndroidH265HardwareDecoderAdapter::selectedHardwareDecoderFactoryName()
            << "; software decoder ranks were preserved for fallback";
    } else if (directHvc1HardwareCount > 0) {
        qCInfo(AndroidVideoDecoderPolicyLog)
            << "Using" << directHvc1HardwareCount
            << "vendor H.265 hardware decoder(s) that accept hvc1 directly; "
               "software decoder ranks were preserved for fallback";
    } else {
        qCWarning(AndroidVideoDecoderPolicyLog)
            << "No compatible vendor H.265 hardware path was found; decoder ranks were left unchanged";
    }

    gst_plugin_feature_list_free(h265Factories);
    gst_clear_caps(&h265Caps);
    gst_clear_caps(&hvc1Caps);
    gst_clear_caps(&byteStreamCaps);
#else
    Q_UNUSED(forceHardwareDecoding)
#endif
}
