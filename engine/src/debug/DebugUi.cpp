#include "engine/debug/DebugUi.hpp"

#include "ImGuiBackends.hpp"

#include <imgui.h>

#include <utility>

namespace engine::debug {

core::Result<DebugUi> DebugUi::create(platform::Window& window, platform::GlContext& context)
{
    if (ImGui::GetCurrentContext() != nullptr) {
        return core::Error{"debug UI: one already exists; only one can exist at a time"};
    }
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    // No imgui.ini or log file next to the program (tests run from the
    // repository root).
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    ImGui::StyleColorsDark();

    if (auto error = detail::initPlatformBackend(window, context)) {
        ImGui::DestroyContext();
        return std::move(*error);
    }
    if (auto error = detail::initRendererBackend()) {
        detail::shutdownPlatformBackend();
        ImGui::DestroyContext();
        return std::move(*error);
    }
    DebugUi ui;
    ui.active_ = true;
    return ui;
}

DebugUi::~DebugUi()
{
    shutdown();
}

DebugUi::DebugUi(DebugUi&& other) noexcept
    : active_(std::exchange(other.active_, false)),
      frameStarted_(std::exchange(other.frameStarted_, false))
{
}

DebugUi& DebugUi::operator=(DebugUi&& other) noexcept
{
    if (this != &other) {
        shutdown();
        active_ = std::exchange(other.active_, false);
        frameStarted_ = std::exchange(other.frameStarted_, false);
    }
    return *this;
}

void DebugUi::shutdown() noexcept
{
    if (!active_) {
        return;
    }
    active_ = false;
    if (frameStarted_) {
        ImGui::EndFrame();
        frameStarted_ = false;
    }
    detail::shutdownRendererBackend();
    detail::shutdownPlatformBackend();
    ImGui::DestroyContext();
}

void DebugUi::beginFrame()
{
    if (!active_) {
        return;
    }
    endFrame();
    detail::newRendererFrame();
    detail::newPlatformFrame();
    ImGui::NewFrame();
    frameStarted_ = true;
}

void DebugUi::render()
{
    if (!active_ || !frameStarted_) {
        return;
    }
    ImGui::Render();
    detail::renderDrawData();
    frameStarted_ = false;
}

void DebugUi::endFrame()
{
    if (!active_ || !frameStarted_) {
        return;
    }
    ImGui::EndFrame();
    frameStarted_ = false;
}

bool DebugUi::wantsMouse() const noexcept
{
    return active_ && ImGui::GetIO().WantCaptureMouse;
}

bool DebugUi::wantsKeyboard() const noexcept
{
    return active_ && ImGui::GetIO().WantCaptureKeyboard;
}

} // namespace engine::debug
