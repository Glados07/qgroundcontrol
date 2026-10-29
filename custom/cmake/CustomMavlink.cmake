include_guard(GLOBAL)

# Assemble a build-local header tree. All upstream headers remain byte-for-byte
# unchanged; only the generated Rouke dialect is added beside them.
function(qgc_prepare_rouke_mavlink upstream_dir output_dir result)
    set(_custom_root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/..")
    set(_dialect "${_custom_root}/src/MAVLink/generated/rouke")
    set(_xml "${_custom_root}/src/MAVLink/rouke.xml")
    set(_manifest "${_dialect}/generation.json")
    file(READ "${_manifest}" _generation)
    string(JSON _expected_xml GET "${_generation}" xml_sha256)
    string(JSON _expected_upstream GET "${_generation}" upstream_all_sha256)
    foreach(_check IN ITEMS xml upstream)
        if(_check STREQUAL "xml")
            set(_file "${_xml}")
        else()
            set(_file "${upstream_dir}/all/all.h")
        endif()
        file(READ "${_file}" _contents)
        string(REPLACE "\r\n" "\n" _contents "${_contents}")
        string(SHA256 _hash "${_contents}")
        if(NOT _hash STREQUAL _expected_${_check})
            message(FATAL_ERROR
                "Rouke MAVLink ${_check} changed. Regenerate with custom/tools/generate_rouke_mavlink.py before building.")
        endif()
    endforeach()

    file(GLOB _dialect_headers CONFIGURE_DEPENDS "${_dialect}/*.h")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${_xml}" "${_manifest}" ${_dialect_headers} "${upstream_dir}/all/all.h")
    file(MAKE_DIRECTORY "${output_dir}")
    file(COPY "${upstream_dir}/" DESTINATION "${output_dir}"
        FILES_MATCHING PATTERN "*.h" PATTERN ".git" EXCLUDE)
    file(MAKE_DIRECTORY "${output_dir}/rouke")
    file(COPY ${_dialect_headers} DESTINATION "${output_dir}/rouke")
    # Both are necessary: MAVLinkLib.h includes <mavlink_types.h> before the
    # dialect. It must resolve to the same physical file as ../protocol.h uses
    # (the upstream type header relies on #pragma once, not an include guard).
    set(${result} "${output_dir}/rouke;${output_dir}" PARENT_SCOPE)
endfunction()

function(_qgc_apply_rouke_mavlink_to_app)
    get_property(_include_dir GLOBAL PROPERTY QGC_ROUKE_MAVLINK_INCLUDE_DIR)
    # Deferred until root CMake has created the application and added native
    # include directories. This makes <mavlink.h> resolve to Rouke everywhere,
    # including the native parser and inspector, not just the payload manager.
    target_include_directories(${CMAKE_PROJECT_NAME} BEFORE PRIVATE ${_include_dir})
endfunction()

function(qgc_enable_rouke_mavlink)
    CPMAddPackage(
        NAME mavlink
        GIT_REPOSITORY ${QGC_MAVLINK_GIT_REPO}
        GIT_TAG ${QGC_MAVLINK_GIT_TAG}
    )
    qgc_prepare_rouke_mavlink("${mavlink_SOURCE_DIR}"
        "${CMAKE_CURRENT_BINARY_DIR}/mavlink" _include_dir)
    set_property(GLOBAL PROPERTY QGC_ROUKE_MAVLINK_INCLUDE_DIR "${_include_dir}")
    set(QGC_ROUKE_MAVLINK_INCLUDE_DIR "${_include_dir}" CACHE INTERNAL "Rouke MAVLink dialect" FORCE)
    cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL _qgc_apply_rouke_mavlink_to_app)
endfunction()
