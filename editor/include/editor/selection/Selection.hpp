#pragma once

// The editor's selection (M5.5): entity IDs, in the order they were selected;
// the last one is the primary selection (the inspector's entity, M5.8). The
// document keeps it valid: an entity that leaves the scene leaves the
// selection, and one that an undo or redo brings back is selected again if it
// was selected when it left (ADR 0029). Selecting is not a document change
// and not an undo step.

#include "engine/scene/EntityId.hpp"
#include "engine/scene/Scene.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace editor {

class Selection {
public:
    [[nodiscard]] bool empty() const noexcept { return ids_.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return ids_.size(); }
    [[nodiscard]] bool contains(engine::scene::EntityId entity) const noexcept;
    // In the order they were selected.
    [[nodiscard]] const std::vector<engine::scene::EntityId>& ids() const noexcept { return ids_; }
    // The entity selected last, if any.
    [[nodiscard]] std::optional<engine::scene::EntityId> primary() const noexcept;

    // Only `entity` (null clears).
    void select(engine::scene::EntityId entity);
    // Adds `entity` as the primary selection (moved to the end if selected).
    // The null ID is ignored.
    void add(engine::scene::EntityId entity);
    void remove(engine::scene::EntityId entity);
    void clear() noexcept { ids_.clear(); }

    // Removes the entities the scene does not contain; returns them in
    // selection order.
    std::vector<engine::scene::EntityId> prune(const engine::scene::Scene& scene);
    // Adds those of `entities` the scene contains, in their order.
    void restore(std::span<const engine::scene::EntityId> entities,
                 const engine::scene::Scene& scene);

private:
    std::vector<engine::scene::EntityId> ids_;
};

} // namespace editor
