// The hierarchy panel (editor/src/panels/HierarchyPanel, M5.7), headless:
// Raven's null backend and host platform with the editor's shell layout, a
// document in memory, and clicks, keys, text and drags injected as the SDL3
// host injects them. Each test runs frames as the editor does: input, then the
// panel's update(), then Raven's frame. After every action the tree must show
// exactly the scene's rows (hierarchyRows), and every action must be one undo
// step.

#include "../ui/RavenSession.hpp"
#include "DocumentTestSupport.hpp"

#include "application/ShellLayout.hpp"
#include "editor/commands/EntityCommands.hpp"
#include "editor/document/EditorDocument.hpp"
#include "editor/panels/HierarchyRows.hpp"
#include "panels/HierarchyPanel.hpp"

#include "engine/core/Log.hpp"
#include "engine/scene/NameComponent.hpp"
#include "engine/scene/ParentComponent.hpp"
#include "engine/scene/TransformComponent.hpp"

#include <catch2/catch_test_macros.hpp>

#include <Raven.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using editor::HierarchyPanel;
using editor::HierarchyRow;
using engine::scene::EntityId;
using engine::scene::NameComponent;
using engine::scene::ParentComponent;
using lpe_test::RavenErrors;
using lpe_test::RavenSession;

struct Point {
    int x = 0;
    int y = 0;
};

// The editor's warnings, as the console receives them.
class Warnings {
public:
    Warnings()
        : subscription_(engine::core::addLogListener([this](const engine::core::LogRecord& line) {
              if (line.level == engine::core::LogLevel::Warning && line.category == "editor") {
                  const std::scoped_lock lock(mutex_);
                  lines_.emplace_back(line.message);
              }
          }))
    {
    }
    Warnings(const Warnings&) = delete;
    Warnings& operator=(const Warnings&) = delete;
    Warnings(Warnings&&) = delete;
    Warnings& operator=(Warnings&&) = delete;
    ~Warnings() = default;

    std::vector<std::string> take()
    {
        const std::scoped_lock lock(mutex_);
        return std::exchange(lines_, {});
    }

private:
    std::mutex mutex_;
    std::vector<std::string> lines_;
    engine::core::LogSubscription subscription_;
};

// The entities of the test scene.
struct Entities {
    EntityId sun;
    EntityId tower;
    EntityId lamp;
    EntityId door;
    EntityId knob;
    EntityId unnamed;
};

class Fixture {
public:
    explicit Fixture(bool withDocument = true)
    {
        REQUIRE(session.started());
        const std::string_view xml = editor::layouts::shell();
        window = Rwindow_create_from_xml(xml.data(), xml.size());
        REQUIRE(window != R_INVALID_ID);
        Rwindow_show(window);
        Rhost_window_set_size(window, 1280, 760, 1.0F);
        Rhost_inject_focus(window, R_TRUE);
        Rdraw_frame();

        // The scene as opened: nothing to undo.
        engine::project::Project project = lpe_test::memoryProject();
        engine::scene::Scene& built = project.startScene;
        e.sun = add(built, "Sun");
        e.tower = add(built, "Tower");
        e.lamp = add(built, "Lamp", e.tower);
        e.door = add(built, "Door", e.tower);
        e.knob = add(built, "Knob", e.door);
        e.unnamed = add(built, "");
        document = std::make_unique<editor::EditorDocument>(std::move(project));

        const auto error = panel.attach(window, withDocument ? document.get() : nullptr);
        INFO((error ? error->message : std::string()));
        REQUIRE_FALSE(error.has_value());
        frame();
    }

    ~Fixture()
    {
        panel.detach();
        session.stop();
    }

    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;
    Fixture(Fixture&&) = delete;
    Fixture& operator=(Fixture&&) = delete;

    // One frame of the editor: the input came, the panel follows, Raven draws.
    void frame()
    {
        panel.update();
        Rdraw_frame();
    }

    [[nodiscard]] const engine::scene::Scene& scene() const { return document->scene(); }

    // A point on an entity's row, in the window's pixels (scale 1), away from
    // the branch arrow.
    [[nodiscard]] Point rowPoint(EntityId entity) const
    {
        Rtree_view_scroll_to_item(panel.tree(), panel.itemOf(entity));
        const std::optional<HierarchyPanel::RowRect> row = panel.rowRect(entity);
        REQUIRE(row.has_value());
        float x = 0.0F;
        float y = 0.0F;
        float width = 0.0F;
        float height = 0.0F;
        Rwidget_get_rect(panel.tree(), &x, &y, &width, &height);
        return {static_cast<int>(std::lround(x + row->x + row->width * 0.6F)),
                static_cast<int>(std::lround(y + row->y + row->height * 0.5F))};
    }

    // A point in the tree below its rows.
    [[nodiscard]] Point emptySpace() const
    {
        float x = 0.0F;
        float y = 0.0F;
        float width = 0.0F;
        float height = 0.0F;
        Rwidget_get_rect(panel.tree(), &x, &y, &width, &height);
        return {static_cast<int>(x + width * 0.5F), static_cast<int>(y + height - 8.0F)};
    }

    [[nodiscard]] Point centreOf(Rid widget) const
    {
        float x = 0.0F;
        float y = 0.0F;
        float width = 0.0F;
        float height = 0.0F;
        Rwidget_get_rect(widget, &x, &y, &width, &height);
        return {static_cast<int>(x + width * 0.5F), static_cast<int>(y + height * 0.5F)};
    }

    void click(Point at, int times = 1)
    {
        for (int i = 0; i < times; ++i) {
            Rhost_inject_mouse_move(window, at.x, at.y, 0);
            Rhost_inject_mouse_button(window, at.x, at.y, R_MOUSE_LEFT, R_TRUE, 0);
            Rhost_inject_mouse_button(window, at.x, at.y, R_MOUSE_LEFT, R_FALSE, 0);
        }
        forgetClicks();
    }
    void clickRow(EntityId entity) { click(rowPoint(entity)); }
    void clickWidget(Rid widget) { click(centreOf(widget)); }

    // Two quick left clicks on one spot are a double click: a middle click
    // makes the next left click a single one. It goes to the tree's empty
    // space, which ignores it: a press anywhere gives the keyboard to what it
    // hits, and the tree keeps it.
    void forgetClicks()
    {
        const Point empty = emptySpace();
        Rhost_inject_mouse_button(window, empty.x, empty.y, R_MOUSE_MIDDLE, R_TRUE, 0);
        Rhost_inject_mouse_button(window, empty.x, empty.y, R_MOUSE_MIDDLE, R_FALSE, 0);
    }

    void key(RKey which)
    {
        Rhost_inject_key(window, which, R_TRUE, 0, R_FALSE);
        Rhost_inject_key(window, which, R_FALSE, 0, R_FALSE);
    }

    void type(std::string_view text)
    {
        for (const char letter : text) {
            Rhost_inject_text(window,
                              static_cast<std::uint32_t>(static_cast<unsigned char>(letter)));
        }
    }

    // Drags `entity`'s row to `to`, through a few pointer moves, and drops it.
    void drag(EntityId entity, Point to)
    {
        const Point from = rowPoint(entity);
        Rhost_inject_mouse_move(window, from.x, from.y, 0);
        Rhost_inject_mouse_button(window, from.x, from.y, R_MOUSE_LEFT, R_TRUE, 0);
        // Far enough to start the drag (Raven: more than 5 units).
        Rhost_inject_mouse_move(window, from.x + 12, from.y, 0);
        for (int step = 1; step <= 4; ++step) {
            Rhost_inject_mouse_move(window, from.x + 12 + (to.x - from.x - 12) * step / 4,
                                    from.y + (to.y - from.y) * step / 4, 0);
        }
        Rhost_inject_mouse_button(window, to.x, to.y, R_MOUSE_LEFT, R_FALSE, 0);
        forgetClicks();
    }

    // The tree's items as rows, depth first, read from Raven.
    [[nodiscard]] std::vector<HierarchyRow> treeRows() const
    {
        std::vector<HierarchyRow> rows;
        const auto visit = [&](auto&& self, Rid item, EntityId parent, std::size_t depth) -> void {
            const char* text = Rtree_item_get_text(item);
            rows.push_back({panel.entityOf(item), parent, depth, text != nullptr ? text : ""});
            const EntityId entity = panel.entityOf(item);
            const int children = Rtree_item_get_child_count(item);
            for (int i = 0; i < children; ++i) {
                self(self, Rtree_item_get_child(item, i), entity, depth + 1);
            }
        };
        const int top = Rtree_view_get_item_count(panel.tree());
        for (int i = 0; i < top; ++i) {
            visit(visit, Rtree_view_get_item(panel.tree(), i), EntityId{}, 0);
        }
        return rows;
    }

    // The tree shows the scene, and the document's selection.
    void checkTree() const
    {
        const std::vector<HierarchyRow> expected = editor::hierarchyRows(scene());
        CHECK(treeRows() == expected);
        CHECK(panel.rows() == expected);
        const std::optional<EntityId> primary = document->selection().primary();
        const Rid selected = Rtree_view_get_selected_item(panel.tree());
        if (primary) {
            CHECK(selected == panel.itemOf(*primary));
        } else {
            CHECK(selected == R_INVALID_ID);
        }
        CHECK(RavenErrors::instance().count() == 0);
    }

    [[nodiscard]] bool fieldShown() const
    {
        return Rwidget_get_visibility(panel.renameField()) == R_VISIBLE;
    }
    [[nodiscard]] std::string fieldText() const
    {
        const char* text = Rtext_input_get_text(panel.renameField());
        return text != nullptr ? text : "";
    }
    [[nodiscard]] std::string nameOf(EntityId entity) const
    {
        const auto component = scene().component<NameComponent>(entity);
        return component ? component.value().value : std::string();
    }
    [[nodiscard]] EntityId parentOf(EntityId entity) const
    {
        const auto component = scene().component<ParentComponent>(entity);
        return component ? component.value().entity : EntityId{};
    }

    RavenSession session;
    Warnings warnings;
    Rid window = R_INVALID_ID;
    std::unique_ptr<editor::EditorDocument> document;
    Entities e;
    HierarchyPanel panel;

private:
    // An entity with a name (none for ""), a transform and a parent.
    static EntityId add(engine::scene::Scene& scene, std::string name,
                        std::optional<EntityId> parent = std::nullopt)
    {
        auto created = scene.createEntity();
        REQUIRE(created.hasValue());
        const EntityId entity = created.value();
        if (!name.empty()) {
            REQUIRE_FALSE(scene.addComponent(entity, NameComponent{std::move(name)}));
        }
        REQUIRE_FALSE(scene.addComponent(entity, engine::scene::TransformComponent{}));
        if (parent) {
            REQUIRE_FALSE(scene.addComponent(entity, ParentComponent{*parent}));
        }
        return entity;
    }
};

} // namespace

TEST_CASE("Without a project the hierarchy is empty and its buttons are disabled",
          "[editor][hierarchy][ui]")
{
    Fixture f(false);
    CHECK(Rtree_view_get_item_count(f.panel.tree()) == 0);
    CHECK(f.panel.rows().empty());
    CHECK(Rwidget_is_enabled(f.panel.createButton()) == R_FALSE);
    CHECK(Rwidget_is_enabled(f.panel.deleteButton()) == R_FALSE);
    CHECK_FALSE(f.fieldShown());
    // Clicking and pressing keys change nothing.
    f.clickWidget(f.panel.createButton());
    f.click(f.emptySpace());
    f.key(R_KEY_DELETE);
    f.key(R_KEY_F2);
    f.frame();
    CHECK(Rtree_view_get_item_count(f.panel.tree()) == 0);
    CHECK(f.document->undoCount() == 0);
    CHECK(RavenErrors::instance().count() == 0);
}

TEST_CASE("The hierarchy shows the scene's entities with their parent links",
          "[editor][hierarchy][ui]")
{
    Fixture f;
    f.checkTree();
    const std::vector<HierarchyRow>& rows = f.panel.rows();
    REQUIRE(rows.size() == 6);
    CHECK(rows[0].label == "Sun");
    CHECK(rows[1].label == "Tower");
    CHECK(rows[2].label == "Lamp");
    CHECK(rows[3].label == "Door");
    CHECK(rows[4].label == "Knob");
    CHECK(rows[5].label == f.e.unnamed.toString());
    CHECK(std::string(Rtree_item_get_text(f.panel.itemOf(f.e.knob))) == "Knob");
    CHECK(Rtree_item_get_parent(f.panel.itemOf(f.e.knob)) == f.panel.itemOf(f.e.door));
    // Branches are open, nothing is selected, Delete waits for a selection.
    CHECK(Rtree_item_is_expanded(f.panel.itemOf(f.e.tower)) != R_FALSE);
    CHECK(Rwidget_is_enabled(f.panel.createButton()) != R_FALSE);
    CHECK(Rwidget_is_enabled(f.panel.deleteButton()) == R_FALSE);
    CHECK(f.panel.rebuildCount() == 1);
    // A frame without a change of the document touches nothing.
    f.frame();
    CHECK(f.panel.rebuildCount() == 1);
    CHECK(f.panel.relabelCount() == 0);
}

TEST_CASE("Create makes one top-level entity named Entity, selected, undone and redone",
          "[editor][hierarchy][ui]")
{
    Fixture f;
    f.clickWidget(f.panel.createButton());
    f.frame();
    REQUIRE(f.document->undoCount() == 1);
    CHECK(f.document->undoName() == "Create entity");
    REQUIRE(f.scene().entityCount() == 7);
    const std::optional<EntityId> created = f.document->selection().primary();
    REQUIRE(created.has_value());
    CHECK(f.nameOf(*created) == editor::kNewEntityName);
    const auto transform = f.scene().component<engine::scene::TransformComponent>(*created);
    REQUIRE(transform.hasValue());
    CHECK(transform.value() == engine::scene::TransformComponent{});
    CHECK(f.parentOf(*created).isNull());
    CHECK(f.panel.rows().back().entity == *created);
    CHECK(Rwidget_is_enabled(f.panel.deleteButton()) != R_FALSE);
    f.checkTree();

    REQUIRE_FALSE(f.document->undo());
    f.frame();
    CHECK_FALSE(f.scene().contains(*created));
    CHECK(f.document->selection().empty());
    CHECK(Rwidget_is_enabled(f.panel.deleteButton()) == R_FALSE);
    f.checkTree();

    REQUIRE_FALSE(f.document->redo());
    f.frame();
    CHECK(f.scene().contains(*created));
    CHECK(f.document->selection().primary() == created);
    f.checkTree();
}

TEST_CASE("Delete removes the selected entity, from the key and from the button",
          "[editor][hierarchy][ui]")
{
    Fixture f;
    f.clickRow(f.e.door);
    f.frame();
    CHECK(f.document->selection().primary() == f.e.door);
    CHECK(Rwidget_is_enabled(f.panel.deleteButton()) != R_FALSE);
    f.checkTree();

    // The key, while the tree has the keyboard: its child goes to the top level.
    f.key(R_KEY_DELETE);
    f.frame();
    CHECK(f.document->undoCount() == 1);
    CHECK_FALSE(f.scene().contains(f.e.door));
    CHECK(f.parentOf(f.e.knob).isNull());
    CHECK(f.document->selection().empty());
    f.checkTree();

    // Undo brings it back under the tower, its child under it, selected.
    REQUIRE_FALSE(f.document->undo());
    f.frame();
    CHECK(f.scene().contains(f.e.door));
    CHECK(f.parentOf(f.e.knob) == f.e.door);
    CHECK(f.document->selection().primary() == f.e.door);
    f.checkTree();

    // The button.
    f.clickRow(f.e.lamp);
    f.frame();
    f.clickWidget(f.panel.deleteButton());
    f.frame();
    CHECK_FALSE(f.scene().contains(f.e.lamp));
    CHECK(f.document->undoCount() == 1);
    f.checkTree();

    // With nothing selected the key does nothing.
    f.click(f.emptySpace());
    f.frame();
    CHECK(f.document->selection().empty());
    f.key(R_KEY_DELETE);
    f.frame();
    CHECK(f.document->undoCount() == 1);
    f.checkTree();
}

TEST_CASE("Rename in place: F2, Enter or a double click; Enter or leaving the field renames",
          "[editor][hierarchy][ui]")
{
    Fixture f;
    f.clickRow(f.e.tower);
    f.frame();

    SECTION("F2 shows the name over the row; typing and Enter rename in one step")
    {
        f.key(R_KEY_F2);
        f.frame();
        REQUIRE(f.panel.renaming() == f.e.tower);
        CHECK(f.fieldShown());
        CHECK(Rwidget_is_focused(f.panel.renameField()) != R_FALSE);
        CHECK(f.fieldText() == "Tower");
        // Over the row.
        const auto row = f.panel.rowRect(f.e.tower);
        REQUIRE(row.has_value());
        float tx = 0.0F;
        float ty = 0.0F;
        float tw = 0.0F;
        float th = 0.0F;
        Rwidget_get_rect(f.panel.tree(), &tx, &ty, &tw, &th);
        float fx = 0.0F;
        float fy = 0.0F;
        float fw = 0.0F;
        float fh = 0.0F;
        Rwidget_get_rect(f.panel.renameField(), &fx, &fy, &fw, &fh);
        CHECK(std::abs(fx - (tx + row->x)) <= 1.0F);
        CHECK(std::abs(fy - (ty + row->y)) <= 1.0F);
        CHECK(std::abs(fw - row->width) <= 1.0F);
        CHECK(std::abs(fh - row->height) <= 1.0F);

        // The whole name is selected: typing replaces it.
        const auto rebuilds = f.panel.rebuildCount();
        f.type("Beacon");
        f.key(R_KEY_ENTER);
        f.frame();
        CHECK_FALSE(f.fieldShown());
        CHECK_FALSE(f.panel.renaming().has_value());
        CHECK(f.nameOf(f.e.tower) == "Beacon");
        CHECK(f.document->undoCount() == 1);
        CHECK(f.document->undoName() == "Rename entity");
        // Relabelled, not rebuilt; the tree has the keyboard again.
        CHECK(f.panel.rebuildCount() == rebuilds);
        CHECK(f.panel.relabelCount() >= 1);
        CHECK(std::string(Rtree_item_get_text(f.panel.itemOf(f.e.tower))) == "Beacon");
        CHECK(Rwidget_is_focused(f.panel.tree()) != R_FALSE);
        f.checkTree();

        REQUIRE_FALSE(f.document->undo());
        f.frame();
        CHECK(f.nameOf(f.e.tower) == "Tower");
        f.checkTree();
    }

    SECTION("Escape leaves the name as it was")
    {
        f.key(R_KEY_F2);
        f.frame();
        f.type("Gone");
        f.key(R_KEY_ESCAPE);
        f.frame();
        CHECK_FALSE(f.fieldShown());
        CHECK(f.nameOf(f.e.tower) == "Tower");
        CHECK(f.document->undoCount() == 0);
        CHECK(Rwidget_is_focused(f.panel.tree()) != R_FALSE);
        f.checkTree();
    }

    SECTION("A double click starts the rename; a click elsewhere renames")
    {
        f.click(f.rowPoint(f.e.lamp), 2);
        f.frame();
        REQUIRE(f.panel.renaming() == f.e.lamp);
        CHECK(f.fieldText() == "Lamp");
        f.type("Lantern");
        // The pointer goes to another row: the field loses the keyboard.
        f.clickRow(f.e.sun);
        f.frame();
        CHECK_FALSE(f.fieldShown());
        CHECK(f.nameOf(f.e.lamp) == "Lantern");
        CHECK(f.document->undoCount() == 1);
        CHECK(f.document->selection().primary() == f.e.sun);
        f.checkTree();
    }

    SECTION("Enter in the tree starts the rename too")
    {
        f.key(R_KEY_ENTER);
        f.frame();
        CHECK(f.panel.renaming() == f.e.tower);
        f.key(R_KEY_ESCAPE);
        f.frame();
        CHECK_FALSE(f.panel.renaming().has_value());
    }

    SECTION("An unchanged name is no step; an empty one is refused with the scene's message")
    {
        f.key(R_KEY_F2);
        f.frame();
        f.key(R_KEY_ENTER);
        f.frame();
        CHECK(f.document->undoCount() == 0);
        CHECK(f.warnings.take().empty());

        f.key(R_KEY_F2);
        f.frame();
        // The selected name is deleted.
        f.key(R_KEY_BACKSPACE);
        CHECK(f.fieldText().empty());
        f.key(R_KEY_ENTER);
        f.frame();
        CHECK(f.nameOf(f.e.tower) == "Tower");
        CHECK(f.document->undoCount() == 0);
        const std::vector<std::string> warnings = f.warnings.take();
        REQUIRE(warnings.size() == 1);
        CHECK(warnings[0] ==
              "hierarchy: cannot rename 'Tower': name component: value: must not be empty");
        f.checkTree();
    }

    SECTION("An entity without a name is renamed from an empty field")
    {
        f.clickRow(f.e.unnamed);
        f.frame();
        f.key(R_KEY_F2);
        f.frame();
        CHECK(f.fieldText().empty());
        // Nothing typed: no name, no step.
        f.key(R_KEY_ENTER);
        f.frame();
        CHECK(f.document->undoCount() == 0);
        CHECK(f.warnings.take().empty());
        f.key(R_KEY_F2);
        f.frame();
        f.type("Named");
        f.key(R_KEY_ENTER);
        f.frame();
        CHECK(f.nameOf(f.e.unnamed) == "Named");
        CHECK(f.document->undoCount() == 1);
        f.checkTree();
    }

    SECTION("A rename ends when its entity leaves the scene")
    {
        f.key(R_KEY_F2);
        f.frame();
        REQUIRE(f.panel.renaming() == f.e.tower);
        REQUIRE_FALSE(f.document->execute(std::make_unique<editor::DeleteEntity>(f.e.tower)));
        f.frame();
        CHECK_FALSE(f.panel.renaming().has_value());
        CHECK_FALSE(f.fieldShown());
        CHECK(f.document->undoCount() == 1);
        f.checkTree();
    }
}

TEST_CASE("Dragging an entity onto another makes it its child; onto the empty space, top level",
          "[editor][hierarchy][ui]")
{
    Fixture f;

    // Lamp under Sun.
    f.drag(f.e.lamp, f.rowPoint(f.e.sun));
    f.frame();
    CHECK(f.parentOf(f.e.lamp) == f.e.sun);
    CHECK(f.document->undoCount() == 1);
    CHECK(f.document->undoName() == "Set parent");
    f.checkTree();

    // Knob to the top level.
    f.drag(f.e.knob, f.emptySpace());
    f.frame();
    CHECK(f.parentOf(f.e.knob).isNull());
    CHECK(f.document->undoCount() == 2);
    CHECK(f.document->undoName() == "Clear parent");
    f.checkTree();

    // Tower under its own grandchild: the scene refuses the cycle, and says so.
    f.drag(f.e.knob, f.rowPoint(f.e.door));
    f.frame();
    REQUIRE(f.parentOf(f.e.knob) == f.e.door);
    const auto steps = f.document->undoCount();
    f.warnings.take();
    f.drag(f.e.tower, f.rowPoint(f.e.knob));
    f.frame();
    CHECK(f.parentOf(f.e.tower).isNull());
    CHECK(f.document->undoCount() == steps);
    const std::vector<std::string> warnings = f.warnings.take();
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0].starts_with("hierarchy: cannot set the parent of 'Tower': "));
    CHECK(warnings[0].find("cycle") != std::string::npos);
    f.checkTree();

    // Onto itself, or where it is already: nothing, and no message.
    f.drag(f.e.sun, f.rowPoint(f.e.sun));
    f.drag(f.e.unnamed, f.emptySpace());
    f.frame();
    CHECK(f.document->undoCount() == steps);
    CHECK(f.warnings.take().empty());
    f.checkTree();

    // Undo and redo all of it.
    while (f.document->canUndo()) {
        REQUIRE_FALSE(f.document->undo());
        f.frame();
        f.checkTree();
    }
    CHECK(f.parentOf(f.e.lamp) == f.e.tower);
    CHECK(f.parentOf(f.e.knob) == f.e.door);
    while (f.document->canRedo()) {
        REQUIRE_FALSE(f.document->redo());
        f.frame();
        f.checkTree();
    }
    CHECK(f.parentOf(f.e.lamp) == f.e.sun);
}

TEST_CASE("The hierarchy and the document share the selection", "[editor][hierarchy][ui]")
{
    Fixture f;
    f.clickRow(f.e.knob);
    f.frame();
    CHECK(f.document->selection().ids() == std::vector<EntityId>{f.e.knob});

    // From the document (another panel, the viewport later): the tree follows.
    f.document->selection().select(f.e.sun);
    f.frame();
    CHECK(Rtree_view_get_selected_item(f.panel.tree()) == f.panel.itemOf(f.e.sun));
    f.document->selection().clear();
    f.frame();
    CHECK(Rtree_view_get_selected_item(f.panel.tree()) == R_INVALID_ID);
    CHECK(Rwidget_is_enabled(f.panel.deleteButton()) == R_FALSE);

    // Keys move the selection in the tree, and so in the document.
    f.clickRow(f.e.tower);
    f.key(R_KEY_DOWN);
    f.frame();
    CHECK(f.document->selection().primary() == f.e.lamp);

    // A click in the empty space selects nothing.
    f.click(f.emptySpace());
    f.frame();
    CHECK(f.document->selection().empty());
    f.checkTree();
}

TEST_CASE("Branches the user closed stay closed when the tree is rebuilt",
          "[editor][hierarchy][ui]")
{
    Fixture f;
    // Left closes the selected branch.
    f.clickRow(f.e.door);
    f.key(R_KEY_LEFT);
    f.frame();
    CHECK(Rtree_item_is_expanded(f.panel.itemOf(f.e.door)) == R_FALSE);

    const auto rebuilds = f.panel.rebuildCount();
    f.clickWidget(f.panel.createButton());
    f.frame();
    CHECK(f.panel.rebuildCount() == rebuilds + 1);
    CHECK(Rtree_item_is_expanded(f.panel.itemOf(f.e.door)) == R_FALSE);
    CHECK(Rtree_item_is_expanded(f.panel.itemOf(f.e.tower)) != R_FALSE);
    f.checkTree();

    // Selecting what the closed branch hides opens it.
    f.document->selection().select(f.e.knob);
    f.frame();
    CHECK(Rtree_item_is_expanded(f.panel.itemOf(f.e.door)) != R_FALSE);
    f.checkTree();
}

TEST_CASE("The tree matches the scene through a long mix of actions, undo and redo",
          "[editor][hierarchy][ui]")
{
    Fixture f;
    std::mt19937 random(5);
    const auto pick = [&random](std::size_t count) {
        return std::uniform_int_distribution<std::size_t>(0, count - 1)(random);
    };
    const auto anyEntity = [&]() -> std::optional<EntityId> {
        const auto ids = f.scene().entityIds();
        if (ids.empty()) {
            return std::nullopt;
        }
        return ids[pick(ids.size())];
    };
    std::size_t actions = 0;
    for (int step = 0; step < 240; ++step) {
        INFO("step " << step);
        switch (pick(9)) {
        case 0:
            // At most 20 entities: every row stays in the tree's visible part.
            if (f.scene().entityCount() < 20) {
                f.clickWidget(f.panel.createButton());
            }
            break;
        case 1:
            if (const auto entity = anyEntity()) {
                f.clickRow(*entity);
                f.frame();
                f.key(R_KEY_DELETE);
            }
            break;
        case 2:
            if (const auto entity = anyEntity()) {
                f.clickRow(*entity);
                f.frame();
                f.key(R_KEY_F2);
                f.frame();
                f.type("N" + std::to_string(step));
                f.key(R_KEY_ENTER);
            }
            break;
        case 3: {
            const auto child = anyEntity();
            const auto parent = anyEntity();
            if (child && parent && *child != *parent) {
                f.drag(*child, f.rowPoint(*parent));
            }
            break;
        }
        case 4:
            if (const auto child = anyEntity()) {
                f.drag(*child, f.emptySpace());
            }
            break;
        case 5:
        case 6:
            if (f.document->canUndo()) {
                REQUIRE_FALSE(f.document->undo());
            }
            break;
        case 7:
            if (f.document->canRedo() && f.scene().entityCount() < 20) {
                REQUIRE_FALSE(f.document->redo());
            }
            break;
        default:
            // A change from outside the panel: a parent link by command.
            if (const auto child = anyEntity()) {
                if (const auto parent = anyEntity()) {
                    (void)f.document->execute(std::make_unique<editor::SetParent>(*child, *parent));
                }
            }
            break;
        }
        f.frame();
        f.checkTree();
        ++actions;
    }
    CHECK(actions == 240);
    // Rebuilt for changes of shape only, relabelled for renames.
    CHECK(f.panel.rebuildCount() > 10);
    CHECK(f.panel.relabelCount() > 5);
}

TEST_CASE("The hierarchy needs its widgets in the layout", "[editor][hierarchy][ui]")
{
    RavenSession session;
    REQUIRE(session.started());
    constexpr std::string_view kNoTree =
        R"(<Window Title="no tree" Width="320" Height="200"><VBox Name="hierarchy"/></Window>)";
    const Rid window = lpe_test::createHostWindow(kNoTree, 320, 200);
    REQUIRE(window != R_INVALID_ID);
    HierarchyPanel panel;
    const auto error = panel.attach(window, nullptr);
    REQUIRE(error.has_value());
    CHECK(error->message == "the shell layout has no widget named 'hierarchy-tree'");
    CHECK_FALSE(panel.attached());
    session.stop();
}
