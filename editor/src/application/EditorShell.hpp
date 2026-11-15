#pragma once

// lpe-editor's application (M5.2): the empty editor shell on Raven. The engine
// runs Raven with the shell's layout (ApplicationDesc::ui, ADR 0027); the shell
// checks that the layout has its five areas, saves a requested screenshot on
// the last frame, and records where the areas are for the checks. The areas
// are empty until later steps fill them.

#include "EditorOptions.hpp"

#include "engine/app/Application.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace editor {

// The named areas of the shell, in the layout's order.
inline constexpr std::array<std::string_view, 5> kShellAreas{"toolbar", "viewport", "hierarchy",
                                                             "inspector", "console"};

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

private:
    void recordAreas(engine::app::AppContext& context);

    EditorOptions options_;
    std::array<std::uint32_t, kShellAreas.size()> widgets_{};
    std::vector<ShellArea> areas_;
};

} // namespace editor
