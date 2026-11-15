#pragma once

// A Lua script attached to an entity (M4.5, ADR 0024): the asset ID of a
// script and whether it runs. The scripting runtime (engine/scripting/)
// loads and calls it; whether an instance has failed is runtime state there,
// keyed by the entity's ID, and is never saved.

#include "engine/assets/AssetId.hpp"
#include "engine/core/Result.hpp"

#include <optional>
#include <string_view>

namespace engine::scene {

struct ScriptComponent {
    static constexpr std::string_view kTypeName = "script";

    // A script asset; never null.
    assets::AssetId asset;
    // A disabled script is not loaded or called.
    bool enabled = true;

    friend bool operator==(const ScriptComponent&, const ScriptComponent&) = default;
};

// The asset must not be null; the error names the field. Whether it is a
// registered script asset is the registry's question. GPU-free.
[[nodiscard]] std::optional<core::Error> validate(const ScriptComponent& component);

} // namespace engine::scene
