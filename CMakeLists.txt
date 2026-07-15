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

add_library(besktop_animation STATIC
    src/besktop/animation/active_encounter_pool.cpp
    src/besktop/animation/action_clip.cpp
    src/besktop/animation/action_player.cpp
    src/besktop/animation/actor_ecosystem.cpp
    src/besktop/animation/awakening_director.cpp
    src/besktop/animation/combat_pair.cpp
    src/besktop/animation/combat_director.cpp
    src/besktop/animation/encounter_arbiter.cpp
    src/besktop/animation/encounter_director.cpp
    src/besktop/animation/gait_ik.cpp
    src/besktop/animation/turn_motion.cpp
)

target_compile_features(besktop_animation PUBLIC cxx_std_20)
target_include_directories(besktop_animation PUBLIC src)

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
target_link_libraries(besktop PRIVATE besktop_animation)

add_executable(besktop_mvp_cli
    tools/mvp_cli.cpp
    "${BESKTOP_GENERATED_RESOURCE_FILE}"
)

add_executable(besktop_action_tests
    tests/action_player_tests.cpp
)
target_link_libraries(besktop_action_tests PRIVATE besktop_animation)

add_executable(besktop_combat_tests
    tests/combat_pair_tests.cpp
)
target_link_libraries(besktop_combat_tests PRIVATE besktop_animation)

add_executable(besktop_combat_director_tests
    tests/combat_director_tests.cpp
)
target_link_libraries(besktop_combat_director_tests PRIVATE besktop_animation)

add_executable(besktop_runtime_options_tests
    tests/runtime_options_tests.cpp
    src/besktop/app/runtime_options.cpp
)
target_link_libraries(besktop_runtime_options_tests PRIVATE besktop_animation)

add_executable(besktop_awakening_tests
    tests/awakening_director_tests.cpp
)
target_link_libraries(besktop_awakening_tests PRIVATE besktop_animation)

add_executable(besktop_encounter_tests
    tests/encounter_director_tests.cpp
)
target_link_libraries(besktop_encounter_tests PRIVATE besktop_animation)

add_executable(besktop_ecosystem_tests
    tests/ecosystem_decision_tests.cpp
)
target_link_libraries(besktop_ecosystem_tests PRIVATE besktop_animation)

add_executable(besktop_active_encounter_pool_tests
    tests/active_encounter_pool_tests.cpp
)
target_link_libraries(besktop_active_encounter_pool_tests PRIVATE besktop_animation)

enable_testing()
add_test(NAME besktop_action_tests COMMAND besktop_action_tests)
add_test(NAME besktop_combat_tests COMMAND besktop_combat_tests)
add_test(NAME besktop_combat_director_tests COMMAND besktop_combat_director_tests)
add_test(NAME besktop_runtime_options_tests COMMAND besktop_runtime_options_tests)
add_test(NAME besktop_awakening_tests COMMAND besktop_awakening_tests)
add_test(NAME besktop_encounter_tests COMMAND besktop_encounter_tests)
add_test(NAME besktop_ecosystem_tests COMMAND besktop_ecosystem_tests)
add_test(NAME besktop_active_encounter_pool_tests COMMAND besktop_active_encounter_pool_tests)

# Some localized MSVC + NMake environments do not produce usable /showIncludes
# dependency files. Keep project headers as explicit object dependencies so an
# incremental build cannot link translation units compiled against different
# class layouts.
if(MSVC AND CMAKE_GENERATOR STREQUAL "NMake Makefiles")
    file(GLOB_RECURSE BESKTOP_CORE_HEADERS CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.inl")
    foreach(target_name IN ITEMS
            besktop_runtime
            besktop_animation
            besktop
            besktop_mvp_cli
            besktop_action_tests
            besktop_combat_tests
            besktop_combat_director_tests
            besktop_runtime_options_tests
            besktop_awakening_tests
            besktop_encounter_tests
            besktop_ecosystem_tests
            besktop_active_encounter_pool_tests)
        get_target_property(target_sources ${target_name} SOURCES)
        foreach(source_file IN LISTS target_sources)
            if(source_file MATCHES "\\.(cc|cpp|cxx)$")
                set_property(SOURCE "${source_file}" APPEND PROPERTY
                    OBJECT_DEPENDS "${BESKTOP_CORE_HEADERS}")
            endif()
        endforeach()
    endforeach()
endif()

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
    set_property(TARGET besktop_animation PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set_property(TARGET besktop_action_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set_property(TARGET besktop_combat_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set_property(TARGET besktop_combat_director_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set_property(TARGET besktop_runtime_options_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set_property(TARGET besktop_awakening_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set_property(TARGET besktop_encounter_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set_property(TARGET besktop_ecosystem_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set_property(TARGET besktop_active_encounter_pool_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
