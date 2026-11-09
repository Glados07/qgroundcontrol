// Enable only the production Android GStreamer branch after host Qt headers.
#include <QtCore/QtCore>
#define Q_OS_ANDROID
#define QGC_GST_STREAMING
#include "AndroidH264HardwareDecoderAdapter.cc"
