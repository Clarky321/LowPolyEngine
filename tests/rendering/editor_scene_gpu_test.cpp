// GPU tier (M5.6): the committed test project tests/projects/lighthouses in
// the editor. Its five lighthouses are drawn in the viewport where their
// transforms put them, inside the generated world: a frame of the editor with
// the project differs from one without it (the same world: the project has
// the default world settings) only where the lighthouses are. Edits through
// the document's commands and their undo reach the viewport in the same
// frame, the scene is read once per change rather than every frame, and the
// renderer's resources stay level through them.
//
// The simulation time is held at zero, so the clouds stand still and two runs
// draw the same world.
// Runs from the repository root (tests/rendering/CMakeLists.txt).

#include "EditorPixels.hpp"

#include "application/EditorShell.hpp"
#include "application/ShellLayout.hpp"
#include "editor/commands/EntityCommands.hpp"
#include "editor/document/EditorDocument.hpp"
#include "viewport/WorldViewport.hpp"

#include "engine/app/Application.hpp"
#include "engine/camera/Camera.hpp"
#include "engine/core/Image.hpp"
#include "engine/render/Renderer.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/world/SceneDraws.hpp"

#include <glm/common.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <initializer_list>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

using lpe_gpu_test::difference;
using lpe_gpu_test::pixelAt;

namespace {

constexpr int kWidth = 1024;
constexpr int kHeight = 640;
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
        .window = {.title = "lpe editor scene GPU test", .width = kWidth, .height = kHeight},
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

editor::EditorOptions withProject()
{
    editor::EditorOptions options;
    options.project = kProject;
    return options;
}

// One frame of the run, after the shell's update.
struct FrameRecord {
    engine::render::ResourceCounts counts;
    std::size_t drawn = 0;
    std::uint64_t sceneUpdates = 0;
};

// What the viewport showed in the last frame: enough to project the
// lighthouses into the frame.
struct ViewRecord {
    engine::camera::Camera camera;
    editor::ViewportSize size;
    std::vector<engine::world::MeshDraw> draws;
    // The lighthouse mesh's bounds, in its own coordinates.
    engine::math::Vec3 low{0.0F};
    engine::math::Vec3 high{0.0F};
};

// The editor with frozen simulation time, document actions at given frames
// (before the shell's update, so the viewport follows them in that frame),
// and screenshots at given frames. Nothing here throws: Catch's REQUIRE would
// throw through the engine's loop, so the callbacks use CHECK.
class SceneProbe final : public engine::app::Application {
public:
    using Action = std::function<void(editor::EditorDocument&)>;

    SceneProbe(editor::EditorOptions options, std::uint64_t frameCount)
        : shell_(std::move(options)), frames_(frameCount)
    {
    }

    void actAt(std::uint64_t frame, Action action) { actions_[frame] = std::move(action); }
    void screenshotAt(std::uint64_t frame, std::filesystem::path path)
    {
        screenshots_[frame] = std::move(path);
    }

    std::optional<engine::core::Error> start(engine::app::AppContext& context) override
    {
        return shell_.start(context);
    }

    void update(engine::app::AppContext& context, const engine::core::FrameTime& time) override
    {
        if (const auto action = actions_.find(time.frameIndex);
            action != actions_.end() && shell_.document() != nullptr) {
            action->second(*shell_.document());
        }
        const engine::core::FrameTime frozen{.deltaSeconds = 0.0,
                                             .rawDeltaSeconds = time.rawDeltaSeconds,
                                             .elapsedSeconds = 0.0,
                                             .frameIndex = time.frameIndex};
        shell_.update(context, frozen);
        const editor::WorldViewport* world = shell_.world();
        CHECK(world != nullptr);
        if (world == nullptr) {
            return;
        }
        frames.push_back({context.renderer().resourceCounts(), world->scene().drawnCount(),
                          world->sceneUpdates()});
        if (const auto shot = screenshots_.find(time.frameIndex); shot != screenshots_.end()) {
            context.requestScreenshot(shot->second);
        }
        if (time.frameIndex + 1 == frames_) {
            view.camera = world->camera();
            view.draws = world->scene().draws();
            for (const auto& draw : view.draws) {
                const auto* asset = world->scene().meshAssets().find(draw.mesh);
                CHECK((asset != nullptr && asset->mesh && !asset->mesh->positions.empty()));
                if (asset == nullptr || !asset->mesh || asset->mesh->positions.empty()) {
                    continue;
                }
                const auto& positions = asset->mesh->positions;
                view.low = positions.front();
                view.high = positions.front();
                for (const auto& p : positions) {
                    view.low = glm::min(view.low, p);
                    view.high = glm::max(view.high, p);
                }
            }
        }
    }

    void render(engine::app::AppContext& context) override { shell_.render(context); }

    void stop(engine::app::AppContext& context) override
    {
        if (const editor::WorldViewport* world = shell_.world()) {
            view.size = world->lastDrawnSize();
        }
        countsBeforeStop = context.renderer().resourceCounts();
        shell_.stop(context);
        countsAfterStop = context.renderer().resourceCounts();
    }

    [[nodiscard]] const std::vector<editor::ShellArea>& areas() const { return shell_.areas(); }
    [[nodiscard]] const editor::EditorDocument* document() const { return shell_.document(); }

    std::vector<FrameRecord> frames;
    ViewRecord view;
    engine::render::ResourceCounts countsBeforeStop;
    engine::render::ResourceCounts countsAfterStop;

private:
    editor::EditorShell shell_;
    std::uint64_t frames_;
    std::map<std::uint64_t, Action> actions_;
    std::map<std::uint64_t, std::filesystem::path> screenshots_;
};

struct Box {
    int left = std::numeric_limits<int>::max();
    int top = std::numeric_limits<int>::max();
    int right = std::numeric_limits<int>::min();
    int bottom = std::numeric_limits<int>::min();

    [[nodiscard]] bool contains(int x, int y, int margin) const
    {
        return x >= left - margin && x <= right + margin && y >= top - margin &&
               y <= bottom + margin;
    }
};

struct Rect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

Rect viewportRect(const std::vector<editor::ShellArea>& areas)
{
    const editor::ShellArea& a = lpe_gpu_test::area(areas, "viewport");
    return {a.x, a.y, a.width, a.height};
}

// The frame's pixels each lighthouse can cover: its mesh's bounds through its
// model matrix and the viewport camera, in the window's pixels.
std::vector<Box> lighthouseBoxes(const ViewRecord& view, const Rect& viewport)
{
    const float aspect = engine::camera::aspectRatio(view.size.width, view.size.height);
    const engine::math::Mat4 viewProjection =
        engine::camera::projectionMatrix(view.camera, aspect) *
        engine::camera::viewMatrix(view.camera);
    std::vector<Box> boxes;
    for (const auto& draw : view.draws) {
        Box box;
        for (int corner = 0; corner < 8; ++corner) {
            const engine::math::Vec3 local{(corner & 1) != 0 ? view.high.x : view.low.x,
                                           (corner & 2) != 0 ? view.high.y : view.low.y,
                                           (corner & 4) != 0 ? view.high.z : view.low.z};
            const engine::math::Vec4 clip =
                viewProjection * draw.model * engine::math::Vec4{local, 1.0F};
            REQUIRE(clip.w > 0.0F);
            const float x = static_cast<float>(viewport.x) +
                            (clip.x / clip.w * 0.5F + 0.5F) * static_cast<float>(view.size.width);
            const float y = static_cast<float>(viewport.y) +
                            (0.5F - clip.y / clip.w * 0.5F) * static_cast<float>(view.size.height);
            box.left = std::min(box.left, static_cast<int>(std::floor(x)));
            box.right = std::max(box.right, static_cast<int>(std::ceil(x)));
            box.top = std::min(box.top, static_cast<int>(std::floor(y)));
            box.bottom = std::max(box.bottom, static_cast<int>(std::ceil(y)));
        }
        boxes.push_back(box);
    }
    return boxes;
}

// Pixels of the viewport (without its border pixels) that differ between two
// frames by more than a rounding step.
struct Change {
    int x = 0;
    int y = 0;
};

std::vector<Change> changes(const engine::core::Image& a, const engine::core::Image& b,
                            const Rect& viewport)
{
    REQUIRE(a.width == b.width);
    REQUIRE(a.height == b.height);
    std::vector<Change> changed;
    for (int y = viewport.y + 2; y < viewport.y + viewport.height - 2; ++y) {
        for (int x = viewport.x + 2; x < viewport.x + viewport.width - 2; ++x) {
            if (difference(pixelAt(a, x, y), pixelAt(b, x, y)) > 16) {
                changed.push_back({x, y});
            }
        }
    }
    return changed;
}

engine::core::Image readFrame(const std::filesystem::path& path)
{
    auto image = engine::core::readPng(path);
    REQUIRE(image.hasValue());
    return std::move(image).value();
}

// Called from the probe's update: CHECK, not REQUIRE.
engine::scene::EntityId entityNamed(const editor::EditorDocument& document, std::string_view name)
{
    for (const engine::scene::EntityId entity : document.scene().entityIds()) {
        const auto component = document.scene().component<engine::scene::NameComponent>(entity);
        if (component && component.value().value == name) {
            return entity;
        }
    }
    INFO("no entity named " << name);
    CHECK(false);
    return {};
}

void succeed(const std::optional<engine::core::Error>& error)
{
    INFO((error ? error->message : std::string()));
    CHECK_FALSE(error.has_value());
}

} // namespace

TEST_CASE("The editor shows the test project's lighthouses where their transforms put them, "
          "and follows edits and undo",
          "[editor][viewport][scene][gpu]")
{
    constexpr std::uint64_t kFrames = 12;
    const auto worldPath = outputPath("editor-scene-world.png");
    const auto scenePath = outputPath("editor-scene-lighthouses.png");

    // The world alone: the editor without a project.
    SceneProbe worldOnly(editor::EditorOptions{}, kFrames);
    worldOnly.screenshotAt(kFrames - 1, worldPath);
    const auto worldRun = engine::app::run(worldOnly, shellDesc(kFrames));
    REQUIRE(worldRun.hasValue());
    checkCleanRun(worldRun.value());
    CHECK(worldOnly.document() == nullptr);
    CHECK(worldOnly.frames.back().drawn == 0);
    CHECK(worldOnly.frames.back().sceneUpdates == 0);

    // The same world with the project's scene.
    SceneProbe lighthouses(withProject(), kFrames);
    lighthouses.screenshotAt(kFrames - 1, scenePath);
    const auto sceneRun = engine::app::run(lighthouses, shellDesc(kFrames));
    REQUIRE(sceneRun.hasValue());
    checkCleanRun(sceneRun.value());
    REQUIRE(lighthouses.document() != nullptr);
    CHECK(lighthouses.document()->project().document.name == "Lighthouses");
    // Five lighthouses, one mesh and one material more than the world alone,
    // the scene read once.
    REQUIRE(lighthouses.view.draws.size() == 5);
    CHECK(lighthouses.frames.back().drawn == 5);
    CHECK(lighthouses.frames.back().sceneUpdates == 1);
    const engine::render::ResourceCounts worldCounts = worldOnly.frames.back().counts;
    const engine::render::ResourceCounts sceneCounts = lighthouses.frames.back().counts;
    CHECK(sceneCounts.meshes == worldCounts.meshes + 1);
    CHECK(sceneCounts.materials == worldCounts.materials + 1);
    CHECK(sceneCounts.textures == worldCounts.textures);
    CHECK(lighthouses.countsAfterStop == engine::render::ResourceCounts{});

    // The frames differ inside the lighthouses' boxes only, and inside each.
    const engine::core::Image world = readFrame(worldPath);
    const engine::core::Image scene = readFrame(scenePath);
    const Rect viewport = viewportRect(lighthouses.areas());
    REQUIRE(std::abs(lighthouses.view.size.width - viewport.width) <= 1);
    REQUIRE(std::abs(lighthouses.view.size.height - viewport.height) <= 1);
    const std::vector<Box> boxes = lighthouseBoxes(lighthouses.view, viewport);
    const std::vector<Change> changed = changes(world, scene, viewport);
    std::vector<std::size_t> perBox(boxes.size(), 0);
    std::size_t outside = 0;
    for (const Change& change : changed) {
        bool inBox = false;
        for (std::size_t i = 0; i < boxes.size(); ++i) {
            if (boxes[i].contains(change.x, change.y, 2)) {
                ++perBox[i];
                inBox = true;
            }
        }
        outside += inBox ? 0U : 1U;
    }
    for (std::size_t i = 0; i < boxes.size(); ++i) {
        const Box& box = boxes[i];
        INFO("lighthouse " << i << ": box (" << box.left << ", " << box.top << ") to (" << box.right
                           << ", " << box.bottom << "), " << perBox[i] << " pixels changed");
        CHECK(box.left >= viewport.x);
        CHECK(box.right < viewport.x + viewport.width);
        CHECK(perBox[i] >= 30);
    }
    INFO(changed.size() << " pixels changed, " << outside << " outside every lighthouse's box");
    CHECK(changed.size() > 500);
    CHECK(outside == 0);

    // Edits and undo through the document's commands, each reaching the
    // viewport in its frame, with the renderer's resources level throughout.
    constexpr std::uint64_t kEditFrames = 24;
    const auto deletedPath = outputPath("editor-scene-deleted.png");
    const auto restoredPath = outputPath("editor-scene-restored.png");
    SceneProbe edited(withProject(), kEditFrames);
    engine::scene::EntityId nearIsland;
    engine::scene::EntityId added;
    edited.actAt(3, [&](editor::EditorDocument& document) {
        nearIsland = entityNamed(document, "Lighthouse on the near island");
        succeed(document.execute(std::make_unique<editor::DeleteEntity>(nearIsland)));
    });
    edited.screenshotAt(3, deletedPath);
    edited.actAt(5, [&](editor::EditorDocument& document) {
        engine::scene::TransformComponent at;
        at.position = {0.0F, 120.0F, 0.0F};
        at.scale = engine::math::Vec3{6.0F};
        const auto mesh = document.assets().assets().front().id;
        auto create = std::make_unique<editor::CreateEntity>(
            std::vector<editor::ComponentValue>{engine::scene::NameComponent{"Added lighthouse"},
                                                at, engine::scene::StaticMeshComponent{mesh, {}}});
        const editor::CreateEntity* raw = create.get();
        succeed(document.execute(std::move(create)));
        added = raw->entity();
    });
    edited.actAt(7, [](editor::EditorDocument& document) { succeed(document.undo()); });
    edited.actAt(8, [](editor::EditorDocument& document) { succeed(document.undo()); });
    edited.actAt(10, [](editor::EditorDocument& document) { succeed(document.redo()); });
    edited.actAt(11, [](editor::EditorDocument& document) { succeed(document.redo()); });
    edited.actAt(13, [](editor::EditorDocument& document) { succeed(document.undo()); });
    edited.actAt(14, [](editor::EditorDocument& document) { succeed(document.undo()); });
    edited.screenshotAt(kEditFrames - 1, restoredPath);
    const auto editRun = engine::app::run(edited, shellDesc(kEditFrames));
    REQUIRE(editRun.hasValue());
    checkCleanRun(editRun.value());
    REQUIRE(edited.frames.size() == kEditFrames);

    // Drawn lighthouses after each frame's action, and the scene read once at
    // start and once per change, not in every frame.
    const std::vector<std::size_t> drawn{5, 5, 5, 4, 4, 5, 5, 4, 5, 5, 4, 5,
                                         5, 4, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5};
    const std::vector<std::uint64_t> reads{1, 1, 1, 2, 2, 3, 3, 4, 5, 5, 6, 7,
                                           7, 8, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9};
    for (std::size_t frame = 0; frame < kEditFrames; ++frame) {
        INFO("frame " << frame);
        CHECK(edited.frames[frame].drawn == drawn[frame]);
        CHECK(edited.frames[frame].sceneUpdates == reads[frame]);
        CHECK(edited.frames[frame].counts == sceneCounts);
    }
    REQUIRE(edited.document() != nullptr);
    CHECK(edited.document()->version() == 8);
    CHECK(edited.document()->scene().contains(nearIsland));
    CHECK_FALSE(edited.document()->scene().contains(added));
    CHECK(edited.countsAfterStop == engine::render::ResourceCounts{});

    // Deleted: only the near lighthouse's box changed. All undone: the frame
    // of the unedited scene.
    const std::vector<Change> deleted = changes(scene, readFrame(deletedPath), viewport);
    std::size_t nearChanges = 0;
    std::size_t elsewhere = 0;
    for (const Change& change : deleted) {
        // The near lighthouse is the first mesh entity of the scene.
        if (boxes[0].contains(change.x, change.y, 2)) {
            ++nearChanges;
        } else {
            ++elsewhere;
        }
    }
    INFO(nearChanges << " pixels changed in the deleted lighthouse's box, " << elsewhere
                     << " elsewhere");
    CHECK(nearChanges >= 30);
    CHECK(elsewhere == 0);
    CHECK(changes(scene, readFrame(restoredPath), viewport).empty());

    std::error_code ignored;
    for (const auto& path : {worldPath, scenePath, deletedPath, restoredPath}) {
        std::filesystem::remove(path, ignored);
    }
}
