#pragma once

// The hierarchy's rows (M5.7): the scene's entities as the hierarchy panel
// shows them, a tree of parent links (ParentComponent, M4.6) read from the
// scene. UI-free: the panel turns the rows into Raven's tree items and tests
// compare the tree with them.

#include "engine/scene/EntityId.hpp"
#include "engine/scene/Scene.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace editor {

struct HierarchyRow {
    engine::scene::EntityId entity;
    // The parent; null for a top-level entity.
    engine::scene::EntityId parent;
    // 0 for a top-level entity, its parent's depth + 1 otherwise.
    std::size_t depth = 0;
    std::string label;

    friend bool operator==(const HierarchyRow&, const HierarchyRow&) = default;
};

// The text an entity is shown with: its name, or its ID (canonical text) when
// it has none.
[[nodiscard]] std::string entityLabel(const engine::scene::Scene& scene,
                                      engine::scene::EntityId entity);

// Every entity of the scene once, depth first: the top-level entities in
// creation order, each followed by its children in creation order, and each
// child by its own. The scene never holds a cycle or a parent it does not
// contain (M4.6); an entity that could not be reached from the top level all
// the same is listed at the top level, so no entity is left out.
[[nodiscard]] std::vector<HierarchyRow> hierarchyRows(const engine::scene::Scene& scene);

// Whether two lists of rows describe the same tree (the same entities, parents
// and order), so that they can differ in their labels only.
[[nodiscard]] bool sameTree(std::span<const HierarchyRow> a,
                            std::span<const HierarchyRow> b) noexcept;

} // namespace editor
