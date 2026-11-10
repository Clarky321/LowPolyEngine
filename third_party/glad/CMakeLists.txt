# Generated OpenGL loader (see README.md). Third-party code: no project warning flags.
add_library(glad STATIC
    src/gl.c
)

# SYSTEM: consumers do not get warnings from the generated headers.
target_include_directories(glad SYSTEM PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
