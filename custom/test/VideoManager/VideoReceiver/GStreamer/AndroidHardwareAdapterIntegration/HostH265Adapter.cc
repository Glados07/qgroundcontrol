#include <QtCore/QtCore>
#define Q_OS_ANDROID
#define QGC_GST_STREAMING
#ifdef QGC_TEST_QT5_COMPAT
#include "AndroidH265HardwareDecoderAdapter-qt5.cc"
#else
#include "AndroidH265HardwareDecoderAdapter.cc"
#endif
