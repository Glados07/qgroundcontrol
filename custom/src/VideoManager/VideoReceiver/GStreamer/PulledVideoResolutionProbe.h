/****************************************************************************
 *
 * Reports the negotiated main pulled-video resolution from a GStreamer sink.
 *
 ****************************************************************************/

#pragma once

class QObject;

namespace PulledVideoResolutionProbe
{

/// Installs a negotiated-caps probe on a QGC GStreamer video sink.
/// Returns true only when a probe was installed for the main video receiver.
bool install(void *sink, QObject *parent);

} // namespace PulledVideoResolutionProbe
