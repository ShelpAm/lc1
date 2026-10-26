cmake_minimum_required(VERSION 3.30)
project(lc1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# CMake warns for `FetchContent` and asks for this.
set(DOWNLOAD_EXTRACT_TIMESTAMP ON)

# clangd reads a compilation database, and .clangd points at this tree's; without this
# the file is only produced when someone remembers to pass -DCMAKE_EXPORT_COMPILE_COMMANDS,
# which is exactly the kind of thing that silently rots. Every build tree gets its own.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CheckCXXCompilerFlag)
include(CheckLinkerFlag)
include(CheckCXXSourceCompiles)

# clang-cl reports compiler ID Clang but accepts the MSVC command-line dialect.
# Probe the chosen flags rather than assuming every compiler in a family supports them.
if(MSVC OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    set(lc1_warning_candidates /W4)
else()
    set(lc1_warning_candidates -Wall -Wextra -Wpedantic)
endif()
set(lc1_warning_options)
foreach(flag IN LISTS lc1_warning_candidates)
    string(MAKE_C_IDENTIFIER "${flag}" flag_id)
    check_cxx_compiler_flag("${flag}" lc1_supports_${flag_id})
    if(lc1_supports_${flag_id})
        list(APPEND lc1_warning_options "${flag}")
    endif()
endforeach()

function(lc1_enable_warnings target)
    target_compile_options(${target} PRIVATE ${lc1_warning_options})
endfunction()

# AddressSanitizer, off by default: it costs real runtime and memory, which is the wrong
# default for an ordinary build. Probed rather than assumed -- the MinGW cross compiler
# here has no sanitizer runtime installed at all, so a clear configure-time failure beats
# an unexplained link error.
#
# check_cxx_source_compiles, not check_cxx_compiler_flag: the warning flags above only
# need the compile line, while -fsanitize=address must reach the link line too, and
# check_cxx_compiler_flag passes its flag to the compile line alone. Against clang that
# check fails on its own missing link flag -- "undefined reference to __asan_init" --
# reporting no sanitizer support for a compiler that has it.
option(lc1_enable_asan "Build with AddressSanitizer" OFF)
if(lc1_enable_asan)
    set(CMAKE_REQUIRED_FLAGS -fsanitize=address)
    check_cxx_source_compiles("int main() { return 0; }" lc1_supports_asan)
    unset(CMAKE_REQUIRED_FLAGS)
    if(NOT lc1_supports_asan)
        message(FATAL_ERROR
            "lc1_enable_asan is ON, but ${CMAKE_CXX_COMPILER} cannot compile and link with -fsanitize=address")
    endif()
    # Global options rather than a per-target function like lc1_enable_warnings: ASan
    # instruments every object that ends up in the executable, the imgui backends and
    # each test target included, so a target list kept here would rot the next time a
    # target is added. The compile option instruments; the link option is what pulls the
    # runtime in, and without it the instrumented objects fail to link. Frame pointers
    # keep ASan's stack traces readable above -O0, where the compiler drops them.
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
endif()

# Static MinGW runtimes are required for standalone Windows executables.
# Fail during configuration if the selected linker cannot provide them.
if(MINGW)
    check_linker_flag(CXX "-static" lc1_supports_static_link)
    if(NOT lc1_supports_static_link)
        message(FATAL_ERROR "The MinGW target requires a linker supporting -static")
    endif()
endif()

add_subdirectory(third-party)

# All dependencies but VMA-Hpp (see third-party/) come from conan, so the build is identical on Linux and
# Windows. GLFW must NOT be built with a static Vulkan loader -- see the comment
# at the bottom of conanfile.py for why nothing here links a loader at all.
find_package(glfw3 CONFIG REQUIRED)
find_package(VulkanHeaders CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(stb CONFIG REQUIRED)
FetchContent_MakeAvailable(vmahpp)
find_package(tinyobjloader CONFIG REQUIRED)
find_package(TinyGLTF CONFIG REQUIRED)
find_package(Ktx CONFIG REQUIRED)
find_package(mikktspace CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(reflectcpp CONFIG REQUIRED)
find_package(freetype CONFIG REQUIRED)
find_package(OpenAL CONFIG REQUIRED)
find_package(drmp3 CONFIG REQUIRED)
find_package(drwav CONFIG REQUIRED)

add_subdirectory(shaders)

# lc1_engine: the reusable library. Public headers live under include/lc1/
# and are included as "lc1/..."; src/ holds sources and private headers
# (e.g. vk/render/vertex-input.hpp) that the app cannot see.
add_library(lc1_engine STATIC
    src/window.cpp
    src/input.cpp
    src/error.cpp
    src/image.cpp
    src/vertex.cpp
    src/tangent-space.cpp
    src/resource-manager.cpp

    # Scene data, model loading and per-instance poses.
    src/scene/animation.cpp
    src/scene/camera.cpp
    src/scene/lights/directional-light.cpp
    src/scene/lights/point-light.cpp
    src/scene/lights/sphere-light.cpp
    src/scene/lights/spot-light.cpp
    src/scene/mesh.cpp
    src/scene/model-definition.cpp
    src/scene/model-instance.cpp
    src/scene/model-node.cpp
    src/scene/model.cpp
    src/scene/object.cpp
    src/scene/scene.cpp
    src/scene/transform.cpp

    # Text rasterization and layout; FreeType only, no Vulkan.
    src/text/glyph-atlas.cpp
    src/text/text-layout.cpp
    src/text/utf8.cpp

    # Audio
    src/audio/audio.cpp

    # Vulkan setup and dispatch.
    src/vk/core/common.cpp
    src/vk/core/device.cpp
    src/vk/core/instance.cpp
    src/vk/core/loader.cpp
    src/vk/core/memory.cpp
    src/vk/core/physical-device.cpp

    # GPU storage, uploads and binding primitives.
    src/vk/resources/acceleration-structure.cpp
    src/vk/resources/command-buffer.cpp
    src/vk/resources/descriptor-heap.cpp
    src/vk/resources/descriptor-heap-cache.cpp
    src/vk/resources/gpu-buffer.cpp
    src/vk/resources/gpu-font.cpp
    src/vk/resources/gpu-image.cpp
    src/vk/resources/gpu-mesh.cpp
    src/vk/resources/gpu-texture.cpp
    src/vk/resources/image-barrier.cpp
    src/vk/resources/shader-object.cpp
    src/vk/resources/sampler.cpp

    # Window surfaces, swapchains and presentation policy.
    src/vk/presentation/surface.cpp
    src/vk/presentation/swapchain-policy.cpp
    src/vk/presentation/swapchain.cpp

    # Concrete lighting, shadow and output passes.
    src/vk/render/draw-validation.cpp
    src/vk/render/frame-resources.cpp
    src/vk/render/render-graph.cpp
    src/vk/render/graphics-shaders.cpp
    src/vk/render/graphics-state.cpp
    src/vk/render/material.cpp
    src/vk/render/ray-query-shadows.cpp
    src/vk/render/renderer.cpp
    src/vk/render/temporal-shadows.cpp
    src/vk/render/text-pass.cpp
    src/vk/render/tone-mapping.cpp

    src/stb-image.cpp
    src/tiny-gltf.cpp
    src/game-clock.cpp
)

target_include_directories(lc1_engine
    PUBLIC  include
    PRIVATE src
)

target_link_libraries(lc1_engine PUBLIC
    glfw
    vulkan-headers::vulkan-headers
    glm::glm
    VulkanMemoryAllocator-Hpp::VulkanMemoryAllocator-Hpp # VMA-Hpp
    spdlog::spdlog
    stb::stb
    tinyobjloader::tinyobjloader
    TinyGLTF::TinyGLTF
    KTX::ktx
    reflectcpp::reflectcpp
    Freetype::Freetype
    OpenAL::OpenAL
    drmp3::drmp3
    drwav::drwav
)
target_link_libraries(lc1_engine PRIVATE
    mikktspace::mikktspace
)

# Conan's MinGW KTX package installs its ASTC encoder as a separate archive,
# but KTX::ktx omits it from the link interface. Its package lib directory is
# already propagated by that target; keep the companion archive after KTX.
if(MINGW)
    target_link_libraries(lc1_engine PRIVATE astcenc-avx2-static)
endif()

target_compile_definitions(lc1_engine PUBLIC
    GLM_FORCE_DEPTH_ZERO_TO_ONE # Force depth from glm in range of [0, 1], which
                                # is required by Vulkan.
    GLM_ENABLE_EXPERIMENTAL # Introduces glm::gtx
)

# GCC puts the non-inline half of <print> in its own library, libstdc++exp -- including
# the terminal helpers vprint_unicode calls, std::__open_terminal and
# std::__write_to_terminal. Linux needs none of this spelled out because libstdc++.so.6
# exports them already, which is why the native build links without it; MinGW's
# libstdc++.dll.a does not, and the Windows link then fails with
#   undefined reference to `std::__open_terminal(_iobuf*)'
# after every object has compiled cleanly -- a linker error that looks like a missing
# implementation of a standard function and is really a missing -l. std::println is used
# by the library (window.cpp, device.cpp, instance.cpp, swapchain.cpp), not only by the
# app, so the requirement belongs to lc1_engine and travels with it.
if(MINGW)
    target_link_libraries(lc1_engine INTERFACE stdc++exp)
endif()

# Game rules, world resources and camera behavior depend on the engine. The list is
# grouped by gameplay feature, mirroring the game/ directory, so a new file has an
# obvious home instead of being appended to one flat list.
# The engine does not link back to the game or application.
add_library(lc1_game STATIC
    src/game/actions.cpp

    src/game/character/character-model.cpp
    src/game/character/character.cpp
    src/game/character/character-controller.cpp
    src/game/character/bag.cpp
    src/game/character/equipment.cpp
    src/game/character/weapon.cpp
    src/game/character/ai-character-controller.cpp
    src/game/player/player-controller.cpp
    src/game/player/player-view.cpp
    src/game/camera/map-camera-controller.cpp

    src/game/continent/continent.cpp
    src/game/continent/prototype.cpp
    src/game/continent/present.cpp
    src/game/continent/patrol-house.cpp
    src/game/continent/layout.cpp
    src/game/continent/view.cpp

    src/game/party/party.cpp
    src/game/party/faction.cpp

    src/game/resources/crafting.cpp
    src/game/resources/recipe.cpp
    src/game/resources/resource.cpp
    src/game/dialog/dialog.cpp

    src/game/world/game-world.cpp
    src/game/world/world-presentation.cpp
    src/game/world/interactable.cpp

    src/game/session/game-session.cpp
    src/game/session/save.cpp

    src/game/ui/game-ui.cpp
    src/game/ui/music-browser.cpp

    src/game/debug/pbr-showcase.cpp
)
target_link_libraries(lc1_game PUBLIC lc1_engine imgui::imgui)

# app: the game executable. Output name stays lc1 so the Makefile works.
add_executable(lc1
    app/main.cpp
    app/application.cpp
)
target_link_libraries(lc1 PRIVATE lc1_game)
add_imgui_backends(TARGET lc1 BACKENDS vulkan glfw DEPS
    vulkan-headers::vulkan-headers glfw)

# A MinGW link pulls the compiler's own runtime in as DLLs -- libstdc++-6.dll,
# libgcc_s_seh-1.dll and libwinpthread-1.dll -- which are present on the machine that
# compiled it and absent everywhere the exe is actually meant to run. Nothing else in
# the link is affected: the Conan dependency graph already uses static libraries here,
# and the Vulkan loader is dlopen'd at runtime rather than linked. -static covers all
# three at once (-static-libgcc/-static-libstdc++ alone would leave winpthread).
#
# This lives here rather than in profiles/mingw64's tools.build:exelinkflags because
# CMAKE_EXE_LINKER_FLAGS_INIT is read only on a tree's *first* configure: a profile
# change silently does nothing to an existing build tree, and the resulting exe looks
# fine until it is copied to another machine.
if(MINGW)
    target_link_options(lc1 PRIVATE -static)
endif()

# The .spv files are loaded at runtime, so `--target lc1` must build them too.
# Keep the runtime tree relocatable. Copy on each build so edited assets are refreshed.
add_custom_target(copy_assets
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/runtime/assets" "${CMAKE_CURRENT_BINARY_DIR}/runtime/assets"
    VERBATIM
)
# Dialogue scripts are authored content and refresh on each build.
add_custom_target(copy_dialogs
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/runtime/dialogs" "${CMAKE_CURRENT_BINARY_DIR}/runtime/dialogs"
    VERBATIM
)
# Refresh authored seed files on each build; checkpoints remain runtime-owned.
add_custom_target(copy_saves
    COMMAND ${CMAKE_COMMAND}
        "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/runtime/saves"
        "-DDESTINATION_DIR=${CMAKE_CURRENT_BINARY_DIR}/runtime/saves"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/scripts/seed-saves.cmake"
    VERBATIM
)
add_dependencies(lc1 shaders copy_assets copy_saves copy_dialogs)

include(CTest)
if(BUILD_TESTING)
    find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
    add_test(NAME descriptor_heap_shader_interfaces
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/check_heap_shaders.py"
            --sample-shading "${PROJECT_BINARY_DIR}/shaders/shader.spv"
            "${PROJECT_BINARY_DIR}/shaders/shader.spv"
            "${PROJECT_BINARY_DIR}/shaders/visibility.spv"
            "${PROJECT_BINARY_DIR}/shaders/temporal-shadows.spv"
            "${PROJECT_BINARY_DIR}/shaders/spatial-shadows.spv"
            "${PROJECT_BINARY_DIR}/shaders/tone-map.spv"
            "${PROJECT_BINARY_DIR}/shaders/text.spv")
    add_test(NAME heap_struct_generator
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/tests/test_heap_struct_generator.py"
                --slangc "${SLANGC_EXECUTABLE}")
    add_test(NAME seed_save_refresh
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/test_seed_saves.py"
                --cmake "${CMAKE_COMMAND}"
                --script "${PROJECT_SOURCE_DIR}/scripts/seed-saves.cmake")
    add_executable(lc1_error_tests tests/error.cpp src/error.cpp)
    target_include_directories(lc1_error_tests PRIVATE "${PROJECT_SOURCE_DIR}/include")
    lc1_enable_warnings(lc1_error_tests)
    add_test(NAME error_diagnostics COMMAND lc1_error_tests)
    add_executable(lc1_input_tests tests/input.cpp)
    target_link_libraries(lc1_input_tests PRIVATE lc1_engine)
    lc1_enable_warnings(lc1_input_tests)
    if(MINGW)
        target_link_options(lc1_input_tests PRIVATE -static)
    endif()
    add_test(NAME input_routing COMMAND lc1_input_tests)
    add_executable(lc1_world_map_tests tests/world-map.cpp)
    target_link_libraries(lc1_world_map_tests PRIVATE lc1_game)
    lc1_enable_warnings(lc1_world_map_tests)
    add_executable(lc1_continent_tests tests/continent.cpp)
    target_link_libraries(lc1_continent_tests PRIVATE lc1_game)
    lc1_enable_warnings(lc1_continent_tests)
    add_executable(lc1_character_tests tests/character.cpp)
    target_link_libraries(lc1_character_tests PRIVATE lc1_game)
    lc1_enable_warnings(lc1_character_tests)
    add_dependencies(lc1_character_tests copy_dialogs)
    add_executable(lc1_dialog_tests tests/dialog.cpp)
    target_link_libraries(lc1_dialog_tests PRIVATE lc1_game)
    lc1_enable_warnings(lc1_dialog_tests)
    add_dependencies(lc1_dialog_tests copy_dialogs)
    # Save tests use immutable fixtures, independent of the editable runtime saves.
    add_executable(lc1_save_tests tests/save.cpp)
    target_link_libraries(lc1_save_tests PRIVATE lc1_game)
    lc1_enable_warnings(lc1_save_tests)
    add_test(NAME save_data
        COMMAND lc1_save_tests
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/tests/fixtures")
    add_test(NAME runtime_save_data
        COMMAND lc1_save_tests runtime/saves/01
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    add_dependencies(lc1_save_tests copy_saves)
    add_executable(lc1_model_animation_tests tests/model-animation.cpp)
    target_link_libraries(lc1_model_animation_tests PRIVATE lc1_game)
    lc1_enable_warnings(lc1_model_animation_tests)
    if(MINGW)
        target_link_options(lc1_model_animation_tests PRIVATE -static)
    endif()
    add_test(NAME model_animation COMMAND lc1_model_animation_tests)
    add_executable(lc1_model_tests tests/model.cpp)
    target_link_libraries(lc1_model_tests PRIVATE lc1_engine)
    lc1_enable_warnings(lc1_model_tests)
    if(MINGW)
        target_link_options(lc1_model_tests PRIVATE -static)
    endif()
    add_test(NAME model COMMAND lc1_model_tests)
    add_executable(lc1_tangent_tests tests/tangent-space.cpp)
    target_link_libraries(lc1_tangent_tests PRIVATE lc1_engine)
    lc1_enable_warnings(lc1_tangent_tests)
    add_executable(lc1_game_world_tests tests/game-world.cpp)
    target_link_libraries(lc1_game_world_tests PRIVATE lc1_game)
    lc1_enable_warnings(lc1_game_world_tests)
    add_test(NAME game_world COMMAND lc1_game_world_tests
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/tests/fixtures")
    add_executable(lc1_player_view_tests tests/player-view.cpp)
    target_link_libraries(lc1_player_view_tests PRIVATE lc1_game)
    lc1_enable_warnings(lc1_player_view_tests)
    add_executable(lc1_render_graph_tests tests/render-graph.cpp)
    target_link_libraries(lc1_render_graph_tests PRIVATE lc1_engine)
    lc1_enable_warnings(lc1_render_graph_tests)
    # The font test reads runtime/assets/fonts like the game does, and skips a face that is
    # absent (runtime/assets/ is not tracked in this repository).
    add_executable(lc1_font_tests tests/font.cpp)
    target_link_libraries(lc1_font_tests PRIVATE lc1_engine)
    lc1_enable_warnings(lc1_font_tests)
    add_test(NAME font_layout
        COMMAND lc1_font_tests
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    add_dependencies(lc1_font_tests copy_assets)
    if(MINGW)
        target_link_options(lc1_error_tests PRIVATE -static)
        target_link_options(lc1_world_map_tests PRIVATE -static)
        target_link_options(lc1_continent_tests PRIVATE -static)
        target_link_options(lc1_character_tests PRIVATE -static)
        target_link_options(lc1_dialog_tests PRIVATE -static)
        target_link_options(lc1_save_tests PRIVATE -static)
        target_link_options(lc1_tangent_tests PRIVATE -static)
        target_link_options(lc1_game_world_tests PRIVATE -static)
        target_link_options(lc1_player_view_tests PRIVATE -static)
        target_link_options(lc1_render_graph_tests PRIVATE -static)
        target_link_options(lc1_font_tests PRIVATE -static)
    endif()
    if(NOT CMAKE_CROSSCOMPILING)
        add_executable(lc1_descriptor_heap_tests EXCLUDE_FROM_ALL tests/descriptor-heap.cpp)
        target_link_libraries(lc1_descriptor_heap_tests PRIVATE lc1_engine)
        lc1_enable_warnings(lc1_descriptor_heap_tests)
        add_dependencies(lc1_descriptor_heap_tests descriptor_heap_test_shader)
        add_executable(lc1_descriptor_heap_bulk_tests EXCLUDE_FROM_ALL tests/descriptor-heap-bulk.cpp)
        target_link_libraries(lc1_descriptor_heap_bulk_tests PRIVATE lc1_engine)
        lc1_enable_warnings(lc1_descriptor_heap_bulk_tests)
        add_dependencies(lc1_descriptor_heap_bulk_tests descriptor_heap_bulk_test_shader)
        # Explicit GPU integration target: needs a display/GPU and the Conan run env.
        # Kept out of ordinary CPU-only CTest runs.
        add_executable(lc1_temporal_shadow_tests EXCLUDE_FROM_ALL tests/temporal-shadows.cpp)
        target_link_libraries(lc1_temporal_shadow_tests PRIVATE lc1_engine)
        lc1_enable_warnings(lc1_temporal_shadow_tests)
        add_dependencies(lc1_temporal_shadow_tests shaders)
        add_executable(lc1_shadow_path_tests EXCLUDE_FROM_ALL tests/shadow-paths.cpp)
        target_link_libraries(lc1_shadow_path_tests PRIVATE lc1_engine)
        lc1_enable_warnings(lc1_shadow_path_tests)
        add_dependencies(lc1_shadow_path_tests shaders copy_assets)
        add_executable(lc1_pbr_tests EXCLUDE_FROM_ALL tests/pbr.cpp)
        target_link_libraries(lc1_pbr_tests PRIVATE lc1_game)
        lc1_enable_warnings(lc1_pbr_tests)
        add_dependencies(lc1_pbr_tests shaders copy_assets)
        add_test(NAME world_map COMMAND lc1_world_map_tests)
        add_test(NAME continent COMMAND lc1_continent_tests)
        add_test(NAME character COMMAND lc1_character_tests)
        add_test(NAME dialog COMMAND lc1_dialog_tests
            "${PROJECT_SOURCE_DIR}/tests/fixtures/dialogs/catalog.yaml")
        add_test(NAME runtime_dialog_data COMMAND lc1_dialog_tests
            "${PROJECT_BINARY_DIR}/runtime/dialogs/catalog.yaml" --validate)
        add_test(NAME tangent_space COMMAND lc1_tangent_tests)
        add_test(NAME player_view COMMAND lc1_player_view_tests)
        add_test(NAME render_graph COMMAND lc1_render_graph_tests)
    endif()
endif()

# Apply the same checked warning policy to both libraries and the application.
foreach(target lc1_engine lc1_game lc1)
    lc1_enable_warnings(${target})
endforeach()
