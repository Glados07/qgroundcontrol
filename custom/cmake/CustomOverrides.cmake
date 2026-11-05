# 保持二次开发版本的应用名和既有 QSettings 路径稳定。
set(QGC_APP_NAME "Custom-QGroundControl" CACHE STRING "App Name" FORCE)

# CustomOptions has already applied the native Debug default (ON). Use a
# product opt-in so existing caches also default to an application-only build.
# Enable regression builds with -DQGC_CUSTOM_BUILD_TESTING=ON in a Debug Kit.
option(QGC_CUSTOM_BUILD_TESTING "Enable tests in custom Debug builds" OFF)
set(_qgc_custom_tests_enabled OFF)
if(QGC_CUSTOM_BUILD_TESTING AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(_qgc_custom_tests_enabled ON)
endif()
set(QGC_BUILD_TESTING ${_qgc_custom_tests_enabled} CACHE BOOL "Enable testing" FORCE)
# The root build caches BUILD_TESTING=OFF when tests are disabled. Restore it
# as well on opt-in so CTest works after switching the same build directory.
set(BUILD_TESTING ${_qgc_custom_tests_enabled} CACHE BOOL "Build the testing tree." FORCE)
unset(_qgc_custom_tests_enabled)

# 团队 all 方言包含 Rouke 的 53022 转发消息。
set(QGC_MAVLINK_GIT_REPO "https://github.com/Glados07/c_library_v2.git" CACHE STRING "URL to MAVLink Git Repo" FORCE)
set(QGC_MAVLINK_GIT_TAG "b498a888ffc58a4dbe9c0f651207b70c978c7b0b" CACHE STRING "Tag of MAVLink Git Repo" FORCE)

# 原生 Viewer3D 后端仍依赖未接入 SettingsManager 的旧设置类。
# custom 仅编译扩展实现，并按需复用 src/Viewer3D 中无差异的公共源文件，避免重复符号。
set(QGC_VIEWER3D OFF CACHE BOOL "Use custom Viewer3D integration" FORCE)

# feature 分支的固件策略：仅保留 PX4 多旋翼并由 custom Factory 接管。
set(QGC_DISABLE_APM_MAVLINK ON CACHE BOOL "Disable APM Dialect" FORCE)
set(QGC_DISABLE_APM_PLUGIN ON CACHE BOOL "Disable APM Plugin" FORCE)
set(QGC_DISABLE_APM_PLUGIN_FACTORY ON CACHE BOOL "Disable APM Plugin Factory" FORCE)
set(QGC_DISABLE_PX4_PLUGIN_FACTORY ON CACHE BOOL "Disable PX4 Plugin Factory" FORCE)
