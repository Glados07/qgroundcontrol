cmake_minimum_required(VERSION 3.25)
project(RoukeMavlinkStandalone LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(ROUKE_UPSTREAM_MAVLINK "" CACHE PATH "QGC pinned c_library_v2 checkout")
if(NOT EXISTS "${ROUKE_UPSTREAM_MAVLINK}/all/all.h")
    message(FATAL_ERROR "Set ROUKE_UPSTREAM_MAVLINK to the QGC pinned c_library_v2 checkout")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/CustomMavlink.cmake")
qgc_prepare_rouke_mavlink("${ROUKE_UPSTREAM_MAVLINK}" "${CMAKE_CURRENT_BINARY_DIR}/mavlink" _rouke_include)
add_executable(RoukeMavlinkTest RoukeMavlinkTest.cc)
target_include_directories(RoukeMavlinkTest BEFORE PRIVATE
    "${_rouke_include}"
    "${CMAKE_CURRENT_LIST_DIR}/../../../src/MAVLink"
    "${ROUKE_UPSTREAM_MAVLINK}")
enable_testing()
add_test(NAME RoukeMavlinkTest COMMAND RoukeMavlinkTest)
