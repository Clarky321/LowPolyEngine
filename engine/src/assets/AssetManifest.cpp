#include "engine/assets/AssetManifest.hpp"

#include "../core/JsonSupport.hpp"

#include "engine/core/FileSystem.hpp"

#include <array>
#include <format>
#include <span>
#include <system_error>
#include <utility>

namespace engine::assets {

namespace {

using core::json::describe;
using core::json::Json;
using core::json::OrderedJson;
using core::json::shortened;

constexpr std::array<std::string_view, 3> kEnvelopeFields{"format", "formatVersion", "assets"};
constexpr std::array<std::string_view, 3> kAssetFields{"id", "type", "path"};

bool isOneOf(std::string_view key, std::span<const std::string_view> known)
{
    for (const std::string_view candidate : known) {
        if (candidate == key) {
            return true;
        }
    }
    return false;
}

// A required string field of an object.
core::Result<std::string> readString(const Json& object, std::string_view key, std::string_view at)
{
    const auto found = object.find(key);
    if (found == object.end()) {
        return core::Error{std::format("{}.{}: missing", at, key)};
    }
    if (!found->is_string()) {
        return core::Error{
            std::format("{}.{}: expected a string, found {}", at, key, describe(*found))};
    }
    return found->get<std::string>();
}

std::string typeList()
{
    std::string list;
    for (const AssetType type : kAssetTypes) {
        list += list.empty() ? std::string(assetTypeName(type))
                             : ", " + std::string(assetTypeName(type));
    }
    return list;
}

} // namespace

std::string manifestToJson(const AssetRegistry& registry)
{
    OrderedJson root = OrderedJson::object();
    root["format"] = std::string(kManifestFormat);
    root["formatVersion"] = kManifestFormatVersion;
    OrderedJson list = OrderedJson::array();
    for (const AssetMetadata& asset : registry.assets()) {
        OrderedJson entry = OrderedJson::object();
        entry["id"] = asset.id.toString();
        entry["type"] = std::string(assetTypeName(asset.type));
        entry["path"] = asset.path.text();
        list.push_back(std::move(entry));
    }
    root["assets"] = std::move(list);
    return root.dump(2, ' ', false, OrderedJson::error_handler_t::replace) + "\n";
}

core::Result<LoadedManifest> manifestFromJson(std::string_view text,
                                              const std::filesystem::path& projectRoot)
{
    if (auto syntax = core::json::syntaxError(text)) {
        return core::Error{"malformed JSON: " + *syntax};
    }
    const Json root = Json::parse(text, nullptr, false);
    if (root.is_discarded()) {
        return core::Error{"malformed JSON"};
    }
    if (!root.is_object()) {
        return core::Error{std::format("expected a JSON object, found {}", describe(root))};
    }
    if (auto version = core::json::readEnvelope(root, kManifestFormat, kManifestFormatVersion);
        !version) {
        return version.error();
    }

    LoadedManifest loaded{AssetRegistry(projectRoot), {}};
    for (const auto& item : root.items()) {
        if (!isOneOf(item.key(), kEnvelopeFields)) {
            loaded.warnings.push_back(
                std::format("unknown field '{}' ignored", shortened(item.key())));
        }
    }

    const auto list = root.find("assets");
    if (list == root.end()) {
        // No list: a project without assets.
        return loaded;
    }
    if (!list->is_array()) {
        return core::Error{std::format("assets: expected an array, found {}", describe(*list))};
    }
    if (list->size() > kMaxManifestAssets) {
        return core::Error{std::format("assets: {} entries, at most {} allowed", list->size(),
                                       kMaxManifestAssets)};
    }

    std::size_t index = 0;
    for (const Json& entry : *list) {
        const std::string at = std::format("assets[{}]", index++);
        if (!entry.is_object()) {
            return core::Error{
                std::format("{}: expected an object, found {}", at, describe(entry))};
        }
        auto idText = readString(entry, "id", at);
        if (!idText) {
            return idText.error();
        }
        auto id = AssetId::parse(idText.value());
        if (!id) {
            return core::Error{std::format("{}.id: {}", at, id.error().message)};
        }
        auto typeText = readString(entry, "type", at);
        if (!typeText) {
            return typeText.error();
        }
        const auto type = parseAssetType(typeText.value());
        if (!type) {
            return core::Error{std::format("{}.type: unknown asset type \"{}\"; known: {}", at,
                                           shortened(typeText.value()), typeList())};
        }
        auto pathText = readString(entry, "path", at);
        if (!pathText) {
            return pathText.error();
        }
        auto path = AssetPath::parse(pathText.value());
        if (!path) {
            return core::Error{std::format("{}.path: {}", at, path.error().message)};
        }
        for (const auto& item : entry.items()) {
            if (!isOneOf(item.key(), kAssetFields)) {
                loaded.warnings.push_back(
                    std::format("unknown field '{}.{}' ignored", at, shortened(item.key())));
            }
        }
        if (auto error = loaded.registry.registerAsset(id.value(), *type, path.value())) {
            return core::Error{std::format("{}: {}", at, error->message)};
        }
    }
    return loaded;
}

std::optional<core::Error> saveManifest(const std::filesystem::path& file,
                                        const AssetRegistry& registry)
{
    const std::string text = manifestToJson(registry);
    return core::writeBinaryFile(
        file, std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(text.data()),
                                            text.size()));
}

core::Result<LoadedManifest> loadManifest(const std::filesystem::path& file,
                                          const std::filesystem::path& projectRoot)
{
    std::error_code error;
    const auto size = std::filesystem::file_size(file, error);
    if (!error && size > kMaxManifestBytes) {
        return core::Error{std::format("'{}': {} bytes, at most {} allowed", core::toUtf8(file),
                                       size, kMaxManifestBytes)};
    }
    auto text = core::readTextFile(file);
    if (!text) {
        return text.error();
    }
    auto loaded = manifestFromJson(text.value(), projectRoot);
    if (!loaded) {
        return core::Error{std::format("'{}': {}", core::toUtf8(file), loaded.error().message)};
    }
    return loaded;
}

} // namespace engine::assets
