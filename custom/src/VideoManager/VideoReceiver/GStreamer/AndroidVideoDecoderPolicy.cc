/****************************************************************************
 *
 * Android video decoder selection policy.
 *
 ****************************************************************************/

#include "AndroidVideoDecoderPolicy.h"

#include "AndroidH265DecoderRoutePolicy.h"
#include "AndroidH265HardwareDecoderAdapter.h"
#include "QGCLoggingCategory.h"

#include <QtCore/QList>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QtGlobal>

#include <algorithm>

#if defined(Q_OS_ANDROID) && defined(QGC_GST_STREAMING)
#include <gst/gst.h>
#endif

QGC_LOGGING_CATEGORY(AndroidVideoDecoderPolicyLog, "gcs.custom.video.androidvideodecoderpolicy")

namespace {

// Written only while apply() runs during startup, before either decodebin is
// created, and read-only for the remainder of the process lifetime.
QStringList s_hvc1HardwareRetryFactoryNames;
QStringList s_byteStreamHardwareRetryFactoryNames;

} // namespace

#if defined(Q_OS_ANDROID) && defined(QGC_GST_STREAMING)
namespace {

// The A8 Mini H.265 adapter is PRIMARY+100. Direct vendor decoders are fixed
// at PRIMARY+3, so hvc1 -> Annex-B/AU normalization is unambiguously first.
// Strict mode disables every non-compatible autoplug decoder, including native
// Force Software (PRIMARY+1), even when no hardware path is available.
constexpr guint kDirectHardwareDecoderRank = GST_RANK_PRIMARY + 3;

QString pluginAndFactoryName(GstElementFactory *factory);

struct DirectHardwareDecoderCandidate {
    QString factoryName;
    guint originalRank = GST_RANK_NONE;
    bool matchesAdapterDecoder = false;
};

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
    return name
        && AndroidH265HardwareDecoderAdapter::isAdapterElementFactoryName(
            QString::fromUtf8(name));
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
                                       GstCaps *byteStreamCaps,
                                       bool compatibleAdapterAvailable,
                                       QStringList *orderedDirectFactoryNames,
                                       QStringList *orderedByteStreamFactoryNames)
{
    if (orderedDirectFactoryNames) {
        orderedDirectFactoryNames->clear();
    }
    if (orderedByteStreamFactoryNames) {
        orderedByteStreamFactoryNames->clear();
    }

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

    QList<DirectHardwareDecoderCandidate> directCandidates;
    QList<DirectHardwareDecoderCandidate> byteStreamCandidates;
    const QString adapterDecoderFactoryName = QString::fromUtf8(
        AndroidH265HardwareDecoderAdapter::selectedHardwareDecoderFactoryName());
    for (GList *node = codecFactories; node; node = node->next) {
        GstElementFactory *const factory = GST_ELEMENT_FACTORY(node->data);
        if (!isAdapterFactory(factory)
            && isVendorMediaCodecDecoderFactory(factory)) {
            GstPluginFeature *const feature = GST_PLUGIN_FEATURE(factory);
            const gchar *const rawFactoryName =
                gst_plugin_feature_get_name(feature);
            const QString factoryName = QString::fromUtf8(
                rawFactoryName ? rawFactoryName : "");
            const DirectHardwareDecoderCandidate candidate{
                factoryName,
                gst_plugin_feature_get_rank(feature),
                !adapterDecoderFactoryName.isEmpty()
                    && factoryName == adapterDecoderFactoryName,
            };
            if (factoryCanSinkCaps(factory, nativeStreamCaps)) {
                directCandidates.append(candidate);
            }
            if (orderedByteStreamFactoryNames
                && factoryCanSinkCaps(factory, byteStreamCaps)) {
                byteStreamCandidates.append(candidate);
            }
        }
    }

    const auto candidateLess = [](const auto &left, const auto &right) {
                  if (left.matchesAdapterDecoder
                      != right.matchesAdapterDecoder) {
                      return left.matchesAdapterDecoder;
                  }
                  if (left.originalRank != right.originalRank) {
                      return left.originalRank > right.originalRank;
                  }
                  return left.factoryName < right.factoryName;
              };
    std::sort(directCandidates.begin(),
              directCandidates.end(),
              candidateLess);
    std::sort(byteStreamCandidates.begin(),
              byteStreamCandidates.end(),
              candidateLess);
    if (orderedDirectFactoryNames) {
        for (const DirectHardwareDecoderCandidate &candidate :
             directCandidates) {
            orderedDirectFactoryNames->append(candidate.factoryName);
        }
    }
    if (orderedByteStreamFactoryNames) {
        for (const DirectHardwareDecoderCandidate &candidate :
             byteStreamCandidates) {
            orderedByteStreamFactoryNames->append(candidate.factoryName);
        }
    }
    const int directNativeVendorCount =
        static_cast<int>(directCandidates.size());

    // Enabling this product option means hardware decoding is required. A
    // device without a compatible MediaCodec path must fail explicitly; it
    // must never appear healthy by silently selecting a CPU decoder.
    const bool hardwarePathAvailable = compatibleAdapterAvailable
        || directNativeVendorCount > 0;
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
            newRank = kDirectHardwareDecoderRank;
        } else if (!adapter) {
            // The decoder remains constructible by explicit factory name, but
            // decodebin will not silently replace a failed MediaCodec path
            // with CPU decoding while strict hardware mode is enabled.
            newRank = GST_RANK_NONE;
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
            << "hardwarePathAvailable" << hardwarePathAvailable
            << "strictHardwareOnly" << true
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
    s_hvc1HardwareRetryFactoryNames.clear();
    s_byteStreamHardwareRetryFactoryNames.clear();

#if defined(Q_OS_ANDROID) && defined(QGC_GST_STREAMING)
    if (!forceHardwareDecoding) {
        qCInfo(AndroidVideoDecoderPolicyLog)
            << "Using native QGC/GStreamer Android decoder selection; "
               "vendor MediaCodec ranks were not overridden";
        return;
    }

    if (!gst_is_initialized()) {
        qCWarning(AndroidVideoDecoderPolicyLog)
            << "GStreamer is not initialized; decoder policy was not applied";
        return;
    }

    // Register the A8 Mini compatibility adapter before enumerating H.265
    // factories. Its rank is deliberately above direct hvc1 MediaCodec paths.
    const bool adapterRegistered =
        AndroidH265HardwareDecoderAdapter::registerElement();

    GstCaps *h264Caps = gst_caps_from_string("video/x-h264");
    GstCaps *avcCaps = gst_caps_from_string("video/x-h264,stream-format=(string)avc");
    GstCaps *h264ByteStreamCaps = gst_caps_from_string(
        "video/x-h264,stream-format=(string)byte-stream,alignment=(string)au");
    GstCaps *h265Caps = gst_caps_from_string("video/x-h265");
    GstCaps *hvc1Caps = gst_caps_from_string(
        "video/x-h265,stream-format=(string)hvc1,alignment=(string)au");
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
        "H.264", h264Caps, avcCaps, h264ByteStreamCaps, false, nullptr,
        nullptr);
    QStringList directHvc1DecoderFactoryNames;
    QStringList directByteStreamDecoderFactoryNames;
    const int directHvc1VendorCount = rankNativeVendorMediaCodecDecoders(
        "H.265",
        h265Caps,
        hvc1Caps,
        h265ByteStreamCaps,
        adapterRegistered,
        &directHvc1DecoderFactoryNames,
        &directByteStreamDecoderFactoryNames);
    const int directByteStreamVendorCount =
        static_cast<int>(directByteStreamDecoderFactoryNames.size());

    s_hvc1HardwareRetryFactoryNames =
        AndroidH265DecoderRoutePolicy::orderedRetryFactories(
            AndroidH265HardwareDecoderAdapter::alternativeElementFactoryNames(),
            directHvc1DecoderFactoryNames);
    s_byteStreamHardwareRetryFactoryNames =
        AndroidH265DecoderRoutePolicy::orderedRetryFactories(
            AndroidH265HardwareDecoderAdapter::alternativeElementFactoryNames(),
            directByteStreamDecoderFactoryNames);
    if (!s_hvc1HardwareRetryFactoryNames.isEmpty()
        || !s_byteStreamHardwareRetryFactoryNames.isEmpty()) {
        qCInfo(AndroidVideoDecoderPolicyLog)
            << "Selected packetization-specific H.265 hardware retry factories"
            << "hvc1" << s_hvc1HardwareRetryFactoryNames
            << "byteStream" << s_byteStreamHardwareRetryFactoryNames
            << "alternativeAnnexBAdapters"
            << AndroidH265HardwareDecoderAdapter::alternativeElementFactoryNames()
            << "directHvc1Decoders" << directHvc1DecoderFactoryNames
            << "directByteStreamDecoders"
            << directByteStreamDecoderFactoryNames;
    }

    if (directH264VendorCount > 0) {
        qCInfo(AndroidVideoDecoderPolicyLog)
            << "Prioritized" << directH264VendorCount
            << "vendor Android MediaCodec H.264 decoder(s) that accept avc; "
               "software H.264 autoplug candidates were disabled";
    } else {
        qCCritical(AndroidVideoDecoderPolicyLog)
            << "No vendor Android MediaCodec H.264 decoder accepting avc was found; "
               "strict hardware decoding cannot be satisfied; software H.264 "
               "autoplug candidates were disabled and decoding will fail";
    }

    if (adapterRegistered) {
        qCInfo(AndroidVideoDecoderPolicyLog)
            << "Android H.265 normalization adapter is preferred over"
            << directHvc1VendorCount
            << "direct hvc1 and" << directByteStreamVendorCount
            << "direct byte-stream vendor MediaCodec candidate(s); internal adapter decoder"
            << AndroidH265HardwareDecoderAdapter::selectedHardwareDecoderFactoryName()
            << "; software H.265 autoplug candidates were disabled";
    } else if (directHvc1VendorCount > 0) {
        qCInfo(AndroidVideoDecoderPolicyLog)
            << "Using" << directHvc1VendorCount
            << "direct hvc1 vendor MediaCodec H.265 decoder(s); software "
               "H.265 autoplug candidates were disabled";
    } else {
        qCCritical(AndroidVideoDecoderPolicyLog)
            << "No compatible vendor Android MediaCodec H.265 path was found; "
               "strict hardware decoding cannot be satisfied, software H.265 "
               "autoplug candidates were disabled, and decoding will fail";
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

QStringList AndroidVideoDecoderPolicy::hardwareRetryFactoryNames(
    bool nativeByteStream)
{
    return nativeByteStream ? s_byteStreamHardwareRetryFactoryNames
                            : s_hvc1HardwareRetryFactoryNames;
}
