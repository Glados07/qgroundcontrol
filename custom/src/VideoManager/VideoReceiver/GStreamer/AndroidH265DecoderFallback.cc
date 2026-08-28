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
// This property name is the intentionally small, generic bridge consumed by
// GstVideoReceiver when it builds one concrete H.265 decoding branch.
constexpr const char *kExplicitDecoderFactoryProperty =
    "customAndroidH265DecoderFactory";

void resetRouteForUri(VideoReceiver *receiver, const QString &uri)
{
    if (!receiver) {
        return;
    }

    const QVariant routeUri = receiver->property(kRouteUriProperty);
    if (routeUri.isValid() && routeUri.toString() == uri) {
        return;
    }

    const QString previousFactory =
        receiver->property(kExplicitDecoderFactoryProperty).toString();
    receiver->setProperty(kRouteUriProperty, uri);
    receiver->setProperty(kExplicitDecoderFactoryProperty, QString());
    receiver->setProperty(kRouteCandidateIndexProperty, -1);
    receiver->setProperty(kRetryRoutesExhaustedProperty, false);
    if (!previousFactory.isEmpty()) {
        qCInfo(AndroidH265DecoderFallbackLog)
            << "Cleared receiver-specific Android H.265 decoder route after URI change"
            << "receiver" << receiver
            << "uri" << uri
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
            && !AndroidVideoDecoderPolicy::hardwareRetryFactoryNames()
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
    // A decoder-branch bus failure is stronger evidence than the source
    // buffer milestone: MediaCodec can reject CSD/caps while processing the
    // CAPS event, before the first compressed buffer reaches the tee probe.
    if ((!sourceFrameReceived && !confirmedDecoderBranchFailure)
        // A watchdog with decoder output but no sink frame is a display-path
        // diagnosis, not proof that another decoder is needed. A confirmed
        // decoder-branch bus failure is stronger: it may expose raw caps or
        // memory which the branch cannot negotiate, so the next hardware
        // candidate is still a valid bounded recovery route.
        || (decoderFrameReceived && !confirmedDecoderBranchFailure)
        || !canAdvanceHardwareRoute(receiver,
                                    uri,
                                    videoCodec,
                                    adapterSelected,
                                    sinkFrameReceived)) {
        return false;
    }

    const QStringList retryFactories =
        AndroidVideoDecoderPolicy::hardwareRetryFactoryNames();
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
                    ? QStringLiteral("A8-style Annex-B adapter")
                    : QStringLiteral("direct hvc1 MediaCodec"))
            << "candidate" << nextRoute.candidateIndex + 1 << "/"
            << retryFactoryCount
            << "sourceFrame" << sourceFrameReceived
            << "decoderBranchFailure" << confirmedDecoderBranchFailure
            << "reason" << (reason ? reason : "unspecified");
        return true;
    }

    // Do not rebuild a failed explicit route forever. Once every alternative
    // A8-style adapter and direct MediaCodec has been tried, return to the
    // preferred adapter used by A8 Mini and keep software ranks disabled.
    receiver->setProperty(kRouteCandidateIndexProperty,
                          nextRoute.candidateIndex);
    receiver->setProperty(kExplicitDecoderFactoryProperty, QString());
    receiver->setProperty(kRetryRoutesExhaustedProperty, true);
    qCWarning(AndroidH265DecoderFallbackLog)
        << "All receiver-specific H.265 hardware retry routes failed;"
           " the next generation will return to the preferred hardware adapter"
        << "receiver" << receiver
        << "uri" << uri
        << "generation" << generation
        << "previousFactory" << previousFactory
        << "candidateCount" << retryFactoryCount
        << "sourceFrame" << sourceFrameReceived
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
