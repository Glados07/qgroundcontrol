# Standalone opt-in host test. Not included by the application or APK build.
QT = core
CONFIG += console c++17
CONFIG -= app_bundle
TEMPLATE = app
TARGET = AndroidHardwareAdapterIntegrationTest
POLICY_ROOT = $$clean_path($$PWD/../../../../../src/VideoManager/VideoReceiver/GStreamer)
INCLUDEPATH += $$PWD $$OUT_PWD $$POLICY_ROOT
SOURCES += $$PWD/AndroidHardwareAdapterIntegrationTest.cc \
           $$PWD/HostH264Adapter.cc $$PWD/HostH265Adapter.cc $$PWD/HostDecoderPolicy.cc \
           $$POLICY_ROOT/AndroidH265DecoderCapsPolicy.cc \
           $$POLICY_ROOT/AndroidH265DecoderRoutePolicy.cc

lessThan(QT_MAJOR_VERSION, 6) {
    !exists($$OUT_PWD/AndroidH265HardwareDecoderAdapter-qt5.cc): error("Run prepare_qt5_compat.py --build-dir BUILD_DIR first; see README.md")
    DEFINES += QGC_TEST_QT5_COMPAT
}
win32 {
    GST_TEST_SDK = $$(GST_TEST_SDK)
    isEmpty(GST_TEST_SDK): error("Set GST_TEST_SDK to the GStreamer MSVC SDK prefix")
    INCLUDEPATH += $$GST_TEST_SDK/include/gstreamer-1.0 \
                   $$GST_TEST_SDK/include/glib-2.0 $$GST_TEST_SDK/lib/glib-2.0/include
    LIBS += -L$$GST_TEST_SDK/lib -lgstreamer-1.0 -lgobject-2.0 -lglib-2.0
    QMAKE_CXXFLAGS += /utf-8
} else {
    CONFIG += link_pkgconfig
    PKGCONFIG += gstreamer-1.0
}
