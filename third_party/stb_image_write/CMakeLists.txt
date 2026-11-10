# stb_image_write implementation; the header comes from the Conan stb package (ADR 0006).
# Third-party code: compiled once here, without project warning flags, like GLAD.
# GCC's -Wnull-dereference reports stb's unchecked realloc at -O3 once the code is
# inlined into a project source file, so the implementation must not live in one.
find_package(stb REQUIRED CONFIG)

add_library(stb_image_write STATIC
    stb_image_write.c
)

# Consumers include <stb_image_write.h> for the declarations only.
target_link_libraries(stb_image_write PUBLIC stb::stb)
# Encoding goes to memory; files are written by project code (core::writeBinaryFile).
target_compile_definitions(stb_image_write PUBLIC STBI_WRITE_NO_STDIO)
