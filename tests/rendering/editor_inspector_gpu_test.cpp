// GPU tier (M5.8): the inspector in the running editor, with the committed
// test project tests/projects/lighthouses. Nothing selected: the inspector
// says so. The floating lighthouse selected: its name, transform (the rotation
// as Euler angles), static mesh and parent link are shown, drawn as text in
// the inspector's area, the other sections are not; a change of the document
// reaches the fields at the end of its frame's update.
// Runs from the repository root (tests/rendering/CMakeLists.txt).

#include "EditorPixels.hpp"

#include "application/EditorShell.hpp"
#include "application/ShellLayout.hpp"
#include "editor/commands/ComponentCommands.hpp"
#include "editor/document/EditorDocument.hpp"
#include "panels/InspectorPanel.hpp"

#include "engine/app/Application.hpp"
#include "engine/core/Image.hpp"
#include "engine/scene/NameComponent.hpp"
#include "engine/scene/TransformComponent.hpp"

#include <catch2/catch_test_macros.hpp>

#include <Raven.h>

#include <cmath>
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

namespace {

constexpr int kWidth = 1280;
constexpr int kHeight = 760;
const std::filesystem::path kProject = "tests/projects/lighthouses";

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
        .window = {.title = "lpe editor inspector GPU test", .width = kWidth, .height = kHeight},
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

// What the inspector showed at the end of a frame's update.
struct Shown {
    std::optional<engine::scene::EntityId> entity;
    double positionX = 0.0;
    double rotationZ = 0.0;
    double scaleY = 0.0;
    bool transform = false;
    bool camera = false;
    bool mesh = false;
    bool parent = false;
    std::string name;
};

// The editor with actions at given frames (before the shell's update, as
// input arrives), what the inspector showed after each frame's update, and a
// screenshot of the last frame. Nothing here throws: Catch's REQUIRE would
// throw through the engine's loop, so the callbacks use CHECK.
class InspectorProbe final : public engine::app::Application {
public:
    using Action = std::function<void(editor::EditorDocument&)>;

    InspectorProbe(std::uint64_t frameCount, std::filesystem::path screenshot)
        : shell_(withProject()), frames_(frameCount), screenshot_(std::move(screenshot))
    {
    }

    void actAt(std::uint64_t frame, Action action) { actions_[frame] = std::move(action); }

    std::optional<engine::core::Error> start(engine::app::AppContext& context) override
    {
        auto error = shell_.start(context);
        if (!error && context.ui() != nullptr) {
            window_ = context.ui()->mainWindow();
        }
        return error;
    }

    void update(engine::app::AppContext& context, const engine::core::FrameTime& time) override
    {
        if (const auto action = actions_.find(time.frameIndex);
            action != actions_.end() && shell_.document() != nullptr) {
            action->second(*shell_.document());
        }
        shell_.update(context, time);
        shown.push_back(read());
        if (time.frameIndex + 1 == frames_) {
            context.requestScreenshot(screenshot_);
        }
    }

    void render(engine::app::AppContext& context) override { shell_.render(context); }
    void stop(engine::app::AppContext& context) override { shell_.stop(context); }

    [[nodiscard]] const editor::EditorShell& shell() const { return shell_; }

    std::vector<Shown> shown;

private:
    static editor::EditorOptions withProject()
    {
        editor::EditorOptions options;
        options.project = kProject;
        return options;
    }

    [[nodiscard]] Rid widget(const char* name) const { return Rwidget_find(window_, name); }
    [[nodiscard]] bool visible(const char* name) const
    {
        return Rwidget_get_visibility(widget(name)) == R_VISIBLE;
    }

    [[nodiscard]] Shown read() const
    {
        Shown now;
        now.entity = shell_.inspector().entity();
        now.positionX = Rspin_box_get_value(widget("inspector-position-x"));
        now.rotationZ = Rspin_box_get_value(widget("inspector-rotation-z"));
        now.scaleY = Rspin_box_get_value(widget("inspector-scale-y"));
        now.transform = visible("inspector-section-transform");
        now.camera = visible("inspector-section-camera");
        now.mesh = visible("inspector-section-mesh");
        now.parent = visible("inspector-section-parent");
        const char* name = Rtext_input_get_text(widget("inspector-name"));
        now.name = name != nullptr ? name : "";
        return now;
    }

    editor::EditorShell shell_;
    std::uint64_t frames_;
    std::filesystem::path screenshot_;
    std::map<std::uint64_t, Action> actions_;
    Rid window_ = R_INVALID_ID;
};

engine::core::Image readFrame(const std::filesystem::path& path)
{
    auto image = engine::core::readPng(path);
    REQUIRE(image.hasValue());
    return std::move(image).value();
}

} // namespace

TEST_CASE("The inspector shows the selected entity's components and follows the document",
          "[editor][inspector][gpu]")
{
    constexpr std::uint64_t kFrames = 30;
    const auto emptyPath = outputPath("editor-inspector-empty.png");
    const auto selectedPath = outputPath("editor-inspector-selected.png");

    InspectorProbe empty(kFrames, emptyPath);
    const auto emptyRun = engine::app::run(empty, shellDesc(kFrames));
    REQUIRE(emptyRun.hasValue());
    checkCleanRun(emptyRun.value());
    CHECK_FALSE(empty.shown.back().entity.has_value());

    InspectorProbe probe(kFrames, selectedPath);
    engine::scene::EntityId floating;
    probe.actAt(2, [&floating](editor::EditorDocument& document) {
        floating = entityNamed(document, "Floating lighthouse");
        document.selection().select(floating);
    });
    // A change of the position from outside the inspector.
    probe.actAt(6, [&floating](editor::EditorDocument& document) {
        auto transform = document.scene().component<engine::scene::TransformComponent>(floating);
        CHECK(transform.hasValue());
        if (!transform) {
            return;
        }
        engine::scene::TransformComponent moved = transform.value();
        moved.position.x = 61.5F;
        const auto error = document.execute(
            std::make_unique<editor::ReplaceComponent>(floating, editor::ComponentValue{moved}));
        CHECK_FALSE(error.has_value());
    });
    const auto run = engine::app::run(probe, shellDesc(kFrames));
    REQUIRE(run.hasValue());
    checkCleanRun(run.value());
    REQUIRE(probe.shown.size() == kFrames);

    // Nothing before the selection, the lighthouse from its frame on.
    CHECK_FALSE(probe.shown[1].entity.has_value());
    REQUIRE(probe.shown[2].entity.has_value());
    CHECK(*probe.shown[2].entity == floating);
    const Shown& selected = probe.shown[2];
    CHECK(selected.name == "Floating lighthouse");
    CHECK(selected.transform);
    CHECK(selected.mesh);
    CHECK(selected.parent);
    CHECK_FALSE(selected.camera);
    // Its stored rotation is 25 degrees about Z; its scale (3, 6, 3).
    CHECK(std::abs(selected.rotationZ - 25.0) < 1.0e-3);
    CHECK(selected.scaleY == 6.0);
    CHECK(selected.positionX == 60.0);
    // The change reaches the field in its frame.
    CHECK(probe.shown[5].positionX == 60.0);
    CHECK(probe.shown[6].positionX == 61.5);
    CHECK(probe.shown.back().positionX == 61.5);

    // Drawn: the inspector's area holds much more light text with the
    // lighthouse than with nothing selected.
    const engine::core::Image withEntity = readFrame(selectedPath);
    const engine::core::Image without = readFrame(emptyPath);
    const editor::ShellArea& inspector = lpe_gpu_test::area(probe.shell().areas(), "inspector");
    const int textWith =
        brightPixels(withEntity, inspector.x, inspector.y, inspector.width, inspector.height, 150);
    const int textWithout =
        brightPixels(without, inspector.x, inspector.y, inspector.width, inspector.height, 150);
    INFO(textWith << " light pixels in the inspector with the lighthouse, " << textWithout
                  << " without");
    CHECK(textWithout > 50);
    CHECK(textWith > textWithout * 5);

    std::error_code ignored;
    std::filesystem::remove(emptyPath, ignored);
    // Kept for a look when the test fails (in the temporary folder).
    if (textWith > textWithout * 5) {
        std::filesystem::remove(selectedPath, ignored);
    }
}
