#pragma once

// Internal to the OpenGL backend: Raven's reference UI shaders, embedded at
// build time from the vendored copy (third_party/raven/src/render/vulkan/
// shaders/ui.vert and ui.frag; engine/CMakeLists.txt, cmake/EmbedFiles.cmake).
// GlRavenBackend adapts them from Vulkan GLSL to OpenGL GLSL at Init, so the
// program reads no shader file of Raven's at run time.

#include <string_view>

namespace engine::render::opengl::raven_shaders {

[[nodiscard]] std::string_view uiVert() noexcept;
[[nodiscard]] std::string_view uiFrag() noexcept;

} // namespace engine::render::opengl::raven_shaders
