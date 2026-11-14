#include "engine/assets/AssetType.hpp"

namespace engine::assets {

namespace {

constexpr std::array<std::string_view, 1> kMeshExtensions{".obj"};
constexpr std::array<std::string_view, 1> kTextureExtensions{".png"};
constexpr std::array<std::string_view, 1> kScriptExtensions{".lua"};

} // namespace

std::string_view assetTypeName(AssetType type) noexcept
{
    switch (type) {
    case AssetType::Mesh:
        return "mesh";
    case AssetType::Texture:
        return "texture";
    case AssetType::Material:
        return "material";
    case AssetType::Script:
        return "script";
    case AssetType::Prefab:
        return "prefab";
    }
    return "unknown";
}

std::optional<AssetType> parseAssetType(std::string_view name) noexcept
{
    for (const AssetType type : kAssetTypes) {
        if (assetTypeName(type) == name) {
            return type;
        }
    }
    return std::nullopt;
}

std::span<const std::string_view> assetExtensions(AssetType type) noexcept
{
    switch (type) {
    case AssetType::Mesh:
        return kMeshExtensions;
    case AssetType::Texture:
        return kTextureExtensions;
    case AssetType::Script:
        return kScriptExtensions;
    case AssetType::Material:
    case AssetType::Prefab:
        break;
    }
    return {};
}

} // namespace engine::assets
