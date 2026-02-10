cmake_minimum_required(VERSION 4.1)
project(assignment LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_language(RC)

# Options
option(DEBUG "Enable Debug Utilities" OFF)
option(USE_SIMD "Enable SIMD" ON)

set(FOX_SIMD_LEVEL "AVX2" CACHE STRING "SIMD level for MSVC: SSE2, AVX, AVX2")
set_property(CACHE FOX_SIMD_LEVEL PROPERTY STRINGS SSE2 AVX AVX2)

option(FOX_FAST_MATH "Enable fast floating point (/fp:fast)" ON)
option(FOX_DISABLE_EXCEPTIONS "Disable C++ exceptions (/EHs-c-)" OFF)
option(FOX_DISABLE_RTTI "Disable RTTI (/GR-)" OFF)

# Shared engine / renderer
add_library(rasterizer STATIC
        src/platform_windows.cpp
        src/gfx_dx11.cpp
        src/scene1.cpp
        src/scene2.cpp
        src/optimized_renderer.cpp

        resource/app.rc
        resource/resource.h
)

target_include_directories(rasterizer
        PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/resource
)

target_link_libraries(rasterizer
        PRIVATE
        d3d11
        d3dcompiler
        dxgi
        windowscodecs
        xinput
)

target_compile_definitions(rasterizer PRIVATE
        $<$<BOOL:${USE_SIMD}>:USE_SIMD>
        $<$<BOOL:${DEBUG}>:DEBUG>
        NOMINMAX
        UNICODE
        _UNICODE
)

target_compile_options(rasterizer PRIVATE
        /W4
        /permissive-
        /Zc:__cplusplus
        /Zc:preprocessor
)

# SIMD configuration
target_compile_definitions(rasterizer PRIVATE FOX_SIMD_LEVEL_${FOX_SIMD_LEVEL}=1)

if (FOX_SIMD_LEVEL STREQUAL "AVX")
    target_compile_options(rasterizer PRIVATE /arch:AVX)
elseif (FOX_SIMD_LEVEL STREQUAL "AVX2")
    target_compile_options(rasterizer PRIVATE /arch:AVX2)
endif()

# Optimization flags
if (FOX_FAST_MATH)
    target_compile_options(rasterizer PRIVATE
            $<$<CONFIG:Release>:/fp:fast>
            $<$<CONFIG:RelWithDebInfo>:/fp:fast>
    )
endif()

target_compile_options(rasterizer PRIVATE
        $<$<CONFIG:Release>:/O2 /Ob3 /Oi /Ot /GL /DNDEBUG>
        $<$<CONFIG:RelWithDebInfo>:/O2 /Ob3 /Oi /Ot /DNDEBUG>
        $<$<CONFIG:MinSizeRel>:/O1 /DNDEBUG>
        $<$<CONFIG:Debug>:/Od /Zi>
)

target_link_options(rasterizer PRIVATE
        $<$<CONFIG:Release>:/LTCG /INCREMENTAL:NO>
)

# Language features
if (FOX_DISABLE_EXCEPTIONS)
    target_compile_options(rasterizer PRIVATE /EHs-c-)
endif()

if (FOX_DISABLE_RTTI)
    target_compile_options(rasterizer PRIVATE /GR-)
endif()

set_property(TARGET rasterizer
        PROPERTY VS_DEBUGGER_WORKING_DIRECTORY
        "${CMAKE_SOURCE_DIR}"
)

# Scene executables
add_executable(scene_1_realtime  scene_1_realtime.cpp)
add_executable(scene_1_offload   scene_1_offload.cpp)
add_executable(scene_2_realtime  scene_2_realtime.cpp)
add_executable(scene_2_offload   scene_2_offload.cpp)

foreach(app
        scene_1_realtime
        scene_1_offload
        scene_2_realtime
        scene_2_offload
)
    target_link_libraries(${app} PRIVATE rasterizer)
    target_include_directories(${app} PRIVATE ${CMAKE_SOURCE_DIR}/include)
    set_property(TARGET ${app}
            PROPERTY VS_DEBUGGER_WORKING_DIRECTORY
            "${CMAKE_SOURCE_DIR}"
    )
endforeach()

message(STATUS "MSVC SIMD: FOX_SIMD_LEVEL=${FOX_SIMD_LEVEL}")
message(STATUS "FOX_FAST_MATH=${FOX_FAST_MATH}")
