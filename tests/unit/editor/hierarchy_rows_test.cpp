// The hierarchy's rows (editor/panels/HierarchyRows, M5.7), headless: the
// scene's entities as the hierarchy panel shows them, depth first, top-level
// entities and siblings in creation order, each shown by its name or its ID.

#include "editor/panels/HierarchyRows.hpp"

#include "engine/core/Uuid.hpp"
#include "engine/scene/NameComponent.hpp"
#include "engine/scene/ParentComponent.hpp"
#include "engine/scene/Scene.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

using editor::HierarchyRow;
using editor::hierarchyRows;
using editor::sameTree;
using engine::scene::EntityId;
using engine::scene::NameComponent;
using engine::scene::ParentComponent;
using engine::scene::Scene;

namespace {

Scene emptyScene()
{
    auto scene = Scene::create("Rows", engine::core::UuidGenerator{7});
    REQUIRE(scene.hasValue());
    return std::move(scene).value();
}

EntityId entity(Scene& scene, std::string name = {})
{
    auto created = scene.createEntity();
    REQUIRE(created.hasValue());
    if (!name.empty()) {
        REQUIRE_FALSE(scene.addComponent(created.value(), NameComponent{std::move(name)}));
    }
    return created.value();
}

void link(Scene& scene, EntityId child, EntityId parent)
{
    if (scene.hasComponent<ParentComponent>(child)) {
        REQUIRE_FALSE(scene.replaceComponent(child, ParentComponent{parent}));
    } else {
        REQUIRE_FALSE(scene.addComponent(child, ParentComponent{parent}));
    }
}

std::vector<std::string> labels(const std::vector<HierarchyRow>& rows)
{
    std::vector<std::string> texts;
    for (const HierarchyRow& row : rows) {
        texts.push_back(std::string(row.depth * 2, ' ') + row.label);
    }
    return texts;
}

} // namespace

TEST_CASE("An empty scene has no rows", "[editor][hierarchy]")
{
    const Scene scene = emptyScene();
    CHECK(hierarchyRows(scene).empty());
}

TEST_CASE("Rows list every entity depth first, siblings in creation order", "[editor][hierarchy]")
{
    Scene scene = emptyScene();
    const EntityId sun = entity(scene, "Sun");
    const EntityId tower = entity(scene, "Tower");
    const EntityId lamp = entity(scene, "Lamp");
    const EntityId unnamed = entity(scene);
    const EntityId door = entity(scene, "Door");
    const EntityId knob = entity(scene, "Knob");
    // A child created before its parent is still listed under it.
    link(scene, lamp, tower);
    link(scene, door, tower);
    link(scene, knob, door);
    link(scene, sun, knob);

    const std::vector<HierarchyRow> rows = hierarchyRows(scene);
    CHECK(labels(rows) == std::vector<std::string>{
                              "Tower",
                              "  Lamp",
                              "  Door",
                              "    Knob",
                              "      Sun",
                              unnamed.toString(),
                          });
    REQUIRE(rows.size() == 6);
    CHECK(rows[0].entity == tower);
    CHECK(rows[0].parent.isNull());
    CHECK(rows[1].entity == lamp);
    CHECK(rows[1].parent == tower);
    CHECK(rows[3].entity == knob);
    CHECK(rows[3].parent == door);
    CHECK(rows[4].entity == sun);
    CHECK(rows[4].depth == 3);
    // Without a name, the ID.
    CHECK(rows[5].entity == unnamed);
    CHECK(rows[5].label == unnamed.toString());
    CHECK(editor::entityLabel(scene, unnamed) == unnamed.toString());
    CHECK(editor::entityLabel(scene, door) == "Door");

    SECTION("A deleted parent's children move to the top level, in creation order")
    {
        REQUIRE_FALSE(scene.destroyEntity(tower));
        CHECK(labels(hierarchyRows(scene)) == std::vector<std::string>{
                                                  "Lamp",
                                                  unnamed.toString(),
                                                  "Door",
                                                  "  Knob",
                                                  "    Sun",
                                              });
    }

    SECTION("Names may repeat: the rows keep the entities apart")
    {
        REQUIRE_FALSE(scene.replaceComponent(lamp, NameComponent{"Door"}));
        const std::vector<HierarchyRow> renamed = hierarchyRows(scene);
        CHECK(renamed[1].label == "Door");
        CHECK(renamed[2].label == "Door");
        CHECK(renamed[1].entity != renamed[2].entity);
    }
}

TEST_CASE("A long chain of parents is listed without recursion", "[editor][hierarchy]")
{
    Scene scene = emptyScene();
    constexpr std::size_t kDepth = 3000;
    std::vector<EntityId> chain;
    for (std::size_t i = 0; i < kDepth; ++i) {
        chain.push_back(entity(scene, "Link " + std::to_string(i)));
        if (i > 0) {
            link(scene, chain[i], chain[i - 1]);
        }
    }
    const std::vector<HierarchyRow> rows = hierarchyRows(scene);
    REQUIRE(rows.size() == kDepth);
    for (std::size_t i = 0; i < kDepth; ++i) {
        CHECK(rows[i].entity == chain[i]);
        CHECK(rows[i].depth == i);
    }
}

TEST_CASE("Two lists of rows have the same tree when only their labels differ",
          "[editor][hierarchy]")
{
    Scene scene = emptyScene();
    const EntityId a = entity(scene, "A");
    const EntityId b = entity(scene, "B");
    const EntityId c = entity(scene, "C");
    const std::vector<HierarchyRow> before = hierarchyRows(scene);
    CHECK(sameTree(before, before));

    REQUIRE_FALSE(scene.replaceComponent(b, NameComponent{"Renamed"}));
    const std::vector<HierarchyRow> renamed = hierarchyRows(scene);
    CHECK(renamed != before);
    CHECK(sameTree(before, renamed));

    link(scene, c, a);
    const std::vector<HierarchyRow> reparented = hierarchyRows(scene);
    CHECK_FALSE(sameTree(renamed, reparented));

    // The same entities in another order.
    REQUIRE_FALSE(scene.destroyEntity(b));
    const EntityId again = entity(scene, "B");
    CHECK(again != b);
    CHECK_FALSE(sameTree(reparented, hierarchyRows(scene)));
    CHECK_FALSE(sameTree(before, {}));
    CHECK(sameTree({}, {}));
}
