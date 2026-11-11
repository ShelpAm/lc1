# Overridable: -DSLANGC_EXECUTABLE=/path/to/slangc, or `make SLANGC=...`, which the
# Makefile forwards here.
#
# Worth knowing because the failure is unrecognisable from this side. The shader-slang
# package on this machine (Fedora, copr lionheartp/Hyprland) installs /usr/bin/slangc
# and libslang-compiler.so but NOT the downstream-compiler shims that spirv-opt lives
# in -- libslang-glslang-<version>.so. A Debug build never notices, because -O0 below
# skips the optimizer entirely; a Release build compiles every object and then dies in
# the shader step with
#   error[E00100]: failed to load downstream compiler 'spirv-opt'
#   note[E99996]: failed to load dynamic library 'slang-glslang-2026.18'
# naming a library this project never mentions. Any complete Slang install works: the
# official release tarball, or a language-server package that ships bin/ alongside lib/.
find_program(SLANGC_EXECUTABLE slangc REQUIRED)

# add_slang_shaders(<target> <source>...)
#
# Compiles each .slang source into ${CMAKE_CURRENT_BINARY_DIR}/<stem>.spv and
# adds <target> (built by default) that depends on all of them.
#
# No -entry flags: slangc picks up every function marked [shader("...")], so one
# file can hold any number of stages. -fvk-use-entrypoint-name keeps their names
# (vertMain, fragMain, ...) in the SPIR-V instead of renaming them all "main".
#
# Debug builds get -O0 -g: debug info for RenderDoc etc., and no spirv-opt pass.
#
# slangc writes a depfile, so editing a module that a shader imports also
# triggers a rebuild, not only editing the shader itself.
function(add_slang_shaders target)
    set(outputs)
    foreach(source IN LISTS ARGN)
        cmake_path(ABSOLUTE_PATH source NORMALIZE)
        cmake_path(GET source STEM name)
        set(spv ${CMAKE_CURRENT_BINARY_DIR}/${name}.spv)

        set(shader_capabilities)
        if(name STREQUAL "ddgi-trace")
            list(APPEND shader_capabilities -capability spvShaderNonUniformEXT)
        endif()
        add_custom_command(
            OUTPUT  ${spv}
            COMMAND ${SLANGC_EXECUTABLE} ${source}
                    -I "${CMAKE_CURRENT_BINARY_DIR}"
                    -target spirv
                    -profile spirv_1_4
                    -capability spvRayQueryKHR
                    -capability spvDescriptorHeapEXT
                    ${shader_capabilities}
                    -emit-spirv-directly
                    -fvk-use-entrypoint-name
                    "$<$<CONFIG:Debug>:-O0;-g>"
                    -o ${spv}
                    -depfile ${spv}.d
            DEPENDS ${source} ${shader_extra_dependencies}
            DEPFILE ${spv}.d
            COMMENT "Compiling ${name}.slang"
            VERBATIM
            COMMAND_EXPAND_LISTS
        )
        list(APPEND outputs ${spv})
    endforeach()
    add_custom_target(${target} ALL DEPENDS ${outputs})
endfunction()

include(HeapStructs.cmake)
# Generate every shader schema without invoking the shader compiler. Preserve
# relative directories so modules with the same filename cannot collide.
file(GLOB_RECURSE heap_schemas CONFIGURE_DEPENDS
    RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/*.heap")
set(heap_struct_outputs)
foreach(schema IN LISTS heap_schemas)
    string(REGEX REPLACE "\\.heap$" ".slang" generated_name "${schema}")
    set(generated_path "${CMAKE_CURRENT_BINARY_DIR}/generated/${generated_name}")
    generate_heap_structs("${generated_path}" "${CMAKE_CURRENT_SOURCE_DIR}/${schema}")
    list(APPEND heap_struct_outputs "${generated_path}")
endforeach()
add_custom_target(generate_heap_structs DEPENDS ${heap_struct_outputs})

set(shader_extra_dependencies ${heap_struct_outputs})

# Built only by the explicit GPU test target, not by ordinary game builds.
add_slang_shaders(descriptor_heap_test_shader descriptor-heap-test.slang)
set_target_properties(descriptor_heap_test_shader PROPERTIES EXCLUDE_FROM_ALL TRUE)
add_slang_shaders(descriptor_heap_bulk_test_shader descriptor-heap-bulk-test.slang)
set_target_properties(descriptor_heap_bulk_test_shader PROPERTIES EXCLUDE_FROM_ALL TRUE)

add_slang_shaders(shaders
    shader.slang
    ddgi-emitter-prepare.slang
    ddgi-candidates.slang
    ddgi-trace.slang
    ddgi-blend.slang
    ddgi-resolve.slang
    visibility.slang
    tone-map.slang
    temporal-shadows.slang
    spatial-shadows.slang
    text.slang
)

set(heap_struct_include "${CMAKE_CURRENT_BINARY_DIR}/generated/heap-material.slang")
generate_heap_structs("${heap_struct_include}"
    "${PROJECT_SOURCE_DIR}/tests/shaders/heap-material.heap")
# Standalone compiler smoke test; does not require the game or a Vulkan device.
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/heap-material-test.spv"
    COMMAND "${SLANGC_EXECUTABLE}"
            "${PROJECT_SOURCE_DIR}/tests/shaders/heap-material-test.slang"
            -I "${CMAKE_CURRENT_BINARY_DIR}/generated"
            -target spirv -profile spirv_1_4 -capability spvDescriptorHeapEXT
            -emit-spirv-directly -O0
            -o "${CMAKE_CURRENT_BINARY_DIR}/heap-material-test.spv"
            -depfile "${CMAKE_CURRENT_BINARY_DIR}/heap-material-test.spv.d"
    DEPENDS "${heap_struct_include}"
            "${PROJECT_SOURCE_DIR}/tests/shaders/heap-material-test.slang"
    DEPFILE "${CMAKE_CURRENT_BINARY_DIR}/heap-material-test.spv.d"
    VERBATIM
)
add_custom_target(heap_struct_shader_test
    DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/heap-material-test.spv")
