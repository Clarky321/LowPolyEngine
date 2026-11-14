#include "engine/scene/SceneDocument.hpp"

#include "../core/DocumentMigration.hpp"
#include "../core/JsonSupport.hpp"
#include "ComponentJson.hpp"

#include "engine/core/FileSystem.hpp"
#include "engine/scene/EntityComponents.hpp"

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

// The links of scene documents, written after the plain components.
constexpr std::string_view kParentKey = "parent";
constexpr std::string_view kPrefabInstanceKey = "prefabInstance";

constexpr std::array<std::string_view, 8> kComponentKeys{json::kComponentKeys[0],
                                                         json::kComponentKeys[1],
                                                         json::kComponentKeys[2],
                                                         json::kComponentKeys[3],
                                                         json::kComponentKeys[4],
                                                         json::kComponentKeys[5],
                                                         kParentKey,
                                                         kPrefabInstanceKey};
constexpr std::array<std::string_view, 5> kEnvelopeFields{"format", "formatVersion", "id", "name",
                                                          "entities"};
constexpr std::array<std::string_view, 2> kEntityFields{"id", "components"};

// Version N to N + 1 is kMigrations[N - 1]; version 1 is the first, so there
// is nothing to migrate yet.
constexpr std::array<core::DocumentMigration<Json>, 0> kMigrations{};

OrderedJson componentsJson(const Scene& scene, EntityId entity)
{
    OrderedJson components = OrderedJson::object();
    json::writeComponents(components, componentsOf(scene, entity));
    if (const auto parent = scene.component<ParentComponent>(entity)) {
        OrderedJson object = OrderedJson::object();
        object["entity"] = parent.value().entity.toString();
        components[std::string(kParentKey)] = std::move(object);
    }
    if (const auto instance = scene.component<PrefabInstanceComponent>(entity)) {
        OrderedJson object = OrderedJson::object();
        object["prefab"] = instance.value().prefab.toString();
        object["instance"] = instance.value().instance.toString();
        object["localId"] = instance.value().localId;
        components[std::string(kPrefabInstanceKey)] = std::move(object);
    }
    return components;
}

// One entity as read, before the scene is built.
struct EntityData {
    EntityId id;
    EntityComponents components;
    std::optional<ParentComponent> parent;
    std::optional<PrefabInstanceComponent> instance;
};

core::Result<ParentComponent> readParent(ComponentReader& reader, const Json& value,
                                         std::string_view path)
{
    if (auto error = ComponentReader::expectObject(value, path)) {
        return *error;
    }
    static constexpr std::array<std::string_view, 1> kFields{"entity"};
    reader.warnUnknown(value, path, kFields);
    auto entity = ComponentReader::requiredId<EntityId>(value, path, "entity");
    if (!entity) {
        return entity.error();
    }
    return ParentComponent{entity.value()};
}

core::Result<PrefabInstanceComponent> readInstance(ComponentReader& reader, const Json& value,
                                                   std::string_view path)
{
    if (auto error = ComponentReader::expectObject(value, path)) {
        return *error;
    }
    static constexpr std::array<std::string_view, 3> kFields{"prefab", "instance", "localId"};
    reader.warnUnknown(value, path, kFields);
    PrefabInstanceComponent component;
    auto prefab = ComponentReader::requiredId<assets::AssetId>(value, path, "prefab");
    if (!prefab) {
        return prefab.error();
    }
    component.prefab = prefab.value();
    auto instance = ComponentReader::requiredId<PrefabInstanceId>(value, path, "instance");
    if (!instance) {
        return instance.error();
    }
    component.instance = instance.value();
    const auto localId = value.find("localId");
    if (localId == value.end()) {
        return core::Error{std::format("{}: missing", join(path, "localId"))};
    }
    if (auto error = core::json::readInteger(*localId, join(path, "localId"), component.localId)) {
        return *error;
    }
    if (auto error = ComponentReader::checked(component, path)) {
        return *error;
    }
    if (auto error =
            reader.checkAsset(component.prefab, assets::AssetType::Prefab, join(path, "prefab"))) {
        return *error;
    }
    return component;
}

std::optional<core::Error> readComponents(ComponentReader& reader, const Json& components,
                                          std::string_view path, EntityData& data)
{
    if (auto error = ComponentReader::expectObject(components, path)) {
        return error;
    }
    for (const auto& item : components.items()) {
        const std::string& key = item.key();
        const std::string at = join(path, shortened(key));
        const Json& value = item.value();
        auto shared = reader.readComponent(key, value, at, data.components);
        if (!shared) {
            return shared.error();
        }
        if (shared.value()) {
            continue;
        }
        if (key == kParentKey) {
            auto parent = readParent(reader, value, at);
            if (!parent) {
                return parent.error();
            }
            data.parent = parent.value();
        } else if (key == kPrefabInstanceKey) {
            auto instance = readInstance(reader, value, at);
            if (!instance) {
                return instance.error();
            }
            data.instance = instance.value();
        } else {
            // Not dropped with a warning: saving the scene again would lose it.
            return core::Error{std::format("{}: unknown component type; known: {}", at,
                                           json::listOf(kComponentKeys))};
        }
    }
    return std::nullopt;
}

// Parent links inside the document: no self-parent, a parent in the
// document, no cycle. The path names the child's link.
std::optional<core::Error> checkParents(const std::vector<EntityData>& entities,
                                        const std::map<EntityId, std::size_t>& indices)
{
    for (std::size_t i = 0; i < entities.size(); ++i) {
        if (!entities[i].parent) {
            continue;
        }
        const std::string path = std::format("entities[{}].components.parent.entity", i);
        const EntityId parent = entities[i].parent->entity;
        if (parent == entities[i].id) {
            return core::Error{path + ": an entity cannot be its own parent"};
        }
        const auto found = indices.find(parent);
        if (found == indices.end()) {
            return core::Error{
                std::format("{}: entity {} is not in the document", path, parent.toString())};
        }
        // Walk up; reaching this entity again closes a cycle.
        std::size_t current = found->second;
        for (std::size_t step = 0; step <= entities.size(); ++step) {
            if (current == i) {
                return core::Error{std::format("{}: entity {} is a descendant of this entity, "
                                               "which makes a cycle",
                                               path, parent.toString())};
            }
            if (!entities[current].parent) {
                break;
            }
            const auto next = indices.find(entities[current].parent->entity);
            if (next == indices.end()) {
                break; // Reported when that entity's own link is checked.
            }
            current = next->second;
        }
    }
    return std::nullopt;
}

} // namespace

std::string sceneToJson(const Scene& scene)
{
    OrderedJson root = OrderedJson::object();
    root["format"] = std::string(kSceneFormat);
    root["formatVersion"] = kSceneFormatVersion;
    root["id"] = scene.id().toString();
    root["name"] = scene.name();
    OrderedJson entities = OrderedJson::array();
    for (const EntityId entity : scene.entityIds()) {
        OrderedJson item = OrderedJson::object();
        item["id"] = entity.toString();
        item["components"] = componentsJson(scene, entity);
        entities.push_back(std::move(item));
    }
    root["entities"] = std::move(entities);
    return root.dump(2, ' ', false, OrderedJson::error_handler_t::replace) + "\n";
}

core::Result<LoadedScene> sceneFromJson(std::string_view text, core::UuidGenerator generator,
                                        const assets::AssetRegistry* registry)
{
    // Stage 1: size and syntax.
    if (text.size() > kMaxSceneDocumentBytes) {
        return core::Error{std::format("document: {} bytes, at most {} allowed", text.size(),
                                       kMaxSceneDocumentBytes)};
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

    // Stage 2: envelope and migrations to the current version.
    const auto version = core::json::readEnvelope(root, kSceneFormat, kSceneFormatVersion);
    if (!version) {
        return version.error();
    }
    if (auto error =
            core::migrateDocument(root, version.value(), kSceneFormatVersion,
                                  std::span<const core::DocumentMigration<Json>>(kMigrations))) {
        return *error;
    }

    // Stage 3: the scene's fields and every entity, checked before anything
    // is built.
    ComponentReader reader(registry);
    reader.warnUnknown(root, "", kEnvelopeFields);

    auto idText = ComponentReader::requiredString(root, "", "id");
    if (!idText) {
        return idText.error();
    }
    const auto sceneId = SceneId::parse(idText.value());
    if (!sceneId) {
        return core::Error{"id: " + sceneId.error().message};
    }
    auto nameText = ComponentReader::requiredString(root, "", "name");
    if (!nameText) {
        return nameText.error();
    }
    if (auto error = validateName(nameText.value(), "name")) {
        return *error;
    }

    std::vector<EntityData> entities;
    std::map<EntityId, std::size_t> seen;
    if (const auto list = root.find("entities"); list != root.end()) {
        if (!list->is_array()) {
            return core::Error{
                std::format("entities: expected an array, found {}", describe(*list))};
        }
        if (list->size() > kMaxSceneEntities) {
            return core::Error{std::format("entities: {} entries, at most {} allowed", list->size(),
                                           kMaxSceneEntities)};
        }
        entities.reserve(list->size());
        std::size_t index = 0;
        for (const Json& item : *list) {
            const std::string at = std::format("entities[{}]", index);
            if (auto error = ComponentReader::expectObject(item, at)) {
                return *error;
            }
            reader.warnUnknown(item, at, kEntityFields);
            auto entityText = ComponentReader::requiredString(item, at, "id");
            if (!entityText) {
                return entityText.error();
            }
            const auto entity = EntityId::parse(entityText.value());
            if (!entity) {
                return core::Error{std::format("{}.id: {}", at, entity.error().message)};
            }
            if (const auto [earlier, added] = seen.emplace(entity.value(), index); !added) {
                return core::Error{std::format("{}.id: entity {} is also entities[{}]", at,
                                               entity.value().toString(), earlier->second)};
            }
            EntityData data;
            data.id = entity.value();
            if (const auto components = item.find("components"); components != item.end()) {
                if (auto error = readComponents(reader, *components, at + ".components", data)) {
                    return *error;
                }
            }
            entities.push_back(std::move(data));
            ++index;
        }
    }

    if (auto error = checkParents(entities, seen)) {
        return *error;
    }

    // Stage 4: the new scene: every entity first, so that parent links find
    // their parents. Everything was checked above, so the scene accepts it;
    // an error here would be a bug, still reported, not thrown.
    auto created = Scene::create(sceneId.value(), nameText.value(), generator);
    if (!created) {
        return created.error();
    }
    Scene scene = std::move(created).value();
    for (std::size_t i = 0; i < entities.size(); ++i) {
        if (auto made = scene.createEntity(entities[i].id); !made) {
            return core::Error{std::format("entities[{}]: {}", i, made.error().message)};
        }
    }
    for (std::size_t i = 0; i < entities.size(); ++i) {
        const EntityData& data = entities[i];
        std::optional<core::Error> error = addComponents(scene, data.id, data.components);
        if (!error && data.parent) {
            error = scene.addComponent(data.id, *data.parent);
        }
        if (!error && data.instance) {
            error = scene.addComponent(data.id, *data.instance);
        }
        if (error) {
            return core::Error{std::format("entities[{}]: {}", i, error->message)};
        }
    }
    return LoadedScene{std::move(scene), std::move(reader.warnings())};
}

std::optional<core::Error> saveScene(const std::filesystem::path& file, const Scene& scene)
{
    if (scene.id().isNull()) {
        return core::Error{std::format("scene not saved to '{}': it has no ID (a moved-from scene)",
                                       core::toUtf8(file))};
    }
    const std::string text = sceneToJson(scene);
    if (auto error = core::writeFileAtomically(
            file, std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(text.data()),
                                                text.size()))) {
        return core::Error{std::format("scene '{}' not saved: {}", scene.name(), error->message)};
    }
    return std::nullopt;
}

core::Result<LoadedScene> loadScene(const std::filesystem::path& file,
                                    core::UuidGenerator generator,
                                    const assets::AssetRegistry* registry)
{
    std::error_code error;
    const auto size = std::filesystem::file_size(file, error);
    if (!error && size > kMaxSceneDocumentBytes) {
        return core::Error{std::format("'{}': {} bytes, at most {} allowed", core::toUtf8(file),
                                       size, kMaxSceneDocumentBytes)};
    }
    auto text = core::readTextFile(file);
    if (!text) {
        return text.error();
    }
    auto loaded = sceneFromJson(text.value(), generator, registry);
    if (!loaded) {
        return core::Error{std::format("'{}': {}", core::toUtf8(file), loaded.error().message)};
    }
    return loaded;
}

} // namespace engine::scene
