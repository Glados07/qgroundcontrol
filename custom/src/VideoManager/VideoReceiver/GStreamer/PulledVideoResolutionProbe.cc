/****************************************************************************
 *
 * Reports the negotiated main pulled-video resolution from a GStreamer sink.
 *
 ****************************************************************************/

#include "PulledVideoResolutionProbe.h"

#include "QGCLoggingCategory.h"

QGC_LOGGING_CATEGORY(PulledVideoResolutionProbeLog,
                     "gcs.custom.gimbal.pulledvideoresolution")

#ifdef QGC_GST_STREAMING

#include "VideoManager/VideoReceiver/VideoReceiver.h"

#include <QtCore/QSize>

#include <gst/gst.h>
#include <gst/video/video.h>

namespace {

struct ProbeContext
{
    VideoReceiver *receiver = nullptr;
    QSize negotiatedSize;
    QSize lastLoggedSize;
    bool reportOnNextBuffer = false;
};

bool updateNegotiatedSize(ProbeContext *context, const GstCaps *caps)
{
    if (!context || !caps || !gst_caps_is_fixed(caps)) {
        return false;
    }

    GstVideoInfo videoInfo;
    gst_video_info_init(&videoInfo);
    if (!gst_video_info_from_caps(&videoInfo, caps)) {
        return false;
    }

    const int width = GST_VIDEO_INFO_WIDTH(&videoInfo);
    const int height = GST_VIDEO_INFO_HEIGHT(&videoInfo);
    if (width <= 0 || height <= 0) {
        return false;
    }

    context->negotiatedSize = QSize(width, height);
    // The native receiver can publish its early query-caps value after CAPS
    // negotiation. Re-publish the negotiated value on the following real frame
    // so VideoManager cannot be left with that provisional value.
    context->reportOnNextBuffer = true;
    return true;
}

void reportNegotiatedSize(ProbeContext *context)
{
    if (!context || !context->receiver || !context->negotiatedSize.isValid()) {
        return;
    }

    if (context->lastLoggedSize != context->negotiatedSize) {
        context->lastLoggedSize = context->negotiatedSize;
        qCInfo(PulledVideoResolutionProbeLog)
            << "Negotiated main pulled-video resolution:"
            << context->negotiatedSize.width()
            << "x"
            << context->negotiatedSize.height();
    }

    emit context->receiver->videoSizeChanged(context->negotiatedSize);
}

GstPadProbeReturn sinkPadProbe(GstPad *pad,
                               GstPadProbeInfo *info,
                               gpointer userData)
{
    auto *context = static_cast<ProbeContext *>(userData);
    if (!context || !info) {
        return GST_PAD_PROBE_OK;
    }

    const GstPadProbeType type = GST_PAD_PROBE_INFO_TYPE(info);
    if ((type & GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM) != 0) {
        GstEvent *event = GST_PAD_PROBE_INFO_EVENT(info);
        if (event && GST_EVENT_TYPE(event) == GST_EVENT_CAPS) {
            GstCaps *caps = nullptr;
            gst_event_parse_caps(event, &caps);
            (void) updateNegotiatedSize(context, caps);
        }
    }

    if ((type & GST_PAD_PROBE_TYPE_BUFFER) != 0) {
        if (!context->negotiatedSize.isValid()) {
            GstCaps *caps = gst_pad_get_current_caps(pad);
            (void) updateNegotiatedSize(context, caps);
            gst_clear_caps(&caps);
        }

        if (context->reportOnNextBuffer) {
            context->reportOnNextBuffer = false;
            reportNegotiatedSize(context);
        }
    }

    return GST_PAD_PROBE_OK;
}

void destroyProbeContext(gpointer userData)
{
    delete static_cast<ProbeContext *>(userData);
}

} // namespace

#endif // QGC_GST_STREAMING

bool PulledVideoResolutionProbe::install(void *sink, QObject *parent)
{
#ifdef QGC_GST_STREAMING
    auto *receiver = qobject_cast<VideoReceiver *>(parent);
    if (!sink || !receiver || receiver->isThermal()) {
        return false;
    }

    GstPad *sinkPad =
        gst_element_get_static_pad(static_cast<GstElement *>(sink), "sink");
    if (!sinkPad) {
        qCWarning(PulledVideoResolutionProbeLog)
            << "Unable to install pulled-video resolution probe: sink pad missing";
        return false;
    }

    auto *context = new ProbeContext;
    context->receiver = receiver;
    const GstPadProbeType probeTypes = static_cast<GstPadProbeType>(
        GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM | GST_PAD_PROBE_TYPE_BUFFER);
    const gulong probeId = gst_pad_add_probe(sinkPad,
                                             probeTypes,
                                             sinkPadProbe,
                                             context,
                                             destroyProbeContext);
    if (probeId == 0) {
        delete context;
        gst_object_unref(sinkPad);
        qCWarning(PulledVideoResolutionProbeLog)
            << "Unable to install pulled-video resolution probe";
        return false;
    }

    GstCaps *currentCaps = gst_pad_get_current_caps(sinkPad);
    (void) updateNegotiatedSize(context, currentCaps);
    gst_clear_caps(&currentCaps);
    gst_object_unref(sinkPad);

    qCDebug(PulledVideoResolutionProbeLog)
        << "Installed negotiated-caps probe for" << receiver->name();
    return true;
#else
    Q_UNUSED(sink);
    Q_UNUSED(parent);
    return false;
#endif
}
