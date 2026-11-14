#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetType.hpp"
#include "engine/render/Renderer.hpp"
#include "engine/scene/EntityId.hpp"
#include "engine/scene/SceneId.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <type_traits>

using engine::assets::AssetId;
using engine::assets::AssetType;
using engine::core::Uuid;

// Asset IDs are persistent identities, renderer handles are runtime resources:
// no conversion in either direction, and none from other IDs, integers or
// pointers.
static_assert(!std::is_constructible_v<AssetId, engine::render::MeshHandle>);
static_assert(!std::is_constructible_v<AssetId, engine::render::TextureHandle>);
static_assert(!std::is_constructible_v<AssetId, engine::render::MaterialHandle>);
static_assert(!std::is_constructible_v<engine::render::MeshHandle, AssetId>);
static_assert(!std::is_constructible_v<engine::render::TextureHandle, AssetId>);
static_assert(!std::is_constructible_v<engine::render::MaterialHandle, AssetId>);
static_assert(!std::is_convertible_v<AssetId, engine::render::MeshHandle>);
static_assert(!std::is_convertible_v<engine::render::MeshHandle, AssetId>);
static_assert(!std::is_constructible_v<AssetId, engine::scene::EntityId>);
static_assert(!std::is_constructible_v<AssetId, engine::scene::SceneId>);
static_assert(!std::is_constructible_v<engine::scene::EntityId, AssetId>);
static_assert(!std::is_constructible_v<AssetId, std::uint64_t>);
static_assert(!std::is_constructible_v<AssetId, std::uint32_t>);
static_assert(!std::is_constructible_v<AssetId, const void*>);
static_assert(!std::is_convertible_v<AssetId, std::uint64_t>);
static_assert(!std::is_convertible_v<Uuid, AssetId>);
static_assert(std::is_constructible_v<AssetId, Uuid>);

TEST_CASE("Asset IDs read and write the canonical form", "[assets]")
{
    const AssetId id{Uuid{0x6F1C2B9E4D3A4F58ULL, 0x9A7E2C5D8B0E1F43ULL}};
    CHECK(id.toString() == "6f1c2b9e-4d3a-4f58-9a7e-2c5d8b0e1f43");
    const auto parsed = AssetId::parse("6F1C2B9E-4D3A-4F58-9A7E-2C5D8B0E1F43");
    REQUIRE(parsed);
    CHECK(parsed.value() == id);

    CHECK(AssetId{}.isNull());
    const auto null = AssetId::parse("00000000-0000-0000-0000-000000000000");
    REQUIRE_FALSE(null);
    CHECK(null.error().message ==
          "asset ID '00000000-0000-0000-0000-000000000000': the null ID is not an identity");
    const auto bad = AssetId::parse("lighthouse");
    REQUIRE_FALSE(bad);
    CHECK(bad.error().message.starts_with("asset ID 'lighthouse': expected 36 characters"));
}

TEST_CASE("Asset types have fixed names in documents", "[assets]")
{
    CHECK(engine::assets::assetTypeName(AssetType::Mesh) == "mesh");
    CHECK(engine::assets::assetTypeName(AssetType::Texture) == "texture");
    CHECK(engine::assets::assetTypeName(AssetType::Material) == "material");
    CHECK(engine::assets::assetTypeName(AssetType::Script) == "script");
    CHECK(engine::assets::assetTypeName(AssetType::Prefab) == "prefab");
    for (const AssetType type : engine::assets::kAssetTypes) {
        CHECK(engine::assets::parseAssetType(engine::assets::assetTypeName(type)) == type);
    }
    CHECK_FALSE(engine::assets::parseAssetType("Mesh").has_value());
    CHECK_FALSE(engine::assets::parseAssetType("model").has_value());
    CHECK_FALSE(engine::assets::parseAssetType("").has_value());
}

TEST_CASE("Asset types name the extensions their files have", "[assets]")
{
    const auto mesh = engine::assets::assetExtensions(AssetType::Mesh);
    REQUIRE(mesh.size() == 1);
    CHECK(mesh[0] == ".obj");
    const auto texture = engine::assets::assetExtensions(AssetType::Texture);
    REQUIRE(texture.size() == 1);
    CHECK(texture[0] == ".png");
    const auto script = engine::assets::assetExtensions(AssetType::Script);
    REQUIRE(script.size() == 1);
    CHECK(script[0] == ".lua");
    // No file format yet: any extension.
    CHECK(engine::assets::assetExtensions(AssetType::Material).empty());
    CHECK(engine::assets::assetExtensions(AssetType::Prefab).empty());
}
