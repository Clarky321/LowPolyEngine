#pragma once

// A link from an entity to its parent entity in the same scene (M4.6, ADR
// 0025): a relationship only. There is no transform inheritance in M4; every
// transform stays relative to the world.
//
// The scene enforces the rules that need the other entities: an entity is
// not its own parent, the parent exists in the same scene, and parent links
// never form a cycle. Destroying a parent detaches its children (their
// ParentComponent is removed); they are not destroyed.

#include "engine/core/Result.hpp"
#include "engine/scene/EntityId.hpp"

#include <optional>
#include <string_view>

namespace engine::scene {

struct ParentComponent {
    static constexpr std::string_view kTypeName = "parent";

    // The parent; never null.
    EntityId entity;

    friend bool operator==(const ParentComponent&, const ParentComponent&) = default;
};

// The parent must not be null; the scene checks the rest. GPU-free.
[[nodiscard]] std::optional<core::Error> validate(const ParentComponent& component);

} // namespace engine::scene
