#pragma once

// The display name of an entity (M4.2, ADR 0021). A name is not an identity:
// names may repeat within a scene and change at any time; the EntityId
// identifies the entity.

#include "engine/core/Result.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace engine::scene {

struct NameComponent {
    static constexpr std::string_view kTypeName = "name";

    // Not empty, at most kMaxNameBytes bytes of valid UTF-8, no control
    // characters: the rules of validateName (engine/scene/Scene.hpp).
    std::string value;

    friend bool operator==(const NameComponent&, const NameComponent&) = default;
};

// The error names the component and the field, e.g.
// "name component: value: must not be empty". GPU-free.
[[nodiscard]] std::optional<core::Error> validate(const NameComponent& component);

} // namespace engine::scene
