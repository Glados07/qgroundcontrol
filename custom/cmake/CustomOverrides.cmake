# 保持二次开发版本的应用名和既有 QSettings 路径稳定。
set(QGC_APP_NAME "Custom-QGroundControl" CACHE STRING "App Name" FORCE)

# 原生 Viewer3D 后端仍依赖未接入 SettingsManager 的旧设置类。
# custom 仅编译扩展实现，并按需复用 src/Viewer3D 中无差异的公共源文件，避免重复符号。
set(QGC_VIEWER3D OFF CACHE BOOL "Use custom Viewer3D integration" FORCE)

# feature 分支的固件策略：仅保留 PX4 多旋翼并由 custom Factory 接管。
set(QGC_DISABLE_APM_MAVLINK ON CACHE BOOL "Disable APM Dialect" FORCE)
set(QGC_DISABLE_APM_PLUGIN ON CACHE BOOL "Disable APM Plugin" FORCE)
set(QGC_DISABLE_APM_PLUGIN_FACTORY ON CACHE BOOL "Disable APM Plugin Factory" FORCE)
set(QGC_DISABLE_PX4_PLUGIN_FACTORY ON CACHE BOOL "Disable PX4 Plugin Factory" FORCE)

# 使用包含 FuelCellController 消息的 MAVLink C 头文件。
set(QGC_MAVLINK_GIT_REPO
    "https://github.com/Glados07/c_library_v2.git"
    CACHE STRING "URL to MAVLink Git Repo" FORCE
)

set(QGC_MAVLINK_GIT_TAG
    "7da72f6ed37f41389d6d284bd18d7b2e9f66682e"
    CACHE STRING "Tag of MAVLink Git Repo" FORCE
)