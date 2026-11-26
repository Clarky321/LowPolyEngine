#include "editor/selection/Selection.hpp"

#include <algorithm>

namespace editor {

bool Selection::contains(engine::scene::EntityId entity) const noexcept
{
    return std::find(ids_.begin(), ids_.end(), entity) != ids_.end();
}

std::optional<engine::scene::EntityId> Selection::primary() const noexcept
{
    if (ids_.empty()) {
        return std::nullopt;
    }
    return ids_.back();
}

void Selection::select(engine::scene::EntityId entity)
{
    ids_.clear();
    add(entity);
}

void Selection::add(engine::scene::EntityId entity)
{
    if (entity.isNull()) {
        return;
    }
    remove(entity);
    ids_.push_back(entity);
}

void Selection::remove(engine::scene::EntityId entity)
{
    std::erase(ids_, entity);
}

std::vector<engine::scene::EntityId> Selection::prune(const engine::scene::Scene& scene)
{
    std::vector<engine::scene::EntityId> removed;
    std::erase_if(ids_, [&](engine::scene::EntityId entity) {
        if (scene.contains(entity)) {
            return false;
        }
        removed.push_back(entity);
        return true;
    });
    return removed;
}

void Selection::restore(std::span<const engine::scene::EntityId> entities,
                        const engine::scene::Scene& scene)
{
    for (const engine::scene::EntityId entity : entities) {
        if (scene.contains(entity)) {
            add(entity);
        }
    }
}

} // namespace editor
