target_sources(${CMAKE_PROJECT_NAME}
    PRIVATE
        EventHandler.cc
        EventHandler.h
        HealthAndArmingCheckReport.cc
        HealthAndArmingCheckReport.h
        logging.cpp
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

#===========================================================================#

CPMAddPackage(
    NAME libevents
    GITHUB_REPOSITORY mavlink/libevents
    # Keep the event protocol compatible with the pinned MAVLink C library.
    GIT_TAG 077677a7d2f48f0af0eb8a4e4a7b6310956728bc
    SOURCE_SUBDIR libs/cpp
)

target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE libevents)
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${libevents_SOURCE_DIR}/libs/cpp)
