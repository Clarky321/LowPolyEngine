# Raven, the UI library of the editor (ADR 0019), from the pruned vendored
# source in third_party/raven/ (ADR 0026). What is kept, what was removed, the
# one local change and the update procedure: third_party/raven/README.md.
#
# Raven's own CMakeLists.txt defines the static C library target `raven`; this
# file fixes the configuration the project builds it with. Nothing is fetched:
# the Vulkan backend (which would look for or download the Vulkan headers),
# the web pages, examples, tests, tools and the installer are off.

# The upstream release and commit the vendored copy was taken from. An update
# changes both, together with the copy (third_party/raven/README.md, "Updating").
set(LPE_RAVEN_VERSION "0.2.3")
set(LPE_RAVEN_COMMIT "7ab270df3b618de3d11af6033912374b6cd621b7")

# Raven's switches are option()s. Set as normal variables they win over the
# option defaults (Raven requires CMake 3.16, so CMP0077 is NEW there) and
# leave no cache entries behind that an old build folder could keep.
set(RAVEN_BUILD_SHARED OFF)          # one static library, linked into the program
set(RAVEN_BUILD_EXAMPLES OFF)
set(RAVEN_BUILD_TESTS OFF)           # also keeps bindings/ out (it is built with the tests)
set(RAVEN_BUILD_TOOLS OFF)
set(RAVEN_BUILD_INSTALLER OFF)
set(RAVEN_BUILD_WEB OFF)             # raven_web.h drives an installed browser; not used
set(RAVEN_BACKEND_VULKAN OFF)        # the engine draws Raven with OpenGL (M5.2), never Vulkan
set(RAVEN_FETCH_VULKAN_HEADERS OFF)  # never download anything
set(RAVEN_REBUILD_SHADERS OFF)       # the shader sources stay as they are (no glslang)
set(RAVEN_BACKEND_WEBGL OFF)         # browser builds only
set(RAVEN_WITH_STB_IMAGE ON)         # PNG/JPEG/BMP/TGA images of XML layouts (bundled stb_image)
set(RAVEN_WITH_SVG ON)               # SVG images and icons (bundled nanosvg)
set(RAVEN_WARNINGS_AS_ERRORS OFF)    # third-party code: its warnings never fail our build
set(RAVEN_MSVC_STATIC_RUNTIME OFF)   # the C runtime DLL, like the rest of the project
set(RAVEN_WAYLAND OFF)               # no Wayland platform (it is only built beside the X11 one)

# The window system is a cache string in Raven's CMakeLists.txt, so it is set
# in the cache: a normal variable would be dropped by Raven's set(CACHE) under
# its CMake 3.16 policies (CMP0126 OLD). "null" compiles Raven's null and host
# platforms only. SDL3 owns every window of the engine (AGENTS.md section 3,
# ADR 0002), and Raven reaches it through its host platform (ADR 0019), so the
# native win32/X11/Wayland platforms of Raven are not compiled and Linux needs
# no X11 development files for Raven.
set(RAVEN_PLATFORM "null" CACHE STRING
    "Raven's window system; LowPolyEngine builds only the null and host platforms (ADR 0026)" FORCE)

# SYSTEM: Raven's headers are system headers for every target that links
# `raven`, so the project's warning flags (-Werror) never see their code.
add_subdirectory("${PROJECT_SOURCE_DIR}/third_party/raven"
                 "${PROJECT_BINARY_DIR}/third_party/raven" SYSTEM)

# The one change to Raven's sources (third_party/raven/README.md, "Local
# changes"): the HTTP(S) image download (WinHTTP on Windows, a curl process
# elsewhere) is compiled out, since Raven has no build option for it.
target_compile_definitions(raven PRIVATE LPE_RAVEN_NO_HTTP_DOWNLOAD)

# Raven gets its own warning level (cmake/RavenCompilerOptions.cmake in its
# tree) and none of the project's (lpe_set_project_warnings is never applied to it).
