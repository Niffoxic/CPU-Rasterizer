cmake_minimum_required(VERSION 4.1)
project(FoxRasterizer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (WIN32)
    enable_language(RC)
endif()

option(DEBUG "Enable Debug Utilities" OFF)
option(USE_SIMD "Enable SIMD" ON)
option(TRACER "Enable Tracy profiler" OFF)
option(EDIT_MODE "Enable Free Camera Mode" ON)

set(FOX_SIMD_LEVEL "AVX2" CACHE STRING "SIMD level for MSVC: SSE2, AVX, AVX2")
set_property(CACHE FOX_SIMD_LEVEL PROPERTY STRINGS SSE2 AVX AVX2)

option(FOX_FAST_MATH "Enable fast floating point (/fp:fast) in optimized configs" ON)
option(FOX_DISABLE_EXCEPTIONS "Disable C++ exceptions (/EHs-c-)" OFF)
option(FOX_DISABLE_RTTI "Disable RTTI (/GR-)" OFF)

add_executable(rasterizer
        main.cpp
        src/platform_windows.cpp
        src/gfx_dx11.cpp
        src/game_world.cpp
        src/camera.cpp
        src/static_mesh.cpp
        src/dynamic_mesh.cpp
        src/optimized_renderer.cpp
        src/fox/scene_io.cpp
        src/render_queue.cpp
        src/level_builder_ui.cpp
        src/texture_cache.cpp
        src/editor/drag_move_tool.cpp
        src/json_loader.cpp
        src/file_system.cpp
        src/helpers.cpp
        src/imgui_hook.cpp
        resource/app.rc
        resource/resource.h
)

target_include_directories(rasterizer PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/resource
)

target_compile_definitions(rasterizer PRIVATE
        NOMINMAX
        UNICODE
        _UNICODE

        $<$<BOOL:${USE_SIMD}>:USE_SIMD>
        $<$<BOOL:${DEBUG}>:DEBUG>
        $<$<BOOL:${EDIT_MODE}>:EDIT_MODE>

        FOX_SIMD_LEVEL_${FOX_SIMD_LEVEL}=1
)

if (MSVC)
    target_compile_options(rasterizer PRIVATE
            /W4
            /permissive-
            /Zc:__cplusplus
            /Zc:preprocessor
    )

    if (FOX_SIMD_LEVEL STREQUAL "AVX")
        target_compile_options(rasterizer PRIVATE /arch:AVX)
    elseif (FOX_SIMD_LEVEL STREQUAL "AVX2")
        target_compile_options(rasterizer PRIVATE /arch:AVX2)
    endif()

    if (FOX_FAST_MATH)
        target_compile_options(rasterizer PRIVATE
                $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:/fp:fast>
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

    if (FOX_DISABLE_EXCEPTIONS)
        target_compile_options(rasterizer PRIVATE /EHs-c-)
    endif()

    if (FOX_DISABLE_RTTI)
        target_compile_options(rasterizer PRIVATE /GR-)
    endif()
endif()

include(FetchContent)

FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG v1.91.0
)
FetchContent_MakeAvailable(imgui)

add_library(imgui_lib STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp

        ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
)

target_include_directories(imgui_lib PUBLIC
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
)

target_link_libraries(imgui_lib PUBLIC
        d3d11
        dxgi
)

target_compile_definitions(imgui_lib PUBLIC
        IMGUI_IMPL_WIN32_DISABLE_GAMEPAD
)

set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
        assimp
        GIT_REPOSITORY https://github.com/assimp/assimp.git
        GIT_TAG v5.4.3
)
FetchContent_MakeAvailable(assimp)

if (TRACER)
    FetchContent_Declare(
            tracy
            GIT_REPOSITORY https://github.com/wolfpld/tracy.git
            GIT_TAG v0.13.1
    )
    FetchContent_MakeAvailable(tracy)

    target_link_libraries(rasterizer PRIVATE Tracy::TracyClient)
    target_compile_definitions(rasterizer PRIVATE
            TRACER=1
            TRACY_ENABLE=1
            TRACY_ON_DEMAND=1
    )
else()
    target_compile_definitions(rasterizer PRIVATE TRACER=0)
endif()

target_link_libraries(rasterizer PRIVATE
        assimp::assimp
        imgui_lib
        d3d11
        d3dcompiler
        dxgi
        windowscodecs
        xinput
)

add_custom_command(TARGET rasterizer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:assimp::assimp>
        $<TARGET_FILE_DIR:rasterizer>
)

set(FOX_ASSETS_SRC "${CMAKE_SOURCE_DIR}/assets")
set(FOX_ASSETS_DST "$<TARGET_FILE_DIR:rasterizer>/assets")

set(FOX_SCENE_SRC "${CMAKE_SOURCE_DIR}/scene.json")
set(FOX_SCENE_DST "$<TARGET_FILE_DIR:rasterizer>/scene.json")

add_custom_command(TARGET rasterizer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${FOX_SCENE_SRC}"
        "${FOX_SCENE_DST}"
        VERBATIM
)


add_custom_command(TARGET rasterizer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${FOX_ASSETS_DST}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${FOX_ASSETS_SRC}" "${FOX_ASSETS_DST}"
)

set_property(TARGET rasterizer PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

message(STATUS "MSVC SIMD: FOX_SIMD_LEVEL=${FOX_SIMD_LEVEL}")
message(STATUS "FOX_FAST_MATH=${FOX_FAST_MATH}")
message(STATUS "TRACER=${TRACER}")
message(STATUS "EDIT_MODE=${EDIT_MODE}")












