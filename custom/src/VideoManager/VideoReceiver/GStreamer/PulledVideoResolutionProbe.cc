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

#include <gst/gst.h>

#include <utility>

namespace {

struct ProbeContext
{
    VideoReceiver *receiver = nullptr;
    QSize negotiatedSize;
    QSize lastLoggedSize;
    bool reportOnNextBuffer = false;
    bool missingCapsLogged = false;
    PulledVideoResolutionProbe::ResolutionHandler resolutionHandler;
};

bool updateNegotiatedSize(ProbeContext *context, const GstCaps *caps)
{
    if (!context
        || !caps
        || gst_caps_is_any(caps)
        || gst_caps_is_empty(caps)) {
        return false;
    }

    for (guint index = 0; index < gst_caps_get_size(caps); ++index) {
        const GstStructure *structure = gst_caps_get_structure(caps, index);
        gint width = 0;
        gint height = 0;
        if (!structure
            || !gst_structure_get_int(structure, "width", &width)
            || !gst_structure_get_int(structure, "height", &height)
            || width <= 0
            || height <= 0) {
            continue;
        }

        context->negotiatedSize = QSize(width, height);
        // A CAPS event alone is not enough: publish only after this exact
        // negotiation has delivered a real decoded frame.
        context->reportOnNextBuffer = true;
        context->missingCapsLogged = false;
        return true;
    }

    return false;
}

bool updateFromCurrentPadCaps(ProbeContext *context, GstPad *pad)
{
    if (!context || !pad) {
        return false;
    }

    GstCaps *caps = gst_pad_get_current_caps(pad);
    const bool updated = updateNegotiatedSize(context, caps);
    gst_clear_caps(&caps);
    return updated;
}

bool updateFromPeerCurrentCaps(ProbeContext *context, GstPad *pad)
{
    if (!context || !pad) {
        return false;
    }

    GstPad *peerPad = gst_pad_get_peer(pad);
    if (!peerPad) {
        return false;
    }

    const bool updated = updateFromCurrentPadCaps(context, peerPad);
    gst_object_unref(peerPad);
    return updated;
}

bool updateFromGhostTargetCurrentCaps(ProbeContext *context, GstPad *pad)
{
    if (!context || !pad || !GST_IS_GHOST_PAD(pad)) {
        return false;
    }

    GstPad *targetPad = gst_ghost_pad_get_target(GST_GHOST_PAD(pad));
    if (!targetPad) {
        return false;
    }

    const bool updated = updateFromCurrentPadCaps(context, targetPad);
    gst_object_unref(targetPad);
    return updated;
}

bool updateFromRealFrameCaps(ProbeContext *context, GstPad *pad)
{
    // qgcvideosinkbin exposes a ghost sink pad. Depending on the platform,
    // current caps can live on the outer pad, its decoder peer, or the ghost
    // target. A real buffer proves that the selected caps are in use.
    return updateFromCurrentPadCaps(context, pad)
        || updateFromPeerCurrentCaps(context, pad)
        || updateFromGhostTargetCurrentCaps(context, pad);
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
    if (context->resolutionHandler) {
        context->resolutionHandler(context->negotiatedSize);
    }
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

    if ((type & (GST_PAD_PROBE_TYPE_BUFFER
                 | GST_PAD_PROBE_TYPE_BUFFER_LIST)) != 0) {
        if (!context->negotiatedSize.isValid()) {
            (void) updateFromRealFrameCaps(context, pad);
        }

        if (context->reportOnNextBuffer) {
            context->reportOnNextBuffer = false;
            reportNegotiatedSize(context);
        } else if (!context->negotiatedSize.isValid()
                   && !context->missingCapsLogged) {
            context->missingCapsLogged = true;
            qCWarning(PulledVideoResolutionProbeLog)
                << "A main pulled-video frame arrived without readable"
                   " negotiated width/height caps";
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

bool PulledVideoResolutionProbe::install(
    void *sink,
    QObject *parent,
    ResolutionHandler resolutionHandler)
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
    context->resolutionHandler = std::move(resolutionHandler);
    const GstPadProbeType probeTypes = static_cast<GstPadProbeType>(
        GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM
        | GST_PAD_PROBE_TYPE_BUFFER
        | GST_PAD_PROBE_TYPE_BUFFER_LIST);
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

    (void) updateFromCurrentPadCaps(context, sinkPad);
    gst_object_unref(sinkPad);

    return true;
#else
    Q_UNUSED(sink);
    Q_UNUSED(parent);
    Q_UNUSED(resolutionHandler);
    return false;
#endif
}
