// GPU tier (M5.7): the hierarchy and the console in the running editor, with
// the committed test project tests/projects/lighthouses. The hierarchy shows
// the scene's seven entities, the two children indented under their parents,
// in light text on its tree; without a project the tree is empty. The tree
// matches the scene at the end of every frame's update, also through edits
// and undo. The console shows the run's log; a line logged in a frame is in
// its list at the end of that frame, and a warning is drawn in amber.
// Runs from the repository root (tests/rendering/CMakeLists.txt).

#include "EditorPixels.hpp"

#include "application/EditorShell.hpp"
#include "application/ShellLayout.hpp"
#include "editor/commands/EntityCommands.hpp"
#include "editor/document/EditorDocument.hpp"
#include "editor/panels/HierarchyRows.hpp"
#include "panels/HierarchyPanel.hpp"

#include "engine/app/Application.hpp"
#include "engine/core/Image.hpp"
#include "engine/core/Log.hpp"
#include "engine/scene/NameComponent.hpp"

#include <catch2/catch_test_macros.hpp>

#include <Raven.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

using lpe_gpu_test::brightPixels;
using lpe_gpu_test::luminance;
using lpe_gpu_test::pixelAt;
using lpe_gpu_test::Rgb;

namespace {

constexpr int kWidth = 1024;
constexpr int kHeight = 640;
const std::filesystem::path kProject = "tests/projects/lighthouses";
// The frame the console test logs its warning in.
constexpr std::uint64_t kWarningFrame = 5;

std::filesystem::path outputPath(std::string_view name)
{
    const auto directory = std::filesystem::temp_directory_path() / "lpe_gpu_test";
    std::error_code ignored;
    std::filesystem::create_directories(directory, ignored);
    auto path = directory / name;
    std::filesystem::remove(path, ignored);
    return path;
}

engine::app::ApplicationDesc shellDesc(std::uint64_t frames)
{
    engine::app::ApplicationDesc desc{
        .window = {.title = "lpe editor panels GPU test", .width = kWidth, .height = kHeight},
        .clearColor = {1.0F, 0.0F, 1.0F, 1.0F},
        .maxFrames = frames,
    };
    desc.ui = engine::ui::UiDesc{.layoutXml = std::string(editor::layouts::shell()),
                                 .layoutName = std::string(editor::layouts::kShellName)};
    return desc;
}

void checkCleanRun(const engine::app::RunSummary& summary)
{
    CHECK(summary.openGlErrors == 0);
    CHECK(summary.failedScreenshots == 0);
    CHECK(summary.ui.glErrors == 0);
    CHECK(summary.ui.stateMismatches == 0);
    CHECK(summary.ui.ravenErrors == 0);
    CHECK(summary.ui.blocksAfterShutdown == 0);
}

// A row of the hierarchy as drawn in the last frame, in the window's pixels.
struct DrawnRow {
    std::string label;
    std::size_t depth = 0;
    int top = 0;
    int bottom = 0;
};

// The editor with actions at given frames (before the shell's update, as
// input arrives) and records after each frame's update. Nothing here throws:
// Catch's REQUIRE would throw through the engine's loop, so the callbacks
// use CHECK.
class PanelsProbe final : public engine::app::Application {
public:
    using Action = std::function<void(editor::EditorDocument*)>;

    PanelsProbe(editor::EditorOptions options, std::uint64_t frameCount,
                std::filesystem::path screenshot)
        : shell_(std::move(options)), frames_(frameCount), screenshot_(std::move(screenshot))
    {
    }

    void actAt(std::uint64_t frame, Action action) { actions_[frame] = std::move(action); }

    std::optional<engine::core::Error> start(engine::app::AppContext& context) override
    {
        return shell_.start(context);
    }

    void update(engine::app::AppContext& context, const engine::core::FrameTime& time) override
    {
        if (const auto action = actions_.find(time.frameIndex); action != actions_.end()) {
            action->second(shell_.document());
        }
        shell_.update(context, time);
        // The tree shows the scene in this frame already.
        if (const editor::EditorDocument* document = shell_.document()) {
            treeMatched.push_back(shell_.hierarchy().rows() ==
                                  editor::hierarchyRows(document->scene()));
            itemsMatched.push_back(itemsMatch());
        }
        consoleRows.push_back(shell_.console().rowCount());
        lastConsoleRow.emplace_back(shell_.console().rowCount() > 0
                                        ? shell_.console().rowText(shell_.console().rowCount() - 1)
                                        : "");
        if (time.frameIndex + 1 == frames_) {
            recordRows(context);
            context.requestScreenshot(screenshot_);
        }
    }

    void render(engine::app::AppContext& context) override { shell_.render(context); }
    void stop(engine::app::AppContext& context) override { shell_.stop(context); }

    [[nodiscard]] const editor::EditorShell& shell() const { return shell_; }

    std::vector<bool> treeMatched;
    std::vector<bool> itemsMatched;
    std::vector<std::size_t> consoleRows;
    std::vector<std::string> lastConsoleRow;
    std::vector<DrawnRow> drawnRows;
    // The tree's left edge, in pixels.
    int treeLeft = 0;

private:
    // Raven's items hold the rows' labels, in order.
    [[nodiscard]] bool itemsMatch() const
    {
        const editor::HierarchyPanel& panel = shell_.hierarchy();
        for (const editor::HierarchyRow& row : panel.rows()) {
            const Rid item = panel.itemOf(row.entity);
            const char* text = item != R_INVALID_ID ? Rtree_item_get_text(item) : nullptr;
            if (text == nullptr || row.label != text) {
                return false;
            }
        }
        return true;
    }

    void recordRows(engine::app::AppContext& context)
    {
        const editor::HierarchyPanel& panel = shell_.hierarchy();
        const float scale = context.ui() != nullptr ? context.ui()->scale() : 1.0F;
        float x = 0.0F;
        float y = 0.0F;
        float width = 0.0F;
        float height = 0.0F;
        Rwidget_get_rect(panel.tree(), &x, &y, &width, &height);
        treeLeft = static_cast<int>(std::lround(x * scale));
        for (const editor::HierarchyRow& row : panel.rows()) {
            const auto rect = panel.rowRect(row.entity);
            CHECK(rect.has_value());
            if (!rect) {
                continue;
            }
            drawnRows.push_back(
                {row.label, row.depth, static_cast<int>(std::lround((y + rect->y) * scale)),
                 static_cast<int>(std::lround((y + rect->y + rect->height) * scale))});
        }
    }

    editor::EditorShell shell_;
    std::uint64_t frames_;
    std::filesystem::path screenshot_;
    std::map<std::uint64_t, Action> actions_;
};

editor::EditorOptions withProject()
{
    editor::EditorOptions options;
    options.project = kProject;
    return options;
}

engine::core::Image readFrame(const std::filesystem::path& path)
{
    auto image = engine::core::readPng(path);
    REQUIRE(image.hasValue());
    return std::move(image).value();
}

engine::scene::EntityId entityNamed(const editor::EditorDocument& document, std::string_view name)
{
    for (const engine::scene::EntityId entity : document.scene().entityIds()) {
        const auto component = document.scene().component<engine::scene::NameComponent>(entity);
        if (component && component.value().value == name) {
            return entity;
        }
    }
    return {};
}

void succeed(const std::optional<engine::core::Error>& error)
{
    INFO((error ? error->message : std::string()));
    CHECK_FALSE(error.has_value());
}

// The leftmost light pixel of a row band of the tree: where its text starts.
int textStart(const engine::core::Image& image, const editor::ShellArea& hierarchy,
              const DrawnRow& row)
{
    for (int x = hierarchy.x; x < hierarchy.x + hierarchy.width; ++x) {
        for (int y = row.top; y < row.bottom; ++y) {
            if (luminance(pixelAt(image, x, y)) > 150) {
                return x;
            }
        }
    }
    return -1;
}

bool amber(const Rgb& c)
{
    return c.r > 180 && c.g > 120 && c.b < 120 && c.r > c.b + 80;
}

} // namespace

TEST_CASE("The hierarchy shows the test project's entities, and the console the run's log",
          "[editor][hierarchy][console][gpu]")
{
    // The console scrolls to a new line smoothly: the warning comes early
    // enough to be shown whole in the last frame.
    constexpr std::uint64_t kFrames = 45;
    const auto emptyPath = outputPath("editor-panels-empty.png");
    const auto projectPath = outputPath("editor-panels-project.png");

    // Without a project: an empty tree.
    PanelsProbe empty(editor::EditorOptions{}, kFrames, emptyPath);
    const auto emptyRun = engine::app::run(empty, shellDesc(kFrames));
    REQUIRE(emptyRun.hasValue());
    checkCleanRun(emptyRun.value());
    CHECK(empty.shell().hierarchy().rows().empty());
    CHECK(empty.consoleRows.back() > 0);

    // With the project, and a warning logged in an early frame.
    PanelsProbe probe(withProject(), kFrames, projectPath);
    probe.actAt(kWarningFrame, [](editor::EditorDocument* /*document*/) {
        engine::core::logWarning("gpu-test", "a warning in frame {}", kWarningFrame);
    });
    const auto projectRun = engine::app::run(probe, shellDesc(kFrames));
    REQUIRE(projectRun.hasValue());
    checkCleanRun(projectRun.value());

    // The scene's rows: its creation order, the children under their parents.
    std::vector<std::string> rows;
    for (const DrawnRow& row : probe.drawnRows) {
        rows.push_back(std::string(row.depth * 2, ' ') + row.label);
    }
    CHECK(rows == std::vector<std::string>{
                      "Evening sun",
                      "Lighthouse on the near island",
                      "  Harbour camera",
                      "Lighthouse on the high island",
                      "  Floating lighthouse",
                      "Lighthouse on the far island",
                      "Lighthouse on the west island",
                  });
    for (std::size_t frame = 0; frame < probe.treeMatched.size(); ++frame) {
        INFO("frame " << frame);
        CHECK(probe.treeMatched[frame]);
        CHECK(probe.itemsMatched[frame]);
    }

    // Each row's text is drawn in its band; a child's starts further right.
    const engine::core::Image withProject = readFrame(projectPath);
    const engine::core::Image withoutProject = readFrame(emptyPath);
    const editor::ShellArea& hierarchy = lpe_gpu_test::area(probe.shell().areas(), "hierarchy");
    REQUIRE(probe.drawnRows.size() == 7);
    std::vector<int> starts;
    for (const DrawnRow& row : probe.drawnRows) {
        INFO("row " << row.label << " at y " << row.top << " to " << row.bottom);
        CHECK(row.top >= hierarchy.y);
        CHECK(row.bottom <= hierarchy.y + hierarchy.height);
        CHECK(brightPixels(withProject, hierarchy.x, row.top, hierarchy.width, row.bottom - row.top,
                           150) > 20);
        CHECK(brightPixels(withoutProject, hierarchy.x, row.top, hierarchy.width,
                           row.bottom - row.top, 150) == 0);
        starts.push_back(textStart(withProject, hierarchy, row));
    }
    // Rows with children begin with their branch's arrow: the text's start is
    // compared on the rows without (0, 5 and 6 at the top level; 2 and 4, the
    // children, one level in).
    INFO("text starts: " << starts[0] << ", " << starts[2] << ", " << starts[4] << ", " << starts[5]
                         << ", " << starts[6]);
    CHECK(starts[0] > probe.treeLeft);
    CHECK(starts[5] == starts[0]);
    CHECK(starts[6] == starts[0]);
    CHECK(starts[2] > starts[0] + 6);
    CHECK(starts[4] == starts[2]);
    // A parent's arrow comes before its children's text.
    CHECK(starts[1] < starts[2]);
    CHECK(starts[3] < starts[4]);

    // The console: the project's line, and the warning in its own frame.
    const editor::ConsoleLog& log = probe.shell().consoleLog();
    bool opened = false;
    for (const editor::ConsoleLine& line : log.lines()) {
        opened = opened || line.text.find("[info] [editor] project 'Lighthouses' opened from") == 0;
    }
    CHECK(opened);
    REQUIRE(probe.lastConsoleRow.size() == kFrames);
    CHECK(probe.lastConsoleRow[kWarningFrame] == "[warning] [gpu-test] a warning in frame 5");
    CHECK(probe.consoleRows[kWarningFrame] == probe.consoleRows[kWarningFrame - 1] + 1);
    CHECK(log.count(engine::core::LogLevel::Warning) == 1);
    const editor::ShellArea& console = lpe_gpu_test::area(probe.shell().areas(), "console");
    std::size_t amberPixels = 0;
    std::size_t amberWithout = 0;
    for (int y = console.y; y < console.y + console.height; ++y) {
        for (int x = console.x; x < console.x + console.width; ++x) {
            amberPixels += amber(pixelAt(withProject, x, y)) ? 1U : 0U;
            amberWithout += amber(pixelAt(withoutProject, x, y)) ? 1U : 0U;
        }
    }
    INFO(amberPixels << " amber pixels in the console with the warning, " << amberWithout
                     << " without");
    CHECK(amberPixels > 50);
    CHECK(amberWithout == 0);

    std::error_code ignored;
    std::filesystem::remove(emptyPath, ignored);
    std::filesystem::remove(projectPath, ignored);
}

TEST_CASE("The hierarchy follows edits and undo in the frame they happen",
          "[editor][hierarchy][gpu]")
{
    constexpr std::uint64_t kFrames = 14;
    const auto path = outputPath("editor-panels-edits.png");
    PanelsProbe probe(withProject(), kFrames, path);
    engine::scene::EntityId camera;
    probe.actAt(3, [&camera](editor::EditorDocument* document) {
        CHECK(document != nullptr);
        if (document == nullptr) {
            return;
        }
        camera = entityNamed(*document, "Harbour camera");
        succeed(document->execute(std::make_unique<editor::RenameEntity>(camera, "Dock camera")));
    });
    probe.actAt(5, [](editor::EditorDocument* document) {
        if (document != nullptr) {
            const auto nearIsland = entityNamed(*document, "Lighthouse on the near island");
            succeed(document->execute(std::make_unique<editor::DeleteEntity>(nearIsland)));
        }
    });
    probe.actAt(7, [](editor::EditorDocument* document) {
        if (document != nullptr) {
            succeed(document->undo());
        }
    });
    probe.actAt(9, [](editor::EditorDocument* document) {
        if (document != nullptr) {
            succeed(document->undo());
        }
    });
    const auto run = engine::app::run(probe, shellDesc(kFrames));
    REQUIRE(run.hasValue());
    checkCleanRun(run.value());
    REQUIRE(probe.treeMatched.size() == kFrames);
    for (std::size_t frame = 0; frame < kFrames; ++frame) {
        INFO("frame " << frame);
        CHECK(probe.treeMatched[frame]);
        CHECK(probe.itemsMatched[frame]);
    }
    // Built once at the start and once for each change of shape (the delete
    // and its undo); the renames only relabelled.
    CHECK(probe.shell().hierarchy().rebuildCount() == 3);
    CHECK(probe.shell().hierarchy().relabelCount() == 2);
    CHECK(probe.shell().hierarchy().rows().size() == 7);
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}
