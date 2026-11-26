// The editor's selection (M5.5, ADR 0029): order, the primary entity, and
// keeping only entities the scene contains.

#include "editor/selection/Selection.hpp"

#include "engine/scene/Scene.hpp"

#include <catch2/catch_test_macros.hpp>

#include <utility>
#include <vector>

using editor::Selection;
using engine::scene::EntityId;

namespace {

engine::scene::Scene sceneWith(std::vector<EntityId>& entities, int count)
{
    auto scene = engine::scene::Scene::create("Main", engine::core::UuidGenerator{3});
    REQUIRE(scene.hasValue());
    for (int i = 0; i < count; ++i) {
        auto entity = scene.value().createEntity();
        REQUIRE(entity.hasValue());
        entities.push_back(entity.value());
    }
    return std::move(scene).value();
}

} // namespace

TEST_CASE("A selection keeps its order and its primary entity", "[editor][selection]")
{
    std::vector<EntityId> e;
    const engine::scene::Scene scene = sceneWith(e, 3);
    Selection selection;
    CHECK(selection.empty());
    CHECK_FALSE(selection.primary().has_value());

    selection.select(e[0]);
    selection.add(e[1]);
    selection.add(e[2]);
    CHECK(selection.ids() == std::vector<EntityId>{e[0], e[1], e[2]});
    CHECK(selection.primary() == e[2]);
    // Added again: it becomes the primary one, once.
    selection.add(e[0]);
    CHECK(selection.ids() == std::vector<EntityId>{e[1], e[2], e[0]});
    selection.remove(e[2]);
    CHECK(selection.ids() == std::vector<EntityId>{e[1], e[0]});
    selection.add(EntityId{});
    CHECK(selection.size() == 2U);
    selection.select(e[2]);
    CHECK(selection.ids() == std::vector<EntityId>{e[2]});
    selection.select(EntityId{});
    CHECK(selection.empty());
    selection.add(e[1]);
    selection.clear();
    CHECK(selection.empty());
    CHECK(scene.entityCount() == 3U);
}

TEST_CASE("A selection keeps only entities the scene contains", "[editor][selection]")
{
    std::vector<EntityId> e;
    engine::scene::Scene scene = sceneWith(e, 4);
    Selection selection;
    for (const EntityId entity : e) {
        selection.add(entity);
    }
    REQUIRE_FALSE(scene.destroyEntity(e[1]).has_value());
    REQUIRE_FALSE(scene.destroyEntity(e[3]).has_value());
    CHECK(selection.prune(scene) == std::vector<EntityId>{e[1], e[3]});
    CHECK(selection.ids() == std::vector<EntityId>{e[0], e[2]});
    CHECK(selection.prune(scene).empty());

    // Restoring adds only entities the scene contains again.
    REQUIRE(scene.createEntity(e[3]));
    const std::vector<EntityId> removed{e[1], e[3]};
    selection.restore(removed, scene);
    CHECK(selection.ids() == std::vector<EntityId>{e[0], e[2], e[3]});
}
