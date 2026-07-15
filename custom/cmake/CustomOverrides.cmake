# 保持二次开发版本的应用名和既有 QSettings 路径稳定。
set(QGC_APP_NAME "Custom-QGroundControl" CACHE STRING "App Name" FORCE)

# 原生 Viewer3D 后端仍依赖未接入 SettingsManager 的旧设置类。
# custom 仅编译扩展实现，并按需复用 src/Viewer3D 中无差异的公共源文件，避免重复符号。
set(QGC_VIEWER3D OFF CACHE BOOL "Use custom Viewer3D integration" FORCE)
