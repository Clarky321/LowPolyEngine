// The Dear ImGui OpenGL 3 renderer backend (ADR 0018). It brings its own small
// loader for the OpenGL functions it uses and saves and restores the OpenGL
// state around its draws, so the renderer's state is unchanged afterwards.

#include "../../debug/ImGuiBackends.hpp"

#include <imgui.h>
#include <imgui_impl_opengl3.h>

namespace engine::debug::detail {

namespace {

// The renderer targets OpenGL 4.5 Core; the backend's GLSL 4.10 core shaders
// run on it.
constexpr const char* kGlslVersion = "#version 410 core";

} // namespace

std::optional<core::Error> initRendererBackend()
{
    if (!ImGui_ImplOpenGL3_Init(kGlslVersion)) {
        return core::Error{"debug UI: the OpenGL 3 backend failed to start"};
    }
    return std::nullopt;
}

void shutdownRendererBackend() noexcept
{
    ImGui_ImplOpenGL3_Shutdown();
}

void newRendererFrame()
{
    ImGui_ImplOpenGL3_NewFrame();
}

void renderDrawData()
{
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

} // namespace engine::debug::detail
