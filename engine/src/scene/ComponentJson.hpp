#pragma once

// Internal to engine/src/scene/: the JSON of the plain components (ADR 0023,
// ADR 0025), shared by scene documents and prefab documents. Each document
// kind adds its own links (parents, prefab instances) around it.

#include "../core/JsonSupport.hpp"

#include "engine/assets/AssetRegistry.hpp"
#include "engine/scene/EntityComponents.hpp"

#include <array>
#include <cstddef>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace engine::scene::json {

using core::json::Json;
using core::json::OrderedJson;

// Keys of the plain components, in the order they are written.
inline constexpr std::array<std::string_view, 6> kComponentKeys{
    "name", "transform", "staticMesh", "camera", "directionalLight", "script"};

// Adds the components present to `object`, in kComponentKeys order, every
// field written.
void writeComponents(OrderedJson& object, const EntityComponents& components);

// An asset ID as text, or null for the null ID.
[[nodiscard]] OrderedJson assetJson(assets::AssetId id);

[[nodiscard]] std::string join(std::string_view path, std::string_view key);
[[nodiscard]] std::string listOf(std::span<const std::string_view> names);
[[nodiscard]] bool isOneOf(std::string_view key, std::span<const std::string_view> known);

// Reads the components of one document: collects its warnings and checks
// asset references against a registry, if one is given.
class ComponentReader {
public:
    explicit ComponentReader(const assets::AssetRegistry* registry) noexcept : registry_(registry)
    {
    }

    [[nodiscard]] std::vector<std::string>& warnings() noexcept { return warnings_; }

    // Keys of an object the reader does not know, as warnings.
    void warnUnknown(const Json& object, std::string_view path,
                     std::span<const std::string_view> known);

    [[nodiscard]] static std::optional<core::Error> expectObject(const Json& value,
                                                                 std::string_view path);
    // A required string field.
    [[nodiscard]] static core::Result<std::string>
    requiredString(const Json& object, std::string_view path, std::string_view key);
    // A required ID field of the given typed ID (EntityId, AssetId, ...).
    template <typename Id>
    [[nodiscard]] static core::Result<Id> requiredId(const Json& object, std::string_view path,
                                                     std::string_view key)
    {
        auto text = requiredString(object, path, key);
        if (!text) {
            return text.error();
        }
        auto id = Id::parse(text.value());
        if (!id) {
            return core::Error{std::format("{}: {}", join(path, key), id.error().message)};
        }
        return id.value();
    }

    // A component value that fails its validate: the message without its
    // "<type> component: " prefix, after the component's path.
    template <typename T>
    [[nodiscard]] static std::optional<core::Error> checked(const T& component,
                                                            std::string_view path)
    {
        if (auto error = validate(component)) {
            std::string_view message = error->message;
            const std::string prefix = std::string(T::kTypeName) + " component: ";
            if (message.starts_with(prefix)) {
                message.remove_prefix(prefix.size());
            }
            return core::Error{std::format("{}: {}", path, message)};
        }
        return std::nullopt;
    }

    // An asset reference of the wrong type or unknown to the registry; no
    // check without a registry or for the null ID.
    [[nodiscard]] std::optional<core::Error> checkAsset(assets::AssetId id, assets::AssetType type,
                                                        std::string_view path) const;

    // Reads the plain component named `key` into `components`; false for a
    // key that names none of them.
    [[nodiscard]] core::Result<bool> readComponent(std::string_view key, const Json& value,
                                                   std::string_view path,
                                                   EntityComponents& components);

private:
    static std::optional<core::Error> floatField(const Json& object, std::string_view path,
                                                 std::string_view key, float& out);
    template <std::size_t N>
    std::optional<core::Error>
    floatsField(const Json& object, std::string_view path, std::string_view key,
                const std::array<std::string_view, N>& names, const std::array<float*, N>& out);

    core::Result<NameComponent> name(const Json& value, std::string_view path);
    core::Result<TransformComponent> transform(const Json& value, std::string_view path);
    core::Result<StaticMeshComponent> staticMesh(const Json& value, std::string_view path);
    core::Result<CameraComponent> camera(const Json& value, std::string_view path);
    core::Result<DirectionalLightComponent> light(const Json& value, std::string_view path);
    core::Result<ScriptComponent> script(const Json& value, std::string_view path);

    const assets::AssetRegistry* registry_;
    std::vector<std::string> warnings_;
};

} // namespace engine::scene::json
