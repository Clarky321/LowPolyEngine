# Raven UI evaluation prototype (docs/experiments/raven-ui-evaluation.md).
#
# Built only with -DLPE_BUILD_RAVEN_EVALUATION=ON and a Raven checkout at the
# pinned commit (fetch_raven.py puts one in build/external/raven). Raven is
# compiled from that checkout as its own C library; nothing is downloaded at
# configure time, and nothing here is part of the engine.

set(LPE_RAVEN_COMMIT "7ab270df3b618de3d11af6033912374b6cd621b7")
set(LPE_RAVEN_SOURCE_DIR "${PROJECT_SOURCE_DIR}/build/external/raven" CACHE PATH
    "Raven checkout at commit ${LPE_RAVEN_COMMIT}")

if(NOT EXISTS "${LPE_RAVEN_SOURCE_DIR}/include/Raven.h")
    message(FATAL_ERROR "LPE_RAVEN_SOURCE_DIR='${LPE_RAVEN_SOURCE_DIR}' holds no Raven checkout; "
                        "run: python sandbox/raven-evaluation/fetch_raven.py")
endif()

# The prototype's configuration of Raven: no Vulkan (so its CMake never looks
# for or downloads the Vulkan headers), no browser-driving web pages, no
# examples, tests or tools, a static library. CMP0077 (Raven requires CMake
# 3.16) makes option() take these normal variables.
set(RAVEN_BACKEND_VULKAN OFF)
set(RAVEN_FETCH_VULKAN_HEADERS OFF)
set(RAVEN_BUILD_EXAMPLES OFF)
set(RAVEN_BUILD_TESTS OFF)
set(RAVEN_BUILD_TOOLS OFF)
set(RAVEN_BUILD_WEB OFF)
set(RAVEN_BUILD_SHARED OFF)
set(RAVEN_WAYLAND OFF)
# SYSTEM: Raven's headers give no warnings in the prototype's C++ code.
add_subdirectory("${LPE_RAVEN_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/raven" EXCLUDE_FROM_ALL SYSTEM)

find_package(SDL3 REQUIRED CONFIG)

add_executable(raven-evaluation
    main.cpp
    GlRavenBackend.cpp
    SdlRavenHost.cpp
    # island-preview's world drawing, reused as it is.
    ${PROJECT_SOURCE_DIR}/sandbox/island-preview/Sky.cpp
    ${PROJECT_SOURCE_DIR}/sandbox/island-preview/WorldScene.cpp
)
target_include_directories(raven-evaluation PRIVATE "${PROJECT_SOURCE_DIR}/sandbox/island-preview")
# SDL3 directly: the prototype owns the one window and the one loop, and hands
# raw SDL events to Raven's host platform (the engine's Platform hides them).
# GLAD: the Raven backend issues OpenGL calls itself, on the engine's context.
target_link_libraries(raven-evaluation PRIVATE engine SDL3::SDL3 glad raven)
target_compile_definitions(raven-evaluation PRIVATE
    LPE_RAVEN_COMMIT="${LPE_RAVEN_COMMIT}"
    LPE_RAVEN_SHADER_DIR="${LPE_RAVEN_SOURCE_DIR}/src/render/vulkan/shaders"
    LPE_RAVEN_EVALUATION_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    LPE_RAVEN_ASSET_DIR="${PROJECT_SOURCE_DIR}/assets"
)
lpe_set_project_warnings(raven-evaluation)
