# Opt-in experiment: normal release builds stay offline and keep the v0.1.0 path.
set(BESKTOP_OZZ_SOURCE_DIR "" CACHE PATH "Pinned ozz-animation 0.16.0 source directory")
set(BESKTOP_ANIMATION_ASSETS "${CMAKE_SOURCE_DIR}/out/animation-foundation/assets" CACHE PATH "Offline-generated animation assets")
if(NOT EXISTS "${BESKTOP_ANIMATION_ASSETS}/foundation.baf")
    message(FATAL_ERROR "Build animation assets first: tools/animation/prepare.ps1")
endif()
if(NOT BESKTOP_OZZ_SOURCE_DIR)
    include(FetchContent)
    FetchContent_Declare(besktop_ozz
        URL https://codeload.github.com/guillaumeblanc/ozz-animation/zip/refs/tags/0.16.0
        URL_HASH SHA256=f5312159da1d2ca0acbe62d94521d32eb1cb4435791400e8d9ab317e8a29bfd9)
    FetchContent_GetProperties(besktop_ozz)
    if(NOT besktop_ozz_POPULATED)
        FetchContent_Populate(besktop_ozz)
    endif()
    set(BESKTOP_OZZ_SOURCE_DIR "${besktop_ozz_SOURCE_DIR}")
endif()
set(ozz_build_tools OFF CACHE BOOL "" FORCE)
set(ozz_build_samples OFF CACHE BOOL "" FORCE)
set(ozz_build_howtos OFF CACHE BOOL "" FORCE)
set(ozz_build_tests OFF CACHE BOOL "" FORCE)
set(ozz_build_postfix OFF CACHE BOOL "" FORCE)
set(ozz_build_msvc_rt_dll OFF CACHE BOOL "" FORCE)
add_subdirectory("${BESKTOP_OZZ_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/ozz" EXCLUDE_FROM_ALL)

add_library(besktop_anim3d STATIC src/besktop/anim3d/runtime.cpp)
target_include_directories(besktop_anim3d PUBLIC src)
target_compile_features(besktop_anim3d PUBLIC cxx_std_20)
target_compile_definitions(besktop_anim3d PUBLIC UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(besktop_anim3d PUBLIC ozz_animation besktop_runtime)
add_library(besktop_anim3d_app STATIC
    src/besktop/anim3d/renderer.cpp
    src/besktop/anim3d/app.cpp
    src/besktop/app/runtime_options.cpp
    src/besktop/desktop/desktop_snapshot.cpp
    src/besktop/render/icon_image_cache.cpp
    src/besktop/render/wallpaper_renderer.cpp
    src/besktop/render/taskbar_renderer.cpp
    src/besktop/logging/logger.cpp)
target_link_libraries(besktop_anim3d_app PUBLIC besktop_anim3d besktop_animation d3d11 dxgi d3dcompiler windowscodecs Comctl32 Gdi32 Gdiplus Ole32 Shell32 User32 Psapi)

configure_file("${CMAKE_SOURCE_DIR}/src/besktop/anim3d/resources.rc.in"
    "${CMAKE_BINARY_DIR}/generated/animation_resources.rc" @ONLY)
add_executable(animation_lab WIN32 tools/animation_lab_main.cpp "${CMAKE_BINARY_DIR}/generated/animation_resources.rc")
target_link_libraries(animation_lab PRIVATE besktop_anim3d_app)
set_target_properties(animation_lab PROPERTIES OUTPUT_NAME "AnimationLab")
target_sources(besktop PRIVATE "${CMAKE_BINARY_DIR}/generated/animation_resources.rc")
target_link_libraries(besktop PRIVATE besktop_anim3d_app)
target_compile_definitions(besktop PRIVATE BESKTOP_ANIMATION_FOUNDATION=1)

add_executable(besktop_anim3d_tests tests/anim3d_tests.cpp)
target_link_libraries(besktop_anim3d_tests PRIVATE besktop_anim3d)
add_test(NAME besktop_anim3d_tests COMMAND besktop_anim3d_tests "${BESKTOP_ANIMATION_ASSETS}")
foreach(target IN ITEMS besktop_anim3d besktop_anim3d_app animation_lab besktop_anim3d_tests)
    target_compile_options(${target} PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
    set_property(TARGET ${target} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endforeach()
