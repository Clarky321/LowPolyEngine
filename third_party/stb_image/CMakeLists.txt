# stb_image implementation for PNG decoding; the header comes from the Conan stb
# package (ADR 0008). Third-party code: compiled once here, without project
# warning flags, like stb_image_write (ADR 0006) and GLAD.
find_package(stb REQUIRED CONFIG)

add_library(stb_image STATIC
    stb_image.c
)

target_link_libraries(stb_image PUBLIC stb::stb)
# Consumers include <stb_image.h> for the declarations only; these options must
# match between the implementation and every includer.
#   STBI_ONLY_PNG   PNG is the only accepted image format (ADR 0008).
#   STBI_NO_STDIO   files are read by project code (core::readTextFile).
#   STBI_NO_LINEAR  no float/HDR loading API.
target_compile_definitions(stb_image PUBLIC
    STBI_ONLY_PNG
    STBI_NO_STDIO
    STBI_NO_LINEAR
)
# Size limits are not set here: core::decodePng reads the header with
# stbi_info_from_memory and applies the project's limits before decoding.
