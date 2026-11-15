#include "EditorShell.hpp"

#include "engine/core/Log.hpp"

#include <Raven.h>

#include <cmath>
#include <string>
#include <utility>

namespace editor {

EditorShell::EditorShell(EditorOptions options) : options_(std::move(options))
{
    widgets_.fill(R_INVALID_ID);
}

std::optional<engine::core::Error> EditorShell::start(engine::app::AppContext& context)
{
    engine::ui::Ui* ui = context.ui();
    if (ui == nullptr) {
        return engine::core::Error{"the editor needs the Raven UI (ApplicationDesc::ui)"};
    }
    const Rid window = ui->mainWindow();
    for (std::size_t i = 0; i < kShellAreas.size(); ++i) {
        const std::string name(kShellAreas[i]);
        widgets_[i] = Rwidget_find(window, name.c_str());
        if (widgets_[i] == R_INVALID_ID) {
            return engine::core::Error{"the shell layout has no area named '" + name + "'"};
        }
    }
    engine::core::logInfo("editor", "editor shell: toolbar, viewport, hierarchy, inspector and "
                                    "console (empty)");
    return std::nullopt;
}

void EditorShell::update(engine::app::AppContext& context, const engine::core::FrameTime& time)
{
    recordAreas(context);
    // The last frame of a --screenshot run is saved, by the editor itself.
    if (options_.screenshotPath && options_.maxFrames &&
        time.frameIndex + 1 == *options_.maxFrames) {
        context.requestScreenshot(*options_.screenshotPath);
    }
}

void EditorShell::render(engine::app::AppContext& /*context*/)
{
    // Nothing yet: Raven's frame is the whole window. The viewport's content
    // comes in M5.3.
}

void EditorShell::stop(engine::app::AppContext& /*context*/)
{
    for (const ShellArea& area : areas_) {
        engine::core::logInfo("editor", "area {}: {}x{} at ({}, {})", area.name, area.width,
                              area.height, area.x, area.y);
    }
}

void EditorShell::recordAreas(engine::app::AppContext& context)
{
    engine::ui::Ui* ui = context.ui();
    if (ui == nullptr || ui->mainWindow() == engine::ui::kInvalidRavenId) {
        return;
    }
    // Raven lays out in logical units; the window's pixels are those times its
    // scale.
    const float scale = ui->scale();
    std::vector<ShellArea> areas;
    for (std::size_t i = 0; i < kShellAreas.size(); ++i) {
        if (Rwidget_is_valid(widgets_[i]) == R_FALSE) {
            return;
        }
        float x = 0.0F;
        float y = 0.0F;
        float width = 0.0F;
        float height = 0.0F;
        Rwidget_get_rect(widgets_[i], &x, &y, &width, &height);
        areas.push_back({kShellAreas[i], static_cast<int>(std::lround(x * scale)),
                         static_cast<int>(std::lround(y * scale)),
                         static_cast<int>(std::lround(width * scale)),
                         static_cast<int>(std::lround(height * scale))});
    }
    areas_ = std::move(areas);
}

} // namespace editor
