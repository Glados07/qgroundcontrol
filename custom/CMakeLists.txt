message(STATUS "QGC: Adding Custom Plugin")

# Enable custom build
set_property(DIRECTORY ${CMAKE_SOURCE_DIR}
    APPEND PROPERTY COMPILE_DEFINITIONS
    QGC_CUSTOM_BUILD
    CUSTOMHEADER="CustomPlugin.h"
    CUSTOMCLASS=CustomPlugin
)
# Viewer3D 使用 Qt Quick3D 渲染本地 3D 场景；Quick3DAssetUtils 用于 OBJ/glTF/GLB 运行时加载。
# 必需 Qt 组件交给根目录 CMakeLists.txt:525 统一 find_package，确保 target 在根目录作用域可见。
list(APPEND CUSTOM_QT_COMPONENTS
    Quick3D
    Quick3DAssetUtils
)
list(REMOVE_DUPLICATES CUSTOM_QT_COMPONENTS)
set(CUSTOM_QT_COMPONENTS ${CUSTOM_QT_COMPONENTS} CACHE INTERNAL "" FORCE)

# Google 3D 在线地图只作为可选能力；未安装 Qt WebEngine 时自动关闭在线源，不影响本地 3D 视图构建。
find_package(Qt6 COMPONENTS WebEngineQuick QUIET)

function(_qgc_viewer3d_promote_local_imported_target target_name)
    if(TARGET ${target_name})
        get_property(_viewer3d_imported_targets DIRECTORY PROPERTY IMPORTED_TARGETS)
        if("${target_name}" IN_LIST _viewer3d_imported_targets)
            set_target_properties(${target_name} PROPERTIES IMPORTED_GLOBAL TRUE)
        endif()
    endif()
endfunction()

set(CUSTOM_VIEWER3D_EXTERNAL_LIBRARIES Qt6::Quick3DAssetUtils)

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

if(ANDROID)
    set(CUSTOM_ANDROID_DIR "${CMAKE_SOURCE_DIR}/custom/android")
    if(EXISTS "${CUSTOM_ANDROID_DIR}")
        file(GLOB CUSTOM_ANDROID_FILES "${CUSTOM_ANDROID_DIR}/*")
        if(CUSTOM_ANDROID_FILES)
            message(STATUS "QGC: Custom Android package template found. Overlaying custom files...")
            set(DEFAULT_ANDROID_DIR "${CMAKE_SOURCE_DIR}/android")
            set(FINAL_ANDROID_DIR "${CMAKE_BINARY_DIR}/custom/android")
            file(COPY "${DEFAULT_ANDROID_DIR}/." DESTINATION "${FINAL_ANDROID_DIR}")
            file(COPY "${CUSTOM_ANDROID_DIR}/." DESTINATION "${FINAL_ANDROID_DIR}")
            set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                            "${DEFAULT_ANDROID_DIR}/"
                            "${CUSTOM_ANDROID_DIR}/"
                        )
            set(QGC_ANDROID_PACKAGE_SOURCE_DIR "${FINAL_ANDROID_DIR}" CACHE PATH "Path to a custom Android package template" FORCE)
            message(STATUS "QGC: Android package template path will be set to: ${QGC_ANDROID_PACKAGE_SOURCE_DIR}")
        else()
            message(STATUS "QGC: Custom Android package template empty. Using default.")
        endif()
    else()
        message(STATUS "QGC: No custom Android package template found. Using default.")
    endif()
endif()

# Our own, custom resources
list(APPEND CUSTOM_RESOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/custom.qrc
)
set(QGC_RESOURCES ${QGC_RESOURCES} ${CUSTOM_RESOURCES} CACHE STRING "Paths to .qrc Resources" FORCE)

set(QML_IMPORT_PATH ${QML_IMPORT_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/res" CACHE STRING "Extra qml import paths" FORCE)

qt_add_library(CustomModule STATIC)

set_source_files_properties(res/Custom/Widgets/CustomArtificialHorizon.qml PROPERTIES QT_RESOURCE_ALIAS CustomArtificialHorizon.qml)
set_source_files_properties(res/Custom/Widgets/CustomAttitudeWidget.qml PROPERTIES QT_RESOURCE_ALIAS CustomAttitudeWidget.qml)
set_source_files_properties(res/Custom/Widgets/CustomIconButton.qml PROPERTIES QT_RESOURCE_ALIAS CustomIconButton.qml)
set_source_files_properties(res/Custom/Widgets/CustomOnOffSwitch.qml PROPERTIES QT_RESOURCE_ALIAS CustomOnOffSwitch.qml)
set_source_files_properties(res/Custom/Widgets/CustomQuickButton.qml PROPERTIES QT_RESOURCE_ALIAS CustomQuickButton.qml)
set_source_files_properties(res/Custom/Widgets/CustomSignalStrength.qml PROPERTIES QT_RESOURCE_ALIAS CustomSignalStrength.qml)
set_source_files_properties(res/Custom/Widgets/CustomToolBarButton.qml PROPERTIES QT_RESOURCE_ALIAS CustomToolBarButton.qml)
set_source_files_properties(res/Custom/Widgets/CustomVehicleButton.qml PROPERTIES QT_RESOURCE_ALIAS CustomVehicleButton.qml)


file(GLOB_RECURSE VIEWER3D_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Viewer3D/*.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Viewer3D/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Viewer3D/*.hpp
)
qt_add_qml_module(CustomModule
    URI Custom.Widgets
    VERSION 1.0
    RESOURCE_PREFIX /qml
    QML_FILES
        res/Custom/Widgets/CustomArtificialHorizon.qml
        res/Custom/Widgets/CustomAttitudeWidget.qml
        res/Custom/Widgets/CustomIconButton.qml
        res/Custom/Widgets/CustomOnOffSwitch.qml
        res/Custom/Widgets/CustomQuickButton.qml
        res/Custom/Widgets/CustomSignalStrength.qml
        res/Custom/Widgets/CustomToolBarButton.qml
        res/Custom/Widgets/CustomVehicleButton.qml
    NO_PLUGIN
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
    ${VIEWER3D_SOURCES}
    CACHE INTERNAL "" FORCE
)

set(CUSTOM_LIBRARIES
    CustomModule # Explicitly link QML module, needed by Qt 6.6.3, TODO: Remove when support for this version is dropped
    Qt6::Quick3D
    ${CUSTOM_VIEWER3D_EXTERNAL_LIBRARIES}
    ${CUSTOM_VIEWER3D_GOOGLE_LIBRARIES}
    CACHE INTERNAL "" FORCE
)

set(CUSTOM_INCLUDE_DIRECTORIES
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/AutoPilotPlugin
    ${CMAKE_CURRENT_SOURCE_DIR}/src/FirmwarePlugin
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Viewer3D
    CACHE INTERNAL "" FORCE
)

# Custom translations - export TS files to be merged into main qt_add_translations
file(GLOB CUSTOM_TS_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/translations/custom_*.ts)
if(CUSTOM_TS_SOURCES)
    set(CUSTOM_TS_SOURCES ${CUSTOM_TS_SOURCES} CACHE INTERNAL "Custom translation .ts files")
endif()
