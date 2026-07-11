cmake_minimum_required(VERSION 3.24)

project(Besktop VERSION 0.1.0 LANGUAGES CXX RC)

set(BESKTOP_BASIC_PACK_PATH
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/packs/icon-fight-basic.bpack")
set(BESKTOP_APP_ICON_PATH
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/app/besktop.ico")
set(BESKTOP_GENERATED_RESOURCE_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(BESKTOP_GENERATED_RESOURCE_FILE
    "${BESKTOP_GENERATED_RESOURCE_DIR}/besktop_resources.rc")
set(BESKTOP_GENERATED_VERSION_RESOURCE_FILE
    "${BESKTOP_GENERATED_RESOURCE_DIR}/besktop_version.rc")
set(BESKTOP_GENERATED_INCLUDE_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/generated/include")

set(BESKTOP_PRODUCT_NAME "Besktop")
set(BESKTOP_PLAY_MODE "Icon Fight")
set(BESKTOP_VERSION_FILE "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}.0")

file(MAKE_DIRECTORY "${BESKTOP_GENERATED_RESOURCE_DIR}")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/besktop/resources/besktop_resources.rc.in"
    "${BESKTOP_GENERATED_RESOURCE_FILE}"
    @ONLY)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/besktop/version.h.in"
    "${BESKTOP_GENERATED_INCLUDE_DIR}/besktop/version.h"
    @ONLY)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/besktop/resources/besktop_version.rc.in"
    "${BESKTOP_GENERATED_VERSION_RESOURCE_FILE}"
    @ONLY)

add_library(besktop_runtime STATIC
    src/besktop/core/json_value.cpp
    src/besktop/core/mvp_runtime.cpp
    src/besktop/core/pack_framework.cpp
)

target_compile_features(besktop_runtime PUBLIC cxx_std_20)

target_include_directories(besktop_runtime PUBLIC
    "${BESKTOP_GENERATED_INCLUDE_DIR}"
    src
)

target_compile_definitions(besktop_runtime PUBLIC
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

add_executable(besktop WIN32
    src/besktop/animation/icon_fight_scene.cpp
    src/besktop/app/runtime_options.cpp
    src/besktop/app/stage_window.cpp
    src/besktop/desktop/desktop_snapshot.cpp
    src/besktop/logging/logger.cpp
    src/besktop/render/icon_image_cache.cpp
    src/besktop/render/taskbar_renderer.cpp
    src/besktop/render/wallpaper_renderer.cpp
    src/main.cpp
    "${BESKTOP_GENERATED_RESOURCE_FILE}"
    "${BESKTOP_GENERATED_VERSION_RESOURCE_FILE}"
)

target_compile_definitions(besktop PRIVATE
    $<$<CONFIG:Debug>:BESKTOP_DEVELOPER_BUILD=1>
)

target_link_libraries(besktop PRIVATE Comctl32 Gdi32 Gdiplus Ole32 Shell32 User32)

add_executable(besktop_mvp_cli
    tools/mvp_cli.cpp
    "${BESKTOP_GENERATED_RESOURCE_FILE}"
)

foreach(target_name IN ITEMS besktop besktop_mvp_cli)
    target_link_libraries(${target_name} PRIVATE besktop_runtime)

    if(MSVC)
        set_property(TARGET ${target_name} PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()

    if(BESKTOP_EXTRA_RC)
        target_sources(${target_name} PRIVATE "${BESKTOP_EXTRA_RC}")
    endif()
endforeach()

if(MSVC)
    set_property(TARGET besktop_runtime PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
