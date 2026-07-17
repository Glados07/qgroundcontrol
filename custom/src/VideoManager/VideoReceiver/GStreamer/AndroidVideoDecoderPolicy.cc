/****************************************************************************
 *
 * Android H.265 视频解码策略。
 *
 ****************************************************************************/

#include "AndroidVideoDecoderPolicy.h"

#include "QGCLoggingCategory.h"

#include <QtCore/QString>
#include <QtCore/QtGlobal>

#if defined(Q_OS_ANDROID) && defined(QGC_GST_STREAMING)
#include <gst/gst.h>
#endif

QGC_LOGGING_CATEGORY(AndroidVideoDecoderPolicyLog, "gcs.custom.video.androidvideodecoderpolicy")

#if defined(Q_OS_ANDROID) && defined(QGC_GST_STREAMING)
namespace {

constexpr guint kHardwareDecoderRank = GST_RANK_PRIMARY + 1;

bool isHardwareDecoderFactory(GstElementFactory* factory)
{
    if (!factory) {
        return false;
    }

    const gchar* const factoryName = gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory));
    if (!factoryName) {
        return false;
    }

    const QString nameLower = QString::fromUtf8(factoryName).toLower();

    // androidmedia 同时暴露厂商硬解和系统软解，不能只按 amcviddec 前缀判断。
    if (nameLower.startsWith(QStringLiteral("amcviddec-omxgoogle")) ||
        nameLower.startsWith(QStringLiteral("amcviddec-c2android")) ||
        nameLower.startsWith(QStringLiteral("amcviddec-c2google")) ||
        nameLower.startsWith(QStringLiteral("amcviddec-c2goldfish")) ||
        nameLower.contains(QStringLiteral("ffmpeg"))) {
        return false;
    }
    if (nameLower.startsWith(QStringLiteral("amcviddec-"))) {
        return true;
    }

    const auto metadataContainsHardware = [factory](const gchar* key) {
        const gchar* const value = gst_element_factory_get_metadata(factory, key);
        return value && QString::fromUtf8(value).contains(QStringLiteral("hardware"), Qt::CaseInsensitive);
    };

    if (metadataContainsHardware(GST_ELEMENT_METADATA_KLASS) ||
        metadataContainsHardware(GST_ELEMENT_METADATA_DESCRIPTION)) {
        return true;
    }

    static const char* const hardwareNameTags[] = {
        "va",
        "nv",
        "qsv",
        "msdk",
        "vulkan",
        "d3d",
        "dxva",
        "vtdec",
        "metal",
    };
    for (const char* const tag : hardwareNameTags) {
        if (nameLower.contains(QString::fromLatin1(tag))) {
            return true;
        }
    }

    return false;
}

QString pluginNameForFactory(GstElementFactory* factory)
{
    GstPluginFeature* const feature = GST_PLUGIN_FEATURE(factory);
    GstPlugin* const plugin = gst_plugin_feature_get_plugin(feature);
    if (!plugin) {
        return QStringLiteral("unknown");
    }

    const QString pluginName = QString::fromUtf8(gst_plugin_get_name(plugin));
    gst_object_unref(plugin);
    return pluginName;
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
        qCWarning(AndroidVideoDecoderPolicyLog) << "GStreamer is not initialized; decoder policy was not applied";
        return;
    }

    GstCaps* h265Caps = gst_caps_from_string("video/x-h265");
    GstCaps* hvc1Caps = gst_caps_from_string("video/x-h265,stream-format=hvc1");
    if (!h265Caps || !hvc1Caps) {
        qCWarning(AndroidVideoDecoderPolicyLog) << "Unable to create H.265 caps";
        gst_clear_caps(&h265Caps);
        gst_clear_caps(&hvc1Caps);
        return;
    }

    GList* const decoderFactories = gst_element_factory_list_get_elements(
        static_cast<GstElementFactoryListType>(GST_ELEMENT_FACTORY_TYPE_DECODER | GST_ELEMENT_FACTORY_TYPE_MEDIA_VIDEO),
        GST_RANK_NONE);
    GList* const h265Factories = gst_element_factory_list_filter(
        decoderFactories, h265Caps, GST_PAD_SINK, FALSE);

    gst_plugin_feature_list_free(decoderFactories);

    if (!h265Factories) {
        qCWarning(AndroidVideoDecoderPolicyLog) << "No H.265 decoder factory is available";
        gst_clear_caps(&h265Caps);
        gst_clear_caps(&hvc1Caps);
        return;
    }

    // 当前 GstVideoReceiver 将 H.265 parser 输出约束为 hvc1，只有与实际管线 caps 兼容的
    // MediaCodec 才能触发强制硬解，避免先禁用软解后才发现无法协商。
    int hardwareDecoderCount = 0;
    for (GList* node = h265Factories; node; node = node->next) {
        GstElementFactory* const factory = GST_ELEMENT_FACTORY(node->data);
        if (isHardwareDecoderFactory(factory) && gst_element_factory_can_sink_any_caps(factory, hvc1Caps)) {
            ++hardwareDecoderCount;
        }
    }

    if (hardwareDecoderCount == 0) {
        qCWarning(AndroidVideoDecoderPolicyLog)
            << "No hvc1-compatible vendor H.265 hardware decoder was found; software decoder ranks were left unchanged";
        for (GList* node = h265Factories; node; node = node->next) {
            GstElementFactory* const factory = GST_ELEMENT_FACTORY(node->data);
            GstPluginFeature* const feature = GST_PLUGIN_FEATURE(factory);
            const bool hardwareDecoder = isHardwareDecoderFactory(factory);
            const bool hvc1Compatible = gst_element_factory_can_sink_any_caps(factory, hvc1Caps);
            qCWarning(AndroidVideoDecoderPolicyLog)
                << "H.265 decoder candidate"
                << pluginNameForFactory(factory) + QLatin1Char('/') +
                       QString::fromUtf8(gst_plugin_feature_get_name(feature))
                << (hardwareDecoder ? "hardware" : "software")
                << "hvc1Compatible" << hvc1Compatible
                << "rank" << gst_plugin_feature_get_rank(feature);
        }
        gst_plugin_feature_list_free(h265Factories);
        gst_clear_caps(&h265Caps);
        gst_clear_caps(&hvc1Caps);
        return;
    }

    qCInfo(AndroidVideoDecoderPolicyLog)
        << "Applying Android H.265 hardware decoder policy with"
        << hardwareDecoderCount << "hardware decoder candidate(s)";

    for (GList* node = h265Factories; node; node = node->next) {
        GstElementFactory* const factory = GST_ELEMENT_FACTORY(node->data);
        GstPluginFeature* const feature = GST_PLUGIN_FEATURE(factory);
        const bool hardwareDecoder = isHardwareDecoderFactory(factory);
        const bool hvc1Compatible = gst_element_factory_can_sink_any_caps(factory, hvc1Caps);
        const guint oldRank = gst_plugin_feature_get_rank(feature);
        guint newRank = oldRank;
        if (hardwareDecoder && hvc1Compatible) {
            // 只提升厂商原有 rank，不降低设备自带的更高优先级。
            newRank = qMax(oldRank, kHardwareDecoderRank);
        } else if (!hardwareDecoder) {
            newRank = GST_RANK_NONE;
        }

        if (oldRank != newRank) {
            gst_plugin_feature_set_rank(feature, newRank);
        }

        qCInfo(AndroidVideoDecoderPolicyLog)
            << "H.265 decoder"
            << pluginNameForFactory(factory) + QLatin1Char('/') +
                   QString::fromUtf8(gst_plugin_feature_get_name(feature))
            << (hardwareDecoder ? "hardware" : "software")
            << "hvc1Compatible" << hvc1Compatible
            << "rank" << oldRank << "->" << newRank;
    }

    gst_plugin_feature_list_free(h265Factories);
    gst_clear_caps(&h265Caps);
    gst_clear_caps(&hvc1Caps);
#else
    Q_UNUSED(forceHardwareDecoding)
#endif
}
