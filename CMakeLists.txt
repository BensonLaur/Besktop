cmake_minimum_required(VERSION 3.24)

project(Besktop LANGUAGES CXX RC)

set(BESKTOP_BASIC_PACK_PATH
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/packs/icon-fight-basic.bpack")
set(BESKTOP_GENERATED_RESOURCE_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(BESKTOP_GENERATED_RESOURCE_FILE
    "${BESKTOP_GENERATED_RESOURCE_DIR}/besktop_resources.rc")

file(MAKE_DIRECTORY "${BESKTOP_GENERATED_RESOURCE_DIR}")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/besktop/resources/besktop_resources.rc.in"
    "${BESKTOP_GENERATED_RESOURCE_FILE}"
    @ONLY)

add_library(besktop_runtime STATIC
    src/besktop/core/json_value.cpp
    src/besktop/core/mvp_runtime.cpp
    src/besktop/core/pack_framework.cpp
)

target_compile_features(besktop_runtime PUBLIC cxx_std_20)

target_include_directories(besktop_runtime PUBLIC
    src
)

target_compile_definitions(besktop_runtime PUBLIC
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

add_executable(besktop WIN32
    src/besktop/app/stage_window.cpp
    src/besktop/desktop/desktop_snapshot.cpp
    src/main.cpp
    "${BESKTOP_GENERATED_RESOURCE_FILE}"
)

target_link_libraries(besktop PRIVATE Gdi32 Ole32 Shell32 User32)

add_executable(besktop_mvp_cli
    tools/mvp_cli.cpp
    "${BESKTOP_GENERATED_RESOURCE_FILE}"
)

foreach(target_name IN ITEMS besktop besktop_mvp_cli)
    target_link_libraries(${target_name} PRIVATE besktop_runtime)

    if(BESKTOP_EXTRA_RC)
        target_sources(${target_name} PRIVATE "${BESKTOP_EXTRA_RC}")
    endif()
endforeach()
