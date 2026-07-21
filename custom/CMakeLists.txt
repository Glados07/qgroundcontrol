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
    src/FlightDisplay/GeneratorBusVoltageAlert.qml
    PROPERTIES QT_RESOURCE_ALIAS GeneratorBusVoltageAlert.qml
)
qt_add_qml_module(CustomModule
    URI Custom.Widgets
    VERSION 1.0
    RESOURCE_PREFIX /qml
    QML_FILES
        src/QmlControls/FuelStatusIndicatorPage.qml
        src/FlightDisplay/GeneratorBusVoltageAlert.qml
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
    CACHE INTERNAL "" FORCE
)

set(CUSTOM_LIBRARIES
    CustomModule
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
    ${CMAKE_SOURCE_DIR}/src/Viewer3D
    CACHE INTERNAL "" FORCE
)

# custom.ts 仅作为模板；实际语言目录由根目录 qt_add_translations 编译。
file(GLOB CUSTOM_TS_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/translations/custom_*.ts"
)
set(CUSTOM_TS_SOURCES "${CUSTOM_TS_SOURCES}" PARENT_SCOPE)
