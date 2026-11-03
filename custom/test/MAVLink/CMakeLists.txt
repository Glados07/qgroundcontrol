cmake_minimum_required(VERSION 3.25)
project(RoukeMavlinkStandalone LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(ROUKE_UPSTREAM_MAVLINK "" CACHE PATH "Team c_library_v2 checkout containing message 53022 in all")
if(NOT EXISTS "${ROUKE_UPSTREAM_MAVLINK}/all/all.h")
    message(FATAL_ERROR "Set ROUKE_UPSTREAM_MAVLINK to the team c_library_v2 checkout used by QGC")
endif()
add_executable(RoukeMavlinkTest RoukeMavlinkTest.cc)
target_include_directories(RoukeMavlinkTest BEFORE PRIVATE
    "${ROUKE_UPSTREAM_MAVLINK}"
    "${ROUKE_UPSTREAM_MAVLINK}/all"
    "${CMAKE_CURRENT_LIST_DIR}/../../../src/MAVLink")
enable_testing()
add_test(NAME RoukeMavlinkTest COMMAND RoukeMavlinkTest)
