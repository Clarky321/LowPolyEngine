#pragma once

// The persistent identity of a scene (ADR 0020), a different type from
// EntityId.

#include "engine/core/Uuid.hpp"

#include <string_view>

namespace engine::scene {

struct SceneIdTag {
    static constexpr std::string_view kName = "scene ID";
};

using SceneId = core::TypedUuid<SceneIdTag>;

} // namespace engine::scene
