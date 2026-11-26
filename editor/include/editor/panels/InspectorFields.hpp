#pragma once

// What the inspector edits (M5.8), UI-free: the groups of values its fields
// show, read from a component and written back into one, the assets a field
// offers, and the components it adds. The Raven panel
// (editor/src/panels/InspectorPanel) turns these into widgets; tests use them
// without a window.
//
// An edit replaces only its group (one value, or a vector's three) in the
// component as stored: every other value keeps its exact stored bits, so
// editing a position never re-rounds the stored rotation.

#include "editor/document/ComponentValue.hpp"

#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "engine/core/Result.hpp"
#include "engine/scene/EntityId.hpp"
#include "engine/scene/Scene.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace editor {

// Values edited together: a vector's three fields or a single field.
enum class FieldGroup : std::uint8_t {
    Position,    // transform, x y z
    Rotation,    // transform, Euler angles in degrees (EulerAngles.hpp)
    Scale,       // transform, x y z
    VerticalFov, // camera, in degrees
    NearPlane,   // camera
    FarPlane,    // camera
    Direction,   // directional light, x y z
    Color,       // directional light, r g b
    Intensity,   // directional light
};

inline constexpr std::array<FieldGroup, 9> kFieldGroups{
    FieldGroup::Position,    FieldGroup::Rotation,  FieldGroup::Scale,
    FieldGroup::VerticalFov, FieldGroup::NearPlane, FieldGroup::FarPlane,
    FieldGroup::Direction,   FieldGroup::Color,     FieldGroup::Intensity,
};

using FieldValues = std::array<double, 3>;

// The component type a group belongs to.
[[nodiscard]] ComponentType componentTypeOf(FieldGroup group) noexcept;
// 3 for a vector, 1 for a single value (the first of FieldValues).
[[nodiscard]] std::size_t fieldCount(FieldGroup group) noexcept;

// The values a group's fields show, read from a component of its type (zeros
// for another type).
[[nodiscard]] FieldValues fieldValues(const ComponentValue& component, FieldGroup group);
// The component with the group replaced by `values` (the first fieldCount of
// them), the rest exactly as it was. A component of another type is returned
// unchanged.
[[nodiscard]] ComponentValue withFieldValues(ComponentValue component, FieldGroup group,
                                             const FieldValues& values);

// An asset a field offers: its ID and its path.
struct AssetChoice {
    engine::assets::AssetId id;
    std::string label;

    friend bool operator==(const AssetChoice&, const AssetChoice&) = default;
};
// The registry's assets of a type, in path order.
[[nodiscard]] std::vector<AssetChoice> assetChoices(const engine::assets::AssetRegistry& registry,
                                                    engine::assets::AssetType type);

// The components the inspector adds and removes, in the order it shows them.
// Parent links and prefab instances are shown, not edited: the hierarchy
// sets parents (M5.7), and prefabs are M6's.
inline constexpr std::array<ComponentType, 6> kEditableComponents{
    ComponentType::Name,       ComponentType::Transform,
    ComponentType::Camera,     ComponentType::DirectionalLight,
    ComponentType::StaticMesh, ComponentType::Script,
};

// The editable components the entity does not have, in kEditableComponents'
// order; none for an entity the scene does not contain.
[[nodiscard]] std::vector<ComponentType> addableComponents(const engine::scene::Scene& scene,
                                                           engine::scene::EntityId entity);

// The name a name component starts with.
inline constexpr std::string_view kNewComponentName = "Entity";

// The value a component is added with: the defaults; a name kNewComponentName;
// a static mesh the first mesh asset (in path order) with the default
// material; a script the first script asset, enabled. Fails for a static
// mesh or a script when the project has no asset of that type, and for a
// type that is not editable.
[[nodiscard]] engine::core::Result<ComponentValue>
newComponent(ComponentType type, const engine::assets::AssetRegistry& registry);

} // namespace editor
