#include "EditorShell.hpp"

#include "engine/core/Log.hpp"
#include "engine/core/Uuid.hpp"
#include "engine/project/Project.hpp"
#include "engine/worldgen/WorldGenSettings.hpp"

#include <Raven.h>

#include <cmath>
#include <string>
#include <utility>

namespace editor {

EditorShell::EditorShell(EditorOptions options) : options_(std::move(options))
{
    widgets_.fill(R_INVALID_ID);
    // Before the engine starts: the console shows the run's log from its
    // first line (ADR 0030).
    logInbox_.listen();
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
                                    "console");

    // The project given on the command line (M5.6): all or nothing (ADR 0028),
    // so a project that does not open leaves no document, and the editor
    // stops with the error.
    if (options_.project) {
        const std::string folder = options_.project->string();
        auto opened = engine::project::openProject(*options_.project,
                                                   engine::core::UuidGenerator::fromEntropy());
        if (!opened) {
            return engine::core::Error{"cannot open the project in '" + folder +
                                       "': " + opened.error().message};
        }
        for (const std::string& warning : opened.value().warnings) {
            engine::core::logWarning("editor", "project: {}", warning);
        }
        document_ = std::make_unique<EditorDocument>(std::move(opened).value().project);
        engine::core::logInfo("editor",
                              "project '{}' opened from '{}': scene '{}', entities: {}, assets: "
                              "{}",
                              document_->project().document.name, folder, document_->scene().name(),
                              document_->scene().entityCount(), document_->assets().size());
    }

    // The panels (M5.7): the scene's entities, and the log received so far.
    logInbox_.listen();
    consoleLog_.append(logInbox_.take());
    if (auto error = hierarchy_.attach(window, document_.get())) {
        return error;
    }
    if (auto error = inspector_.attach(window, document_.get())) {
        detachPanels();
        return error;
    }
    if (auto error = console_.attach(window, consoleLog_)) {
        detachPanels();
        return error;
    }

    // Last, so that nothing fails after it: stop() is not called after a
    // failed start(). The project's world settings, or the default ones.
    const Rid viewport = widgets_[kViewportArea];
    auto world = WorldViewport::create(context.renderer(), viewport,
                                       document_ ? document_->worldSettings()
                                                 : engine::worldgen::WorldGenSettings{},
                                       document_.get());
    if (!world) {
        detachPanels();
        return world.error();
    }
    world_ = std::move(world).value();
    // The viewport has the keyboard from the start: the camera flies at once.
    Rwidget_focus(viewport);
    return std::nullopt;
}

void EditorShell::update(engine::app::AppContext& context, const engine::core::FrameTime& time)
{
    if (world_) {
        world_->update(time);
    }
    // After the document's changes of this frame's input, before Raven draws:
    // the tree and the inspector match the scene in the frame drawn (the
    // inspector after the hierarchy, whose clicks select). The console last,
    // with what the others logged in this update.
    hierarchy_.update();
    inspector_.update();
    consoleLog_.append(logInbox_.take());
    console_.update();
    recordAreas(context);
    // The last frame of a --screenshot run is saved, by the editor itself.
    if (options_.screenshotPath && options_.maxFrames &&
        time.frameIndex + 1 == *options_.maxFrames) {
        context.requestScreenshot(*options_.screenshotPath);
    }
}

void EditorShell::render(engine::app::AppContext& /*context*/)
{
    // Nothing: Raven's frame is the whole window, and the world is drawn from
    // the viewport's callback inside it.
}

void EditorShell::stop(engine::app::AppContext& /*context*/)
{
    // While Raven and the renderer still exist.
    if (world_) {
        const engine::camera::Camera& camera = world_->camera();
        engine::core::logInfo("editor",
                              "viewport camera at ({:.2f}, {:.2f}, {:.2f}), yaw {:.3f}, pitch "
                              "{:.3f}, speed {:.2f}; drawn in {} frames",
                              camera.position.x, camera.position.y, camera.position.z,
                              camera.yawRadians, camera.pitchRadians, world_->moveSpeed(),
                              world_->drawnFrames());
        if (document_) {
            const engine::world::SceneView& scene = world_->scene();
            engine::core::logInfo("editor",
                                  "viewport scene: mesh entities drawn: {} of {}, meshes "
                                  "uploaded: {}, scene reads: {}, document version: {}",
                                  scene.drawnCount(), scene.draws().size(), scene.meshCount(),
                                  world_->sceneUpdates(), document_->version());
        }
        world_->release();
        world_.reset();
    }
    if (hierarchy_.attached()) {
        engine::core::logInfo("editor", "hierarchy: rows: {}, rebuilds: {}, relabels: {}",
                              hierarchy_.rows().size(), hierarchy_.rebuildCount(),
                              hierarchy_.relabelCount());
    }
    if (console_.attached()) {
        using engine::core::LogLevel;
        engine::core::logInfo("editor",
                              "console: lines: {} (errors: {}, warnings: {}, info: {}, debug: "
                              "{}), shown: {}",
                              consoleLog_.lines().size(), consoleLog_.count(LogLevel::Error),
                              consoleLog_.count(LogLevel::Warning),
                              consoleLog_.count(LogLevel::Info), consoleLog_.count(LogLevel::Debug),
                              console_.rowCount());
    }
    detachPanels();
    // What is logged from now on reaches no panel.
    logInbox_.stopListening();
    for (const ShellArea& area : areas_) {
        engine::core::logInfo("editor", "area {}: {}x{} at ({}, {})", area.name, area.width,
                              area.height, area.x, area.y);
    }
}

void EditorShell::detachPanels() noexcept
{
    hierarchy_.detach();
    inspector_.detach();
    console_.detach();
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
