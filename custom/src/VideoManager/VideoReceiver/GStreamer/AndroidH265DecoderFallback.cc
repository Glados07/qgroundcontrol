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
// This property name is the intentionally small, generic bridge consumed by
// GstVideoReceiver when it builds one concrete H.265 decoding branch.
constexpr const char *kExplicitDecoderFactoryProperty =
    "customAndroidH265DecoderFactory";

void resetRouteForUri(VideoReceiver *receiver, const QString &uri)
{
    if (!receiver
        || receiver->property(kRouteUriProperty).toString() == uri) {
        return;
    }

    const QString previousFactory =
        receiver->property(kExplicitDecoderFactoryProperty).toString();
    receiver->setProperty(kRouteUriProperty, uri);
    receiver->setProperty(kExplicitDecoderFactoryProperty, QString());
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

bool AndroidH265DecoderFallback::canRetryDirect(
    VideoReceiver *receiver,
    const QString &uri,
    int videoCodec,
    bool adapterSelected,
    bool decoderFrameReceived,
    bool sinkFrameReceived)
{
#if defined(Q_OS_ANDROID) && defined(QGC_GST_STREAMING)
    if (!receiver || uri.isEmpty() || receiver->uri() != uri
        || videoCodec != VideoReceiver::VIDEO_CODEC_H265
        || !adapterSelected || decoderFrameReceived || sinkFrameReceived
        || !receiver->property(kExplicitDecoderFactoryProperty)
                .toString().isEmpty()) {
        return false;
    }

    return !AndroidVideoDecoderPolicy::preferredDirectHvc1DecoderFactoryName()
                .isEmpty();
#else
    Q_UNUSED(receiver)
    Q_UNUSED(uri)
    Q_UNUSED(videoCodec)
    Q_UNUSED(adapterSelected)
    Q_UNUSED(decoderFrameReceived)
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
        || !canRetryDirect(receiver,
                           uri,
                           videoCodec,
                           adapterSelected,
                           decoderFrameReceived,
                           sinkFrameReceived)) {
        return false;
    }

    const QString directFactory =
        AndroidVideoDecoderPolicy::preferredDirectHvc1DecoderFactoryName();
    if (directFactory.isEmpty()) {
        return false;
    }

    // The caller invokes this on the receiver's Qt thread before requesting a
    // complete stop. GstVideoReceiver consumes the frozen value only while it
    // constructs the next generation, so no running pipeline is mutated.
    receiver->setProperty(kRouteUriProperty, uri);
    receiver->setProperty(kExplicitDecoderFactoryProperty, directFactory);
    qCWarning(AndroidH265DecoderFallbackLog)
        << "H.265 adapter failed before producing a decoded frame;"
           " the next generation will use receiver-specific direct vendor MediaCodec"
        << "receiver" << receiver
        << "uri" << uri
        << "generation" << generation
        << "factory" << directFactory
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
