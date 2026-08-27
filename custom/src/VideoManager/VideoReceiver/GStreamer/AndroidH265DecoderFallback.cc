/****************************************************************************
 *
 * Per-receiver Android H.265 hardware decoder fallback.
 *
 ****************************************************************************/

#include "AndroidH265DecoderFallback.h"

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
constexpr const char *kDirectRoutesExhaustedProperty =
    "customAndroidH265DecoderDirectRoutesExhausted";
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
    receiver->setProperty(kDirectRoutesExhaustedProperty, false);
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

bool AndroidH265DecoderFallback::usesAdapterRoute(
    VideoReceiver *receiver,
    const QString &uri)
{
#if defined(Q_OS_ANDROID) && defined(QGC_GST_STREAMING)
    return receiver && !uri.isEmpty() && receiver->uri() == uri
        && receiver->property(kRouteUriProperty).toString() == uri
        && receiver->property(kExplicitDecoderFactoryProperty)
               .toString().isEmpty();
#else
    Q_UNUSED(receiver)
    Q_UNUSED(uri)
    return false;
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
            && !receiver->property(kDirectRoutesExhaustedProperty).toBool()
            && !AndroidVideoDecoderPolicy::directHvc1DecoderFactoryNames()
                    .isEmpty();
    }

    // An explicit factory is itself generation-scoped proof that this is an
    // H.265 direct route. It may advance even when the failure occurs before
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

bool AndroidH265DecoderFallback::prepareDirectRetry(
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

    const QStringList directFactories =
        AndroidVideoDecoderPolicy::directHvc1DecoderFactoryNames();
    const int directFactoryCount =
        static_cast<int>(directFactories.size());
    const QString previousFactory =
        receiver->property(kExplicitDecoderFactoryProperty).toString();
    int previousIndex =
        receiver->property(kRouteCandidateIndexProperty).toInt();
    if (!previousFactory.isEmpty()) {
        const int matchingIndex = static_cast<int>(
            directFactories.indexOf(previousFactory));
        if (matchingIndex >= 0) {
            previousIndex = matchingIndex;
        }
    }
    const int nextIndex = previousIndex + 1;

    // The caller invokes this on the receiver's Qt thread before requesting a
    // complete stop. GstVideoReceiver consumes the frozen value only while it
    // constructs the next generation, so no running pipeline is mutated.
    receiver->setProperty(kRouteUriProperty, uri);
    if (nextIndex >= 0 && nextIndex < directFactoryCount) {
        const QString directFactory = directFactories.at(nextIndex);
        receiver->setProperty(kRouteCandidateIndexProperty, nextIndex);
        receiver->setProperty(kExplicitDecoderFactoryProperty, directFactory);
        qCWarning(AndroidH265DecoderFallbackLog)
            << "Advancing the receiver-specific H.265 hardware route;"
               " the next generation will use a direct vendor MediaCodec"
            << "receiver" << receiver
            << "uri" << uri
            << "generation" << generation
            << "previousFactory"
            << (previousFactory.isEmpty()
                    ? QStringLiteral("qgcandroidh265hwdec")
                    : previousFactory)
            << "factory" << directFactory
            << "candidate" << nextIndex + 1 << "/"
            << directFactoryCount
            << "sourceFrame" << sourceFrameReceived
            << "decoderBranchFailure" << confirmedDecoderBranchFailure
            << "reason" << (reason ? reason : "unspecified");
        return true;
    }

    // Do not rebuild the same failed direct factory forever. Once every
    // compatible direct MediaCodec has been tried, return to the same adapter
    // used by A8 Mini and keep software decoder ranks disabled.
    receiver->setProperty(kRouteCandidateIndexProperty,
                          directFactoryCount);
    receiver->setProperty(kExplicitDecoderFactoryProperty, QString());
    receiver->setProperty(kDirectRoutesExhaustedProperty, true);
    qCWarning(AndroidH265DecoderFallbackLog)
        << "All receiver-specific direct H.265 MediaCodec routes failed before a decoded frame;"
           " the next generation will return to the shared hardware adapter"
        << "receiver" << receiver
        << "uri" << uri
        << "generation" << generation
        << "previousFactory" << previousFactory
        << "candidateCount" << directFactoryCount
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
