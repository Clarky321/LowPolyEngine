#pragma once

// The persistent identity of an entity in a scene (ADR 0020). Not a pointer,
// not a GPU handle and not an ECS handle: it is saved in documents and comes
// back unchanged when they are loaded.

#include "engine/core/Uuid.hpp"

#include <string_view>

namespace engine::scene {

struct EntityIdTag {
    static constexpr std::string_view kName = "entity ID";
};

using EntityId = core::TypedUuid<EntityIdTag>;

} // namespace engine::scene
