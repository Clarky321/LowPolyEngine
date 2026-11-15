#pragma once

// The link from an entity of a prefab instance to its prefab (M4.6, ADR
// 0025): which prefab, which instance and which entity of the prefab. An
// instance stores its full component values in the scene; there are no
// overrides and no live updates in M4, but a later milestone can find and
// update instances through this link.

#include "engine/assets/AssetId.hpp"
#include "engine/core/Result.hpp"
#include "engine/core/Uuid.hpp"

#include <cstdint>
#include <optional>
#include <string_view>

namespace engine::scene {

struct PrefabInstanceIdTag {
    static constexpr std::string_view kName = "prefab instance ID";
};

// Identifies one instance of a prefab within a scene; all entities of the
// instance carry the same one.
using PrefabInstanceId = core::TypedUuid<PrefabInstanceIdTag>;

// Identifies an entity within one prefab document: 1 or more, unique there.
using PrefabLocalId = std::uint32_t;

struct PrefabInstanceComponent {
    static constexpr std::string_view kTypeName = "prefab instance";

    // The prefab asset; never null.
    assets::AssetId prefab;
    // The instance; never null.
    PrefabInstanceId instance;
    // The entity's local ID in the prefab; at least 1.
    PrefabLocalId localId = 0;

    friend bool operator==(const PrefabInstanceComponent&,
                           const PrefabInstanceComponent&) = default;
};

// No null ID and a local ID of at least 1; the error names the field.
// GPU-free.
[[nodiscard]] std::optional<core::Error> validate(const PrefabInstanceComponent& component);

} // namespace engine::scene
