/****************************************************************************
 *
 * Per-receiver Android H.265 hardware decoder fallback.
 *
 ****************************************************************************/

#include "AndroidH265DecoderFallback.h"

#include "AndroidH265DecoderRoutePolicy.h"
#include "AndroidH265HardwareDecoderAdapter.h"
#include "AndroidVideoDecoderPolicy.h"
#include "QGCLoggingCategory.h"
#include "VideoManager/VideoReceiver/VideoReceiver.h"

#include <QtCore/QPointer>
#include <QtCore/QVariant>
#include <QtCore/QtGlobal>

QGC_LOGGING_CATEGORY(AndroidH265DecoderFallbackLog,
                     "gcs.custom.video.androidh265decoderfallback")

namespace {

constexpr const char *kFallbackInstalledProperty =
    "customAndroidH265DecoderFallbackInstalled";
constexpr const char *kRouteUriProperty =
    "customAndroidH265DecoderRouteUri";
constexpr const char *kRouteCandidateIndexProperty =
    "customAndroidH265DecoderRouteCandidateIndex";
constexpr const char *kRetryRoutesExhaustedProperty =
    "customAndroidH265DecoderRetryRoutesExhausted";
constexpr const char *kRouteInputFormatProperty =
    "customAndroidH265DecoderRouteInputFormat";
constexpr const char *kParserOutputFormatProperty =
    "customAndroidH265ParserOutputFormat";
// This property name is the intentionally small, generic bridge consumed by
// GstVideoReceiver when it builds one concrete H.265 decoding branch.
constexpr const char *kExplicitDecoderFactoryProperty =
    "customAndroidH265DecoderFactory";

bool usesNativeByteStream(VideoReceiver *receiver)
{
    return receiver
        && receiver->property(kParserOutputFormatProperty).toString().compare(
               QStringLiteral("byte-stream"), Qt::CaseInsensitive) == 0;
}

bool canSwitchToNativeByteStream(VideoReceiver *receiver,
                                 const QString &uri,
                                 int videoCodec,
                                 bool adapterSelected,
                                 bool sinkFrameReceived)
{
    if (!receiver || uri.isEmpty() || receiver->uri() != uri
        || receiver->property(kRouteUriProperty).toString() != uri
        || usesNativeByteStream(receiver) || sinkFrameReceived) {
        return false;
    }

    const QString explicitFactory =
        receiver->property(kExplicitDecoderFactoryProperty).toString();
    return videoCodec == VideoReceiver::VIDEO_CODEC_H265
        || adapterSelected || !explicitFactory.isEmpty();
}

void resetRouteForUri(VideoReceiver *receiver, const QString &uri)
{
    if (!receiver) {
        return;
    }

    const QVariant routeUri = receiver->property(kRouteUriProperty);
    const QString inputFormat = usesNativeByteStream(receiver)
        ? QStringLiteral("byte-stream") : QStringLiteral("hvc1");
    if (routeUri.isValid() && routeUri.toString() == uri
        && receiver->property(kRouteInputFormatProperty).toString()
            == inputFormat) {
        return;
    }

    const QString previousFactory =
        receiver->property(kExplicitDecoderFactoryProperty).toString();
    receiver->setProperty(kRouteUriProperty, uri);
    receiver->setProperty(kRouteInputFormatProperty, inputFormat);
    receiver->setProperty(kExplicitDecoderFactoryProperty, QString());
    receiver->setProperty(kRouteCandidateIndexProperty, -1);
    receiver->setProperty(kRetryRoutesExhaustedProperty, false);
    if (!previousFactory.isEmpty()) {
        qCInfo(AndroidH265DecoderFallbackLog)
            << "Cleared receiver-specific Android H.265 decoder route after URI/input-format change"
            << "receiver" << receiver
            << "uri" << uri
            << "inputFormat" << inputFormat
            << "previousFactory" << previousFactory;
    }
}

} // namespace

void AndroidH265DecoderFallback::install(VideoReceiver *receiver)
{
#if defined(Q_OS_ANDROID) && defined(QGC_GST_STREAMING)
    if (!receiver
        || receiver->property(kFallbackInstalledProperty).toBool()) {
        return;
    }

    receiver->setProperty(kFallbackInstalledProperty, true);
    resetRouteForUri(receiver, receiver->uri());
    QObject::connect(
        receiver,
        &VideoReceiver::uriChanged,
        receiver,
        [guardedReceiver = QPointer<VideoReceiver>(receiver)](
            const QString &uri) {
            if (guardedReceiver) {
                resetRouteForUri(guardedReceiver.data(), uri);
            }
        });
#else
    Q_UNUSED(receiver)
#endif
}

void AndroidH265DecoderFallback::resetForCurrentInputFormat(
    VideoReceiver *receiver)
{
#if defined(Q_OS_ANDROID) && defined(QGC_GST_STREAMING)
    if (receiver
        && receiver->property(kFallbackInstalledProperty).toBool()) {
        resetRouteForUri(receiver, receiver->uri());
    }
#else
    Q_UNUSED(receiver)
#endif
}

QString AndroidH265DecoderFallback::activeAdapterFactoryName(
    VideoReceiver *receiver,
    const QString &uri)
{
#if defined(Q_OS_ANDROID) && defined(QGC_GST_STREAMING)
    if (!receiver || uri.isEmpty() || receiver->uri() != uri
        || receiver->property(kRouteUriProperty).toString() != uri) {
        return {};
    }

    const QString explicitFactory =
        receiver->property(kExplicitDecoderFactoryProperty).toString();
    if (explicitFactory.isEmpty()) {
        return QString::fromLatin1(
            AndroidH265HardwareDecoderAdapter::elementFactoryName());
    }
    return AndroidH265HardwareDecoderAdapter::isAdapterElementFactoryName(
               explicitFactory)
        ? explicitFactory
        : QString();
#else
    Q_UNUSED(receiver)
    Q_UNUSED(uri)
    return {};
#endif
}

bool AndroidH265DecoderFallback::canAdvanceHardwareRoute(
    VideoReceiver *receiver,
    const QString &uri,
    int videoCodec,
    bool adapterSelected,
    bool sinkFrameReceived)
{
#if defined(Q_OS_ANDROID) && defined(QGC_GST_STREAMING)
    if (!receiver || uri.isEmpty() || receiver->uri() != uri
        || receiver->property(kRouteUriProperty).toString() != uri
        || sinkFrameReceived) {
        return false;
    }

    const QString explicitFactory =
        receiver->property(kExplicitDecoderFactoryProperty).toString();
    const bool confirmedH265 =
        videoCodec == VideoReceiver::VIDEO_CODEC_H265
        || adapterSelected || !explicitFactory.isEmpty();
    if (!confirmedH265) {
        return false;
    }

    if (explicitFactory.isEmpty()) {
        // videoCodec is generation-scoped CAPS evidence. It also covers an
        // adapter which rejects CAPS before deep-element-added can publish
        // the outer adapter or its internal MediaCodec identity.
        return (adapterSelected
                || videoCodec == VideoReceiver::VIDEO_CODEC_H265)
            && !receiver->property(kRetryRoutesExhaustedProperty).toBool()
            && !AndroidVideoDecoderPolicy::hardwareRetryFactoryNames(
                    usesNativeByteStream(receiver))
                    .isEmpty();
    }

    // An explicit factory is itself generation-scoped proof that this is an
    // H.265 retry route. It may advance even when the failure occurs before
    // the first compressed buffer or decoder-selection callback.
    return true;
#else
    Q_UNUSED(receiver)
    Q_UNUSED(uri)
    Q_UNUSED(videoCodec)
    Q_UNUSED(adapterSelected)
    Q_UNUSED(sinkFrameReceived)
    return false;
#endif
}

bool AndroidH265DecoderFallback::prepareHardwareRetry(
    VideoReceiver *receiver,
    const QString &uri,
    quint64 generation,
    int videoCodec,
    bool adapterSelected,
    bool sourceFrameReceived,
    bool confirmedDecoderBranchFailure,
    bool decoderFrameReceived,
    bool sinkFrameReceived,
    const char *reason)
{
#if defined(Q_OS_ANDROID) && defined(QGC_GST_STREAMING)
    resetRouteForUri(receiver, uri);
    // A decoder-branch bus failure is stronger evidence than the source
    // buffer milestone: MediaCodec can reject CSD/caps while processing the
    // CAPS event, before the first compressed buffer reaches the tee probe.
    // Decoder output without a sink frame is evidence for the display/GL
    // branch, not the compressed-input route. Do not change packetization or
    // replace a healthy MediaCodec solely for a presentation failure.
    if (decoderFrameReceived && !confirmedDecoderBranchFailure) {
        return false;
    }

    if (canSwitchToNativeByteStream(receiver,
                                    uri,
                                    videoCodec,
                                    adapterSelected,
                                    sinkFrameReceived)) {
        const QString previousFactory =
            receiver->property(kExplicitDecoderFactoryProperty).toString();
        receiver->setProperty(kParserOutputFormatProperty,
                              QStringLiteral("byte-stream"));
        // The input-format change is a new route namespace. Clear any hvc1
        // candidate/exhaustion state before the owner builds the next full
        // generation; this never mutates the currently running pipeline.
        resetRouteForUri(receiver, uri);
        qCWarning(AndroidH265DecoderFallbackLog)
            << "Switching the receiver-specific H.265 packetization before changing decoder factory;"
               " the next generation will use native byte-stream/AU"
            << "receiver" << receiver
            << "uri" << uri
            << "generation" << generation
            << "previousFactory"
            << (previousFactory.isEmpty()
                    ? QStringLiteral("qgcandroidh265hwdec")
                    : previousFactory)
            << "sourceFrame" << sourceFrameReceived
            << "decoderFrame" << decoderFrameReceived
            << "decoderBranchFailure" << confirmedDecoderBranchFailure
            << "reason" << (reason ? reason : "unspecified");
        return true;
    }

    // Factory changes still require a compressed source buffer or a strict
    // decoder-branch error. The one-time hvc1 -> byte-stream switch above is
    // deliberately allowed from generation-scoped H.265 CAPS alone: the
    // hvc1 depay/parser path can wait for or discard media while constructing
    // codec_data, before the tee observes its first buffer.
    if (!sourceFrameReceived && !confirmedDecoderBranchFailure) {
        return false;
    }

    if (!canAdvanceHardwareRoute(receiver,
                                 uri,
                                 videoCodec,
                                 adapterSelected,
                                 sinkFrameReceived)) {
        return false;
    }

    const bool nativeByteStream = usesNativeByteStream(receiver);
    const QStringList retryFactories =
        AndroidVideoDecoderPolicy::hardwareRetryFactoryNames(
            nativeByteStream);
    const int retryFactoryCount =
        static_cast<int>(retryFactories.size());
    const QString previousFactory =
        receiver->property(kExplicitDecoderFactoryProperty).toString();
    const int previousIndex =
        receiver->property(kRouteCandidateIndexProperty).toInt();
    const AndroidH265DecoderRoutePolicy::RouteSelection nextRoute =
        AndroidH265DecoderRoutePolicy::nextRoute(retryFactories,
                                                 previousFactory,
                                                 previousIndex);

    // The caller invokes this on the receiver's Qt thread before requesting a
    // complete stop. GstVideoReceiver consumes the frozen value only while it
    // constructs the next generation, so no running pipeline is mutated.
    receiver->setProperty(kRouteUriProperty, uri);
    if (!nextRoute.exhausted) {
        receiver->setProperty(kRouteCandidateIndexProperty,
                              nextRoute.candidateIndex);
        receiver->setProperty(kExplicitDecoderFactoryProperty,
                              nextRoute.factoryName);
        const bool a8StyleAdapter =
            AndroidH265HardwareDecoderAdapter::isAdapterElementFactoryName(
                nextRoute.factoryName);
        qCWarning(AndroidH265DecoderFallbackLog)
            << "Advancing the receiver-specific H.265 hardware route;"
               " the next generation will use the next bounded hardware factory"
            << "receiver" << receiver
            << "uri" << uri
            << "generation" << generation
            << "previousFactory"
            << (previousFactory.isEmpty()
                    ? QStringLiteral("qgcandroidh265hwdec")
                    : previousFactory)
            << "factory" << nextRoute.factoryName
            << "routeKind"
            << (a8StyleAdapter
                    ? (nativeByteStream
                           ? QStringLiteral("native byte-stream/AU adapter")
                           : QStringLiteral("A8-style Annex-B adapter"))
                    : (nativeByteStream
                           ? QStringLiteral("direct byte-stream/AU MediaCodec")
                           : QStringLiteral("direct hvc1 MediaCodec")))
            << "candidate" << nextRoute.candidateIndex + 1 << "/"
            << retryFactoryCount
            << "sourceFrame" << sourceFrameReceived
            << "decoderFrame" << decoderFrameReceived
            << "decoderBranchFailure" << confirmedDecoderBranchFailure
            << "reason" << (reason ? reason : "unspecified");
        return true;
    }

    // Do not rebuild a failed explicit route forever. Once every alternative
    // normalization adapter and compatible direct MediaCodec has been tried,
    // return to the generation's preferred automatic hardware route and keep
    // software ranks disabled. For A8 this is the established hvc1 route; for
    // MT11 it is the native byte-stream/AU adapter route.
    receiver->setProperty(kRouteCandidateIndexProperty,
                          nextRoute.candidateIndex);
    receiver->setProperty(kExplicitDecoderFactoryProperty, QString());
    receiver->setProperty(kRetryRoutesExhaustedProperty, true);
    qCWarning(AndroidH265DecoderFallbackLog)
        << "All receiver-specific H.265 hardware retry routes failed;"
           " the next generation will return to the preferred hardware route"
        << "receiver" << receiver
        << "uri" << uri
        << "generation" << generation
        << "previousFactory" << previousFactory
        << "candidateCount" << retryFactoryCount
        << "inputFormat"
        << (nativeByteStream ? QStringLiteral("byte-stream")
                             : QStringLiteral("hvc1"))
        << "sourceFrame" << sourceFrameReceived
        << "decoderFrame" << decoderFrameReceived
        << "decoderBranchFailure" << confirmedDecoderBranchFailure
        << "reason" << (reason ? reason : "unspecified");
    return true;
#else
    Q_UNUSED(receiver)
    Q_UNUSED(uri)
    Q_UNUSED(generation)
    Q_UNUSED(videoCodec)
    Q_UNUSED(adapterSelected)
    Q_UNUSED(sourceFrameReceived)
    Q_UNUSED(confirmedDecoderBranchFailure)
    Q_UNUSED(decoderFrameReceived)
    Q_UNUSED(sinkFrameReceived)
    Q_UNUSED(reason)
    return false;
#endif
}
