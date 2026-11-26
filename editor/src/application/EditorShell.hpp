#pragma once

// lpe-editor's application: the editor shell on Raven (M5.2) with the
// generated world in its viewport (M5.3), given a project folder the
// project's document with its scene's entities in the viewport (M5.6), the
// hierarchy and console panels (M5.7) and the inspector (M5.8). The engine
// runs Raven with the shell's layout (ApplicationDesc::ui, ADR 0027); the
// shell checks that the layout has its five areas, opens the project, shows
// the archipelago of the project's world settings (the default ones without a
// project) in the viewport, the scene's entities in the hierarchy, the
// selected entity's components in the inspector and the engine's log in the
// console, saves a requested screenshot on the last frame, and records where
// the areas are for the checks.
//
// The console's inbox listens to the engine's log from the shell's
// construction, before the engine starts, so the console shows the run's log
// from its first line; it stops listening in stop().

#include "EditorOptions.hpp"
#include "editor/document/EditorDocument.hpp"
#include "editor/panels/ConsoleLog.hpp"
#include "panels/ConsolePanel.hpp"
#include "panels/HierarchyPanel.hpp"
#include "panels/InspectorPanel.hpp"
#include "viewport/WorldViewport.hpp"

#include "engine/app/Application.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace editor {

// The named areas of the shell, in the layout's order.
inline constexpr std::array<std::string_view, 5> kShellAreas{"toolbar", "viewport", "hierarchy",
                                                             "inspector", "console"};
// The viewport's index in kShellAreas.
inline constexpr std::size_t kViewportArea = 1;
static_assert(kShellAreas[kViewportArea] == "viewport");

// Where an area was in the last frame, in the window's pixels (origin top left).
struct ShellArea {
    std::string_view name;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

class EditorShell final : public engine::app::Application {
public:
    explicit EditorShell(EditorOptions options);

    std::optional<engine::core::Error> start(engine::app::AppContext& context) override;
    void update(engine::app::AppContext& context, const engine::core::FrameTime& time) override;
    void render(engine::app::AppContext& context) override;
    void stop(engine::app::AppContext& context) override;

    // The areas as laid out in the last frame of the run (empty before Raven's
    // first layout).
    [[nodiscard]] const std::vector<ShellArea>& areas() const noexcept { return areas_; }

    // The world in the viewport, from start() to stop(); null otherwise.
    [[nodiscard]] const WorldViewport* world() const noexcept { return world_.get(); }

    // The open project's document, from start() on (it stays after stop());
    // null without a project.
    [[nodiscard]] EditorDocument* document() noexcept { return document_.get(); }
    [[nodiscard]] const EditorDocument* document() const noexcept { return document_.get(); }

    // The panels, attached from start() to stop().
    [[nodiscard]] const HierarchyPanel& hierarchy() const noexcept { return hierarchy_; }
    [[nodiscard]] const InspectorPanel& inspector() const noexcept { return inspector_; }
    [[nodiscard]] const ConsolePanel& console() const noexcept { return console_; }
    // The console's lines (they stay after stop()).
    [[nodiscard]] const ConsoleLog& consoleLog() const noexcept { return consoleLog_; }

private:
    void recordAreas(engine::app::AppContext& context);
    void detachPanels() noexcept;

    EditorOptions options_;
    std::array<std::uint32_t, kShellAreas.size()> widgets_{};
    std::vector<ShellArea> areas_;
    // Before the console's panel, which shows them.
    LogInbox logInbox_;
    ConsoleLog consoleLog_;
    // Before the viewport and the hierarchy, which refer to it.
    std::unique_ptr<EditorDocument> document_;
    std::unique_ptr<WorldViewport> world_;
    HierarchyPanel hierarchy_;
    InspectorPanel inspector_;
    ConsolePanel console_;
};

} // namespace editor
