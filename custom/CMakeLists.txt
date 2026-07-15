message(STATUS "QGC: Adding Custom Plugin")

set_property(DIRECTORY ${CMAKE_SOURCE_DIR}
    APPEND PROPERTY COMPILE_DEFINITIONS
    QGC_CUSTOM_BUILD
    CUSTOMHEADER="CustomPlugin.h"
    CUSTOMCLASS=CustomPlugin
)

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

set(CUSTOM_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/CustomPlugin.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/CustomPlugin.h
    ${VIEWER3D_CUSTOM_SOURCES}
    ${VIEWER3D_NATIVE_SOURCES}
    ${GIMBAL_SOURCES}
    ${COMMS_SOURCES}
    CACHE INTERNAL "" FORCE
)

set(CUSTOM_LIBRARIES
    Qt6::Quick3D
    Qt6::Quick3DAssetUtils
    ${CUSTOM_VIEWER3D_GOOGLE_LIBRARIES}
    CACHE INTERNAL "" FORCE
)

set(CUSTOM_INCLUDE_DIRECTORIES
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Viewer3D
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Gimbal
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Comms
    ${CMAKE_SOURCE_DIR}/src/Viewer3D
    CACHE INTERNAL "" FORCE
)

# custom 翻译由根目录 qt_add_translations 合并生成。
file(GLOB CUSTOM_TS_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/translations/custom_*.ts)
if(CUSTOM_TS_SOURCES)
    set(CUSTOM_TS_SOURCES ${CUSTOM_TS_SOURCES} CACHE INTERNAL "Custom translation .ts files")
endif()
