#include "engine/scene/PrefabDocument.hpp"

#include "../core/DocumentMigration.hpp"
#include "../core/JsonSupport.hpp"
#include "ComponentJson.hpp"

#include "engine/core/FileSystem.hpp"
#include "engine/scene/Scene.hpp"

#include <array>
#include <format>
#include <map>
#include <span>
#include <system_error>
#include <utility>

namespace engine::scene {

namespace {

using core::json::describe;
using core::json::Json;
using core::json::OrderedJson;
using core::json::shortened;
using json::ComponentReader;
using json::join;

constexpr std::string_view kParentKey = "parent";

constexpr std::array<std::string_view, 7> kComponentKeys{json::kComponentKeys[0],
                                                         json::kComponentKeys[1],
                                                         json::kComponentKeys[2],
                                                         json::kComponentKeys[3],
                                                         json::kComponentKeys[4],
                                                         json::kComponentKeys[5],
                                                         kParentKey};
constexpr std::array<std::string_view, 5> kEnvelopeFields{"format", "formatVersion", "id", "name",
                                                          "entities"};
constexpr std::array<std::string_view, 2> kEntityFields{"localId", "components"};

// Version N to N + 1 is kMigrations[N - 1]; version 1 is the first.
constexpr std::array<core::DocumentMigration<Json>, 0> kMigrations{};

// A required local ID: an integer of at least 1.
std::optional<core::Error> readLocalId(const Json& object, std::string_view path,
                                       std::string_view key, PrefabLocalId& out)
{
    const auto found = object.find(key);
    const std::string at = join(path, key);
    if (found == object.end()) {
        return core::Error{at + ": missing"};
    }
    if (auto error = core::json::readInteger(*found, at, out)) {
        return error;
    }
    if (out < 1) {
        return core::Error{std::format("{}: {}, must be at least 1", at, out)};
    }
    return std::nullopt;
}

std::optional<core::Error> readComponents(ComponentReader& reader, const Json& components,
                                          std::string_view path, PrefabEntity& entity)
{
    if (auto error = ComponentReader::expectObject(components, path)) {
        return error;
    }
    for (const auto& item : components.items()) {
        const std::string& key = item.key();
        const std::string at = join(path, shortened(key));
        const Json& value = item.value();
        auto shared = reader.readComponent(key, value, at, entity.components);
        if (!shared) {
            return shared.error();
        }
        if (shared.value()) {
            continue;
        }
        if (key != kParentKey) {
            return core::Error{std::format("{}: unknown component type; known: {}", at,
                                           json::listOf(kComponentKeys))};
        }
        if (auto error = ComponentReader::expectObject(value, at)) {
            return error;
        }
        static constexpr std::array<std::string_view, 1> kFields{"entity"};
        reader.warnUnknown(value, at, kFields);
        PrefabLocalId parent = 0;
        if (auto error = readLocalId(value, at, "entity", parent)) {
            return error;
        }
        entity.parent = parent;
    }
    return std::nullopt;
}

} // namespace

std::string prefabToJson(const Prefab& prefab)
{
    OrderedJson root = OrderedJson::object();
    root["format"] = std::string(kPrefabFormat);
    root["formatVersion"] = kPrefabFormatVersion;
    root["id"] = prefab.id.toString();
    root["name"] = prefab.name;
    OrderedJson entities = OrderedJson::array();
    for (const PrefabEntity& entity : prefab.entities) {
        OrderedJson item = OrderedJson::object();
        item["localId"] = entity.localId;
        OrderedJson components = OrderedJson::object();
        json::writeComponents(components, entity.components);
        if (entity.parent) {
            OrderedJson parent = OrderedJson::object();
            parent["entity"] = *entity.parent;
            components[std::string(kParentKey)] = std::move(parent);
        }
        item["components"] = std::move(components);
        entities.push_back(std::move(item));
    }
    root["entities"] = std::move(entities);
    return root.dump(2, ' ', false, OrderedJson::error_handler_t::replace) + "\n";
}

core::Result<LoadedPrefab> prefabFromJson(std::string_view text,
                                          const assets::AssetRegistry* registry)
{
    // Stage 1: size and syntax.
    if (text.size() > kMaxPrefabDocumentBytes) {
        return core::Error{std::format("document: {} bytes, at most {} allowed", text.size(),
                                       kMaxPrefabDocumentBytes)};
    }
    if (auto syntax = core::json::syntaxError(text)) {
        return core::Error{"malformed JSON: " + *syntax};
    }
    Json root = Json::parse(text, nullptr, false);
    if (root.is_discarded()) {
        return core::Error{"malformed JSON"};
    }
    if (!root.is_object()) {
        return core::Error{std::format("expected a JSON object, found {}", describe(root))};
    }

    // Stage 2: envelope and migrations.
    const auto version = core::json::readEnvelope(root, kPrefabFormat, kPrefabFormatVersion);
    if (!version) {
        return version.error();
    }
    if (auto error =
            core::migrateDocument(root, version.value(), kPrefabFormatVersion,
                                  std::span<const core::DocumentMigration<Json>>(kMigrations))) {
        return *error;
    }

    // Stage 3: every field, then the prefab as a whole.
    ComponentReader reader(registry);
    reader.warnUnknown(root, "", kEnvelopeFields);
    LoadedPrefab loaded;
    auto id = ComponentReader::requiredId<assets::AssetId>(root, "", "id");
    if (!id) {
        return id.error();
    }
    loaded.prefab.id = id.value();
    if (auto error = reader.checkAsset(loaded.prefab.id, assets::AssetType::Prefab, "id")) {
        return *error;
    }
    auto name = ComponentReader::requiredString(root, "", "name");
    if (!name) {
        return name.error();
    }
    if (auto error = validateName(name.value(), "name")) {
        return *error;
    }
    loaded.prefab.name = std::move(name).value();

    if (const auto list = root.find("entities"); list != root.end()) {
        if (!list->is_array()) {
            return core::Error{
                std::format("entities: expected an array, found {}", describe(*list))};
        }
        if (list->size() > kMaxPrefabEntities) {
            return core::Error{std::format("entities: {} entries, at most {} allowed", list->size(),
                                           kMaxPrefabEntities)};
        }
        loaded.prefab.entities.reserve(list->size());
        std::map<PrefabLocalId, std::size_t> seen;
        std::size_t index = 0;
        for (const Json& item : *list) {
            const std::string at = std::format("entities[{}]", index);
            if (auto error = ComponentReader::expectObject(item, at)) {
                return *error;
            }
            reader.warnUnknown(item, at, kEntityFields);
            PrefabEntity entity;
            if (auto error = readLocalId(item, at, "localId", entity.localId)) {
                return *error;
            }
            if (const auto [earlier, added] = seen.emplace(entity.localId, index); !added) {
                return core::Error{std::format("{}.localId: {} is also entities[{}]", at,
                                               entity.localId, earlier->second)};
            }
            if (const auto components = item.find("components"); components != item.end()) {
                if (auto error = readComponents(reader, *components, at + ".components", entity)) {
                    return *error;
                }
            }
            loaded.prefab.entities.push_back(std::move(entity));
            ++index;
        }
    }
    // Parent links, cycles and the entity count.
    if (auto error = validate(loaded.prefab)) {
        return *error;
    }
    loaded.warnings = std::move(reader.warnings());
    return loaded;
}

std::optional<core::Error> savePrefab(const std::filesystem::path& file, const Prefab& prefab)
{
    if (auto error = validate(prefab)) {
        return core::Error{std::format("'{}' not written: {}", core::toUtf8(file), error->message)};
    }
    const std::string text = prefabToJson(prefab);
    if (auto error = core::writeFileAtomically(
            file, std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(text.data()),
                                                text.size()))) {
        return core::Error{std::format("prefab '{}' not saved: {}", prefab.name, error->message)};
    }
    return std::nullopt;
}

core::Result<LoadedPrefab> loadPrefab(const std::filesystem::path& file,
                                      const assets::AssetRegistry* registry)
{
    std::error_code error;
    const auto size = std::filesystem::file_size(file, error);
    if (!error && size > kMaxPrefabDocumentBytes) {
        return core::Error{std::format("'{}': {} bytes, at most {} allowed", core::toUtf8(file),
                                       size, kMaxPrefabDocumentBytes)};
    }
    auto text = core::readTextFile(file);
    if (!text) {
        return text.error();
    }
    auto loaded = prefabFromJson(text.value(), registry);
    if (!loaded) {
        return core::Error{std::format("'{}': {}", core::toUtf8(file), loaded.error().message)};
    }
    return loaded;
}

core::Result<LoadedPrefab> loadPrefabAsset(const assets::AssetRegistry& registry,
                                           assets::AssetId id)
{
    auto file = registry.resolve(id, assets::AssetType::Prefab);
    if (!file) {
        return file.error();
    }
    auto loaded = loadPrefab(file.value(), &registry);
    if (!loaded) {
        return loaded.error();
    }
    if (loaded.value().prefab.id != id) {
        return core::Error{std::format("'{}': the document's ID {} is not the asset ID {} the "
                                       "registry lists for the file",
                                       core::toUtf8(file.value()),
                                       loaded.value().prefab.id.toString(), id.toString())};
    }
    return loaded;
}

} // namespace engine::scene
