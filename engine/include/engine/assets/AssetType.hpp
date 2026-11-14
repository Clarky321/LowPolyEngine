#pragma once

// The kinds of asset a project has (M4.3, ADR 0022), each with a fixed name in
// documents and the file extensions it may have.

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace engine::assets {

enum class AssetType : std::uint8_t {
    Mesh,     // a mesh file the engine reads: the OBJ subset of ADR 0012
    Texture,  // an image file the engine reads: PNG (ADR 0008)
    Material, // a material file; its format comes with M6 (ADR 0010)
    Script,   // a Lua script (M4.5)
    Prefab,   // a prefab document (M4.6)
};

inline constexpr std::array<AssetType, 5> kAssetTypes{
    AssetType::Mesh, AssetType::Texture, AssetType::Material, AssetType::Script, AssetType::Prefab,
};

// The name in documents and messages: "mesh", "texture", "material", "script",
// "prefab".
[[nodiscard]] std::string_view assetTypeName(AssetType type) noexcept;

// The type with that exact name, or nothing.
[[nodiscard]] std::optional<AssetType> parseAssetType(std::string_view name) noexcept;

// The file extensions (lower case, with the dot) an asset of the type must
// have, compared without regard to letter case: ".obj" for meshes, ".png" for
// textures, ".lua" for scripts. Empty for materials and prefabs, whose file
// formats do not exist yet; the step that adds a format adds its extension.
[[nodiscard]] std::span<const std::string_view> assetExtensions(AssetType type) noexcept;

} // namespace engine::assets
