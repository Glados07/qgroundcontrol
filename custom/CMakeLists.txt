message(STATUS "QGC: Adding Custom Plugin")

set_property(DIRECTORY ${CMAKE_SOURCE_DIR}
    APPEND PROPERTY COMPILE_DEFINITIONS
    QGC_CUSTOM_BUILD
    CUSTOMHEADER="CustomPlugin.h"
    CUSTOMCLASS=CustomPlugin
)

# Android 的 JNI 固定查找 org.mavlink.qgroundcontrol.QGCUsbSerialManager。
# 将原生 Android 模板复制到构建目录，再用 custom/android 的同路径文件覆盖；Gradle
# 最终只会看到一个同名 Java 类，修复逻辑因此无需散落到根 android/ 或 src/。
if(ANDROID)
    set(_qgc_default_android_dir "${CMAKE_SOURCE_DIR}/android")
    set(_qgc_custom_android_dir "${CMAKE_CURRENT_SOURCE_DIR}/android")
    set(_qgc_final_android_dir "${CMAKE_CURRENT_BINARY_DIR}/android")

    file(GLOB_RECURSE _qgc_default_android_files CONFIGURE_DEPENDS LIST_DIRECTORIES false
        "${_qgc_default_android_dir}/src/*"
        "${_qgc_default_android_dir}/res/*"
        "${_qgc_default_android_dir}/gradle/*"
        "${_qgc_default_android_dir}/*.gradle"
        "${_qgc_default_android_dir}/gradle.properties"
        "${_qgc_default_android_dir}/gradlew*"
        "${_qgc_default_android_dir}/AndroidManifest.xml"
    )
    file(GLOB_RECURSE _qgc_custom_android_files CONFIGURE_DEPENDS LIST_DIRECTORIES false
        "${_qgc_custom_android_dir}/*"
    )
    list(FILTER _qgc_custom_android_files EXCLUDE REGEX "/(\\.gradle|build)/")
    list(FILTER _qgc_custom_android_files EXCLUDE REGEX "/local\\.properties$")

    if(_qgc_custom_android_files)
        cmake_path(IS_PREFIX CMAKE_BINARY_DIR "${_qgc_final_android_dir}" NORMALIZE _qgc_final_android_in_binary)
        cmake_path(IS_PREFIX _qgc_default_android_dir "${_qgc_final_android_dir}" NORMALIZE _qgc_final_in_default_source)
        cmake_path(IS_PREFIX _qgc_custom_android_dir "${_qgc_final_android_dir}" NORMALIZE _qgc_final_in_custom_source)
        if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR
            OR NOT _qgc_final_android_in_binary
            OR _qgc_final_in_default_source
            OR _qgc_final_in_custom_source
        )
            message(FATAL_ERROR
                "QGC: Android custom overlay requires an out-of-source build; refusing to remove a source directory"
            )
        endif()

        # GLOB 的 CONFIGURE_DEPENDS 负责发现增删；显式文件依赖负责已有 Java/Gradle
        # 文件内容变化时重新 configure，从而刷新构建目录中的 overlay。
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
            ${_qgc_default_android_files}
            ${_qgc_custom_android_files}
        )

        message(STATUS "QGC: Overlaying custom Android package files")
        # 这是构建目录中的生成副本；每次 configure 重建可避免已删除的 overlay 文件残留。
        file(REMOVE_RECURSE "${_qgc_final_android_dir}")
        file(MAKE_DIRECTORY "${_qgc_final_android_dir}")
        file(COPY "${_qgc_default_android_dir}/." DESTINATION "${_qgc_final_android_dir}"
            PATTERN ".gradle" EXCLUDE
            PATTERN "build" EXCLUDE
            PATTERN "local.properties" EXCLUDE
        )
        file(COPY "${_qgc_custom_android_dir}/." DESTINATION "${_qgc_final_android_dir}"
            PATTERN ".gradle" EXCLUDE
            PATTERN "build" EXCLUDE
            PATTERN "local.properties" EXCLUDE
        )

        # UniRC channel input uses the Android Bluetooth adapter as a Classic
        # RFCOMM client. Qt requests the runtime permission; the corresponding
        # platform declarations still have to be present in the APK manifest.
        set(_qgc_android_manifest "${_qgc_final_android_dir}/AndroidManifest.xml")
        if(NOT EXISTS "${_qgc_android_manifest}")
            message(FATAL_ERROR "QGC: Generated Android package is missing AndroidManifest.xml")
        endif()
        file(READ "${_qgc_android_manifest}" _qgc_android_manifest_contents)
        set(_qgc_android_permission_anchor "    <!-- %%INSERT_PERMISSIONS -->")
        set(_qgc_bluetooth_manifest_injection "")

        string(FIND "${_qgc_android_manifest_contents}"
            "android:name=\"android.permission.BLUETOOTH\""
            _qgc_legacy_bluetooth_permission_position
        )
        if(_qgc_legacy_bluetooth_permission_position EQUAL -1)
            string(APPEND _qgc_bluetooth_manifest_injection
                "\n    <uses-permission android:name=\"android.permission.BLUETOOTH\" android:maxSdkVersion=\"30\" />"
            )
        elseif(NOT _qgc_android_manifest_contents MATCHES
            "<uses-permission[^>]*android:name=\"android\\.permission\\.BLUETOOTH\"[^>]*android:maxSdkVersion=\"30\"[^>]*/>"
        )
            message(FATAL_ERROR
                "QGC: Existing android.permission.BLUETOOTH must be limited to maxSdkVersion 30"
            )
        endif()

        string(FIND "${_qgc_android_manifest_contents}"
            "android:name=\"android.permission.BLUETOOTH_ADMIN\""
            _qgc_legacy_bluetooth_admin_permission_position
        )
        if(_qgc_legacy_bluetooth_admin_permission_position EQUAL -1)
            string(APPEND _qgc_bluetooth_manifest_injection
                "\n    <uses-permission android:name=\"android.permission.BLUETOOTH_ADMIN\" android:maxSdkVersion=\"30\" />"
            )
        endif()

        string(FIND "${_qgc_android_manifest_contents}"
            "android:name=\"android.permission.ACCESS_FINE_LOCATION\""
            _qgc_legacy_bluetooth_location_permission_position
        )
        if(_qgc_legacy_bluetooth_location_permission_position EQUAL -1)
            string(APPEND _qgc_bluetooth_manifest_injection
                "\n    <uses-permission android:name=\"android.permission.ACCESS_FINE_LOCATION\" android:maxSdkVersion=\"30\" />"
            )
        endif()

        string(FIND "${_qgc_android_manifest_contents}"
            "android:name=\"android.permission.BLUETOOTH_CONNECT\""
            _qgc_bluetooth_connect_permission_position
        )
        if(_qgc_bluetooth_connect_permission_position EQUAL -1)
            string(APPEND _qgc_bluetooth_manifest_injection
                "\n    <uses-permission android:name=\"android.permission.BLUETOOTH_CONNECT\" />"
            )
        endif()

        string(FIND "${_qgc_android_manifest_contents}"
            "android:name=\"android.permission.BLUETOOTH_SCAN\""
            _qgc_bluetooth_scan_permission_position
        )
        if(_qgc_bluetooth_scan_permission_position EQUAL -1)
            string(APPEND _qgc_bluetooth_manifest_injection
                "\n    <uses-permission android:name=\"android.permission.BLUETOOTH_SCAN\" android:usesPermissionFlags=\"neverForLocation\" />"
            )
        endif()

        if(_qgc_bluetooth_manifest_injection)
            string(FIND "${_qgc_android_manifest_contents}"
                "${_qgc_android_permission_anchor}"
                _qgc_android_permission_anchor_position
            )
            if(_qgc_android_permission_anchor_position EQUAL -1)
                message(FATAL_ERROR
                    "QGC: AndroidManifest.xml is missing the permission insertion anchor"
                )
            endif()
            string(REPLACE
                "${_qgc_android_permission_anchor}"
                "${_qgc_android_permission_anchor}${_qgc_bluetooth_manifest_injection}"
                _qgc_android_manifest_contents
                "${_qgc_android_manifest_contents}"
            )
            file(WRITE "${_qgc_android_manifest}" "${_qgc_android_manifest_contents}")
        endif()

        file(READ "${_qgc_android_manifest}" _qgc_android_manifest_validation)
        if(NOT _qgc_android_manifest_validation MATCHES
            "<uses-permission[^>]*android:name=\"android\\.permission\\.BLUETOOTH\"[^>]*android:maxSdkVersion=\"30\"[^>]*/>"
        )
            message(FATAL_ERROR
                "QGC: Generated AndroidManifest.xml is missing the maxSdkVersion 30 BLUETOOTH permission"
            )
        endif()
        string(FIND "${_qgc_android_manifest_validation}"
            "android:name=\"android.permission.BLUETOOTH_CONNECT\""
            _qgc_generated_bluetooth_connect_permission_position
        )
        if(_qgc_generated_bluetooth_connect_permission_position EQUAL -1)
            message(FATAL_ERROR
                "QGC: Generated AndroidManifest.xml is missing BLUETOOTH_CONNECT"
            )
        endif()
        string(FIND "${_qgc_android_manifest_validation}"
            "android:name=\"android.permission.BLUETOOTH_SCAN\""
            _qgc_generated_bluetooth_scan_permission_position
        )
        if(_qgc_generated_bluetooth_scan_permission_position EQUAL -1)
            message(FATAL_ERROR
                "QGC: Generated AndroidManifest.xml is missing BLUETOOTH_SCAN"
            )
        endif()

        # Qt's Android deployment invokes Gradle from a generated project. Keep the
        # regular build cache, but disable configuration-cache reuse for the custom
        # package so stale instrumented AGP classes cannot break APK packaging before
        # the project build script is evaluated.
        set(_qgc_gradle_properties "${_qgc_final_android_dir}/gradle.properties")
        if(NOT EXISTS "${_qgc_gradle_properties}")
            message(FATAL_ERROR "QGC: Generated Android package is missing gradle.properties")
        endif()
        file(READ "${_qgc_gradle_properties}" _qgc_gradle_properties_contents)
        if(_qgc_gradle_properties_contents MATCHES "org\\.gradle\\.configuration-cache[ \t]*=[ \t]*true")
            string(REGEX REPLACE
                "org\\.gradle\\.configuration-cache[ \t]*=[ \t]*true"
                "org.gradle.configuration-cache=false"
                _qgc_gradle_properties_contents
                "${_qgc_gradle_properties_contents}"
            )
            file(WRITE "${_qgc_gradle_properties}" "${_qgc_gradle_properties_contents}")
        elseif(NOT _qgc_gradle_properties_contents MATCHES "org\\.gradle\\.configuration-cache[ \t]*=[ \t]*false")
            file(APPEND "${_qgc_gradle_properties}"
                "\n# Custom Android build stability\norg.gradle.configuration-cache=false\n"
            )
        endif()

        set(_qgc_usb_manager_overlay
            "${_qgc_final_android_dir}/src/org/mavlink/qgroundcontrol/QGCUsbSerialManager.java"
        )
        if(NOT EXISTS "${_qgc_usb_manager_overlay}")
            message(FATAL_ERROR "QGC: Custom Android USB serial manager overlay was not generated")
        endif()
        file(READ "${_qgc_usb_manager_overlay}" _qgc_usb_manager_overlay_header LIMIT 4096)
        if(NOT _qgc_usb_manager_overlay_header MATCHES "QGC_CUSTOM_ANDROID_USB_SERIAL_MANAGER_V1")
            message(FATAL_ERROR "QGC: Generated Android package does not contain the custom USB serial manager")
        endif()

        set(_qgc_media_library_overlay
            "${_qgc_final_android_dir}/src/org/mavlink/qgroundcontrol/QGCCustomMediaLibrary.java"
        )
        if(NOT EXISTS "${_qgc_media_library_overlay}")
            message(FATAL_ERROR "QGC: Custom Android media library overlay was not generated")
        endif()
        file(READ "${_qgc_media_library_overlay}" _qgc_media_library_overlay_header LIMIT 4096)
        if(NOT _qgc_media_library_overlay_header MATCHES "QGC_CUSTOM_ANDROID_MEDIA_LIBRARY_V2")
            message(FATAL_ERROR "QGC: Generated Android package does not contain the custom media library")
        endif()

        set(QGC_ANDROID_PACKAGE_SOURCE_DIR "${_qgc_final_android_dir}"
            CACHE PATH "Path to the merged custom Android package template" FORCE
        )
        message(STATUS "QGC: Android package template path: ${QGC_ANDROID_PACKAGE_SOURCE_DIR}")
    else()
        set(QGC_ANDROID_PACKAGE_SOURCE_DIR "${_qgc_default_android_dir}"
            CACHE PATH "Path to the default Android package template" FORCE
        )
    endif()
endif()

# Viewer3D 本地场景需要 Quick3D；AssetUtils 用于 OBJ/glTF/GLB 运行时加载。
list(APPEND CUSTOM_QT_COMPONENTS
    Bluetooth
    Quick3D
    Quick3DAssetUtils
)
list(REMOVE_DUPLICATES CUSTOM_QT_COMPONENTS)
set(CUSTOM_QT_COMPONENTS ${CUSTOM_QT_COMPONENTS} CACHE INTERNAL "" FORCE)

# Google 3D Maps 是可选能力。没有 WebEngine 时仍可构建本地 Viewer3D。
find_package(Qt6 COMPONENTS WebEngineQuick QUIET)

function(_qgc_viewer3d_promote_local_imported_target target_name)
    if(TARGET ${target_name})
        get_property(_viewer3d_imported_targets DIRECTORY PROPERTY IMPORTED_TARGETS)
        if("${target_name}" IN_LIST _viewer3d_imported_targets)
            set_target_properties(${target_name} PROPERTIES IMPORTED_GLOBAL TRUE)
        endif()
    endif()
endfunction()

set(CUSTOM_VIEWER3D_GOOGLE_LIBRARIES)
if(TARGET Qt6::WebEngineQuick)
    _qgc_viewer3d_promote_local_imported_target(Qt6::WebEngineQuick)
    list(APPEND CUSTOM_VIEWER3D_GOOGLE_LIBRARIES Qt6::WebEngineQuick)
    set_property(DIRECTORY ${CMAKE_SOURCE_DIR}
        APPEND PROPERTY COMPILE_DEFINITIONS
        QGC_VIEWER3D_GOOGLE_WEBENGINE
    )
    message(STATUS "QGC: Viewer3D Google 3D Maps source enabled with Qt WebEngineQuick")
else()
    message(STATUS "QGC: Viewer3D Google 3D Maps source disabled: Qt6::WebEngineQuick was not found")
endif()

list(APPEND CUSTOM_RESOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/custom.qrc
)
set(QGC_RESOURCES ${QGC_RESOURCES} ${CUSTOM_RESOURCES} CACHE STRING "Paths to .qrc Resources" FORCE)

# Register only the reusable custom QML controls used by this project.
qt_add_library(CustomModule STATIC)
set_source_files_properties(
    src/QmlControls/FuelStatusIndicatorPage.qml
    PROPERTIES QT_RESOURCE_ALIAS FuelStatusIndicatorPage.qml
)
set_source_files_properties(
    src/QmlControls/ProximityRadarIndicatorPage.qml
    PROPERTIES QT_RESOURCE_ALIAS ProximityRadarIndicatorPage.qml
)
set_source_files_properties(
    src/FlightDisplay/GeneratorBusVoltageAlert.qml
    PROPERTIES QT_RESOURCE_ALIAS GeneratorBusVoltageAlert.qml
)
qt_add_qml_module(CustomModule
    URI Custom.Widgets
    VERSION 1.0
    RESOURCE_PREFIX /qml
    QML_FILES
        src/QmlControls/FuelStatusIndicatorPage.qml
        src/QmlControls/ProximityRadarIndicatorPage.qml
        src/FlightDisplay/GeneratorBusVoltageAlert.qml
    NO_PLUGIN
)

# QML types which extend the native FlightDisplay tree live in custom/src/FlightDisplay,
# but use their own module so that upstream QGroundControl.FlightDisplay stays untouched.
qt_add_library(CustomFlightDisplayModule STATIC)
set_source_files_properties(
    src/FlightDisplay/DualPipView.qml
    PROPERTIES QT_RESOURCE_ALIAS DualPipView.qml
)
set_source_files_properties(
    src/FlightDisplay/FlyViewSecondaryVideo.qml
    PROPERTIES QT_RESOURCE_ALIAS FlyViewSecondaryVideo.qml
)
set_source_files_properties(
    src/FlightDisplay/FlightDisplayViewSecondaryVideo.qml
    PROPERTIES QT_RESOURCE_ALIAS FlightDisplayViewSecondaryVideo.qml
)
qt_add_qml_module(CustomFlightDisplayModule
    URI Custom.FlightDisplay
    VERSION 1.0
    RESOURCE_PREFIX /qml
    QML_FILES
        src/FlightDisplay/DualPipView.qml
        src/FlightDisplay/FlyViewSecondaryVideo.qml
        src/FlightDisplay/FlightDisplayViewSecondaryVideo.qml
    NO_PLUGIN
)

# 只收集 custom 真正修改或新增的 Viewer3D 实现。
file(GLOB_RECURSE VIEWER3D_CUSTOM_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Viewer3D/*.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Viewer3D/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Viewer3D/*.hpp
)

# 以下文件与 QGC src 完全一致，直接复用原生实现，不在 custom 保存副本。
set(VIEWER3D_NATIVE_SOURCES
    ${CMAKE_SOURCE_DIR}/src/Viewer3D/CityMapGeometry.h
    ${CMAKE_SOURCE_DIR}/src/Viewer3D/earcut.hpp
    ${CMAKE_SOURCE_DIR}/src/Viewer3D/OsmParser.h
    ${CMAKE_SOURCE_DIR}/src/Viewer3D/OsmParserThread.cc
    ${CMAKE_SOURCE_DIR}/src/Viewer3D/OsmParserThread.h
    ${CMAKE_SOURCE_DIR}/src/Viewer3D/Viewer3DQmlVariableTypes.h
    ${CMAKE_SOURCE_DIR}/src/Viewer3D/Viewer3DTerrainGeometry.h
    ${CMAKE_SOURCE_DIR}/src/Viewer3D/Viewer3DTerrainTexture.cc
    ${CMAKE_SOURCE_DIR}/src/Viewer3D/Viewer3DTerrainTexture.h
    ${CMAKE_SOURCE_DIR}/src/Viewer3D/Viewer3DTileQuery.cc
    ${CMAKE_SOURCE_DIR}/src/Viewer3D/Viewer3DTileQuery.h
    ${CMAKE_SOURCE_DIR}/src/Viewer3D/Viewer3DTileReply.cc
    ${CMAKE_SOURCE_DIR}/src/Viewer3D/Viewer3DTileReply.h
    ${CMAKE_SOURCE_DIR}/src/Viewer3D/Viewer3DUtils.cc
    ${CMAKE_SOURCE_DIR}/src/Viewer3D/Viewer3DUtils.h
)

file(GLOB_RECURSE GIMBAL_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Gimbal/*.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Gimbal/*.h
)

file(GLOB_RECURSE COMMS_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Comms/*.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Comms/*.h
)

file(GLOB_RECURSE VIDEO_MANAGER_CUSTOM_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/VideoManager/*.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/VideoManager/*.h
)

file(GLOB_RECURSE SETTINGS_CUSTOM_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Settings/*.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Settings/*.h
)

file(GLOB_RECURSE ANDROID_CUSTOM_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Android/*.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Android/*.h
)

set(CUSTOM_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/CustomPlugin.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/CustomPlugin.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/AutoPilotPlugin/CustomAutoPilotPlugin.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/AutoPilotPlugin/CustomAutoPilotPlugin.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/FirmwarePlugin/CustomFirmwarePlugin.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/FirmwarePlugin/CustomFirmwarePlugin.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/FirmwarePlugin/CustomFirmwarePluginFactory.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/FirmwarePlugin/CustomFirmwarePluginFactory.h
    ${VIEWER3D_CUSTOM_SOURCES}
    ${VIEWER3D_NATIVE_SOURCES}
    ${GIMBAL_SOURCES}
    ${COMMS_SOURCES}
    ${VIDEO_MANAGER_CUSTOM_SOURCES}
    ${SETTINGS_CUSTOM_SOURCES}
    ${ANDROID_CUSTOM_SOURCES}
    CACHE INTERNAL "" FORCE
)

set(CUSTOM_LIBRARIES
    CustomModule
    CustomFlightDisplayModule
    Qt6::Bluetooth
    Qt6::Quick3D
    Qt6::Quick3DAssetUtils
    ${CUSTOM_VIEWER3D_GOOGLE_LIBRARIES}
    CACHE INTERNAL "" FORCE
)

set(CUSTOM_INCLUDE_DIRECTORIES
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/AutoPilotPlugin
    ${CMAKE_CURRENT_SOURCE_DIR}/src/FirmwarePlugin
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Viewer3D
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Gimbal
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Comms
    ${CMAKE_CURRENT_SOURCE_DIR}/src/VideoManager
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Settings
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Android
    ${CMAKE_SOURCE_DIR}/src/Viewer3D
    CACHE INTERNAL "" FORCE
)

# 协议测试保持在 custom 内，避免为了私有云台功能修改原生 test 框架。
# 移动端构建不生成额外测试应用；桌面 Debug/显式测试构建可直接运行该用例。
if(QGC_BUILD_TESTING AND NOT ANDROID AND NOT IOS)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    enable_testing()
    qt_add_executable(SiyiProtocolTest
        test/Gimbal/SiyiProtocolTest.cc
        src/Gimbal/A8MiniZoomPolicy.cc
        src/Gimbal/A8MiniZoomPolicy.h
        src/Gimbal/SiyiProtocol.cc
        src/Gimbal/SiyiProtocol.h
        src/Gimbal/ZoomStepPolicy.cc
        src/Gimbal/ZoomStepPolicy.h
    )
    target_include_directories(SiyiProtocolTest PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Gimbal
    )
    target_link_libraries(SiyiProtocolTest PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME SiyiProtocolTest COMMAND $<TARGET_FILE:SiyiProtocolTest>)
    add_custom_target(check_siyi_protocol
        COMMAND $<TARGET_FILE:SiyiProtocolTest>
        DEPENDS SiyiProtocolTest
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        USES_TERMINAL
    )

    qt_add_executable(Mt11ProtocolTest
        test/Gimbal/Mt11ProtocolTest.cc
        src/Gimbal/Mt11Protocol.cc
        src/Gimbal/Mt11Protocol.h
        src/Gimbal/Mt11ZoomPolicy.cc
        src/Gimbal/Mt11ZoomPolicy.h
        src/Gimbal/ZoomStepPolicy.cc
        src/Gimbal/ZoomStepPolicy.h
    )
    target_include_directories(Mt11ProtocolTest PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Gimbal
    )
    target_link_libraries(Mt11ProtocolTest PRIVATE
        Qt6::Core
        Qt6::Test
    )
    add_test(NAME Mt11ProtocolTest COMMAND $<TARGET_FILE:Mt11ProtocolTest>)
    add_custom_target(check_mt11_protocol
        COMMAND $<TARGET_FILE:Mt11ProtocolTest>
        DEPENDS Mt11ProtocolTest
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        USES_TERMINAL
    )

    qt_add_executable(GimbalMediaSessionPolicyTest
        test/Gimbal/GimbalMediaSessionPolicyTest.cc
        src/Gimbal/GimbalMediaSessionPolicy.cc
        src/Gimbal/GimbalMediaSessionPolicy.h
    )
    target_include_directories(GimbalMediaSessionPolicyTest PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Gimbal
    )
    target_link_libraries(GimbalMediaSessionPolicyTest PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME GimbalMediaSessionPolicyTest COMMAND $<TARGET_FILE:GimbalMediaSessionPolicyTest>)
    add_custom_target(check_gimbal_media_session_policy
        COMMAND $<TARGET_FILE:GimbalMediaSessionPolicyTest>
        DEPENDS GimbalMediaSessionPolicyTest
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        USES_TERMINAL
    )

    qt_add_executable(GimbalPhotoCapturePolicyTest
        test/Gimbal/GimbalPhotoCapturePolicyTest.cc
        src/Gimbal/GimbalPhotoCapturePolicy.cc
        src/Gimbal/GimbalPhotoCapturePolicy.h
    )
    target_include_directories(GimbalPhotoCapturePolicyTest PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Gimbal
    )
    target_link_libraries(GimbalPhotoCapturePolicyTest PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Test
    )
    add_test(NAME GimbalPhotoCapturePolicyTest COMMAND $<TARGET_FILE:GimbalPhotoCapturePolicyTest>)
    add_custom_target(check_gimbal_photo_capture_policy
        COMMAND $<TARGET_FILE:GimbalPhotoCapturePolicyTest>
        DEPENDS GimbalPhotoCapturePolicyTest
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        USES_TERMINAL
    )

    qt_add_executable(GimbalAzimuthPolicyTest
        test/Gimbal/GimbalAzimuthPolicyTest.cc
        src/Gimbal/GimbalAzimuthPolicy.cc
        src/Gimbal/GimbalAzimuthPolicy.h
    )
    target_include_directories(GimbalAzimuthPolicyTest PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Gimbal
    )
    target_link_libraries(GimbalAzimuthPolicyTest PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME GimbalAzimuthPolicyTest COMMAND $<TARGET_FILE:GimbalAzimuthPolicyTest>)
    add_custom_target(check_gimbal_azimuth_policy
        COMMAND $<TARGET_FILE:GimbalAzimuthPolicyTest>
        DEPENDS GimbalAzimuthPolicyTest
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        USES_TERMINAL
    )

    qt_add_executable(UniRcProtocolTest
        test/Android/UniRcProtocolTest.cc
        src/Android/UniRcChannelPolicy.cc
        src/Android/UniRcChannelPolicy.h
        src/Android/UniRcProtocol.cc
        src/Android/UniRcProtocol.h
    )
    target_include_directories(UniRcProtocolTest PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Android
    )
    target_link_libraries(UniRcProtocolTest PRIVATE
        Qt6::Core
        Qt6::Test
    )
    add_test(NAME UniRcProtocolTest COMMAND $<TARGET_FILE:UniRcProtocolTest>)
    add_custom_target(check_unirc_protocol
        COMMAND $<TARGET_FILE:UniRcProtocolTest>
        DEPENDS UniRcProtocolTest
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        USES_TERMINAL
    )

    qt_add_executable(AndroidH265DecoderRoutePolicyTest
        test/VideoManager/VideoReceiver/GStreamer/AndroidH265DecoderRoutePolicyTest.cc
        src/VideoManager/VideoReceiver/GStreamer/AndroidH265DecoderCapsPolicy.cc
        src/VideoManager/VideoReceiver/GStreamer/AndroidH265DecoderCapsPolicy.h
        src/VideoManager/VideoReceiver/GStreamer/AndroidH265DecoderRoutePolicy.cc
        src/VideoManager/VideoReceiver/GStreamer/AndroidH265DecoderRoutePolicy.h
        src/VideoManager/VideoReceiver/GStreamer/AndroidH265StreamFormatPolicy.cc
        src/VideoManager/VideoReceiver/GStreamer/AndroidH265StreamFormatPolicy.h
    )
    target_include_directories(AndroidH265DecoderRoutePolicyTest PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/VideoManager/VideoReceiver/GStreamer
    )
    target_link_libraries(AndroidH265DecoderRoutePolicyTest PRIVATE
        Qt6::Core
        Qt6::Test
    )
    add_test(
        NAME AndroidH265DecoderRoutePolicyTest
        COMMAND $<TARGET_FILE:AndroidH265DecoderRoutePolicyTest>
    )
    add_custom_target(check_android_h265_decoder_route_policy
        COMMAND $<TARGET_FILE:AndroidH265DecoderRoutePolicyTest>
        DEPENDS AndroidH265DecoderRoutePolicyTest
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        USES_TERMINAL
    )
endif()

# custom.ts 仅作为模板；实际语言目录由根目录 qt_add_translations 编译。
file(GLOB CUSTOM_TS_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/translations/custom_*.ts"
)
set(CUSTOM_TS_SOURCES "${CUSTOM_TS_SOURCES}" PARENT_SCOPE)
