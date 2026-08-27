/****************************************************************************
 *
 * Reports the negotiated pulled-video resolution from a GStreamer sink.
 *
 ****************************************************************************/

#pragma once

#include <QtCore/QSize>

#include <functional>

class QObject;

namespace PulledVideoResolutionProbe
{

using ResolutionHandler = std::function<void(const QSize&)>;

/// Installs a negotiated-caps probe on a QGC GStreamer video sink.
/// Returns true only when a probe was installed for a non-thermal receiver.
bool install(void *sink,
             QObject *parent,
             ResolutionHandler resolutionHandler = {});

} // namespace PulledVideoResolutionProbe
