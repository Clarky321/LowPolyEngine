#include "engine/scene/SceneDocument.hpp"

#include "../core/DocumentMigration.hpp"
#include "../core/JsonSupport.hpp"

#include "engine/core/FileSystem.hpp"

#include <array>
#include <format>
#include <map>
#include <span>
#include <system_error>
#include <utility>

namespace engine::scene {

namespace {

using core::json::describe;
using core::json::floatNumber;
using core::json::Json;
using core::json::OrderedJson;
using core::json::readFloat;
using core::json::readString;
using core::json::shortened;

// Component keys in documents, in the order they are written.
constexpr std::string_view kNameKey = "name";
constexpr std::string_view kTransformKey = "transform";
constexpr std::string_view kStaticMeshKey = "staticMesh";
constexpr std::string_view kCameraKey = "camera";
constexpr std::string_view kLightKey = "directionalLight";

constexpr std::array<std::string_view, 5> kComponentKeys{kNameKey, kTransformKey, kStaticMeshKey,
                                                         kCameraKey, kLightKey};
constexpr std::array<std::string_view, 5> kEnvelopeFields{"format", "formatVersion", "id", "name",
                                                          "entities"};
constexpr std::array<std::string_view, 2> kEntityFields{"id", "components"};
constexpr std::array<std::string_view, 3> kXyz{"x", "y", "z"};
constexpr std::array<std::string_view, 3> kRgb{"r", "g", "b"};
constexpr std::array<std::string_view, 4> kWxyz{"w", "x", "y", "z"};

// Version N to N + 1 is kMigrations[N - 1]; version 1 is the first, so there
// is nothing to migrate yet.
constexpr std::array<core::DocumentMigration<Json>, 0> kMigrations{};

// ---- Writing -------------------------------------------------------------

OrderedJson vectorJson(const math::Vec3& value, const std::array<std::string_view, 3>& names)
{
    OrderedJson object = OrderedJson::object();
    object[std::string(names[0])] = floatNumber(value.x);
    object[std::string(names[1])] = floatNumber(value.y);
    object[std::string(names[2])] = floatNumber(value.z);
    return object;
}

OrderedJson rotationJson(const math::Quat& value)
{
    OrderedJson object = OrderedJson::object();
    object["w"] = floatNumber(value.w);
    object["x"] = floatNumber(value.x);
    object["y"] = floatNumber(value.y);
    object["z"] = floatNumber(value.z);
    return object;
}

OrderedJson assetJson(assets::AssetId id)
{
    return id.isNull() ? OrderedJson(nullptr) : OrderedJson(id.toString());
}

OrderedJson componentsJson(const Scene& scene, EntityId entity)
{
    OrderedJson components = OrderedJson::object();
    if (scene.hasComponent<NameComponent>(entity)) {
        const NameComponent name = scene.component<NameComponent>(entity).value();
        OrderedJson object = OrderedJson::object();
        object["value"] = name.value;
        components[std::string(kNameKey)] = std::move(object);
    }
    if (scene.hasComponent<TransformComponent>(entity)) {
        const TransformComponent transform = scene.component<TransformComponent>(entity).value();
        OrderedJson object = OrderedJson::object();
        object["position"] = vectorJson(transform.position, kXyz);
        object["rotation"] = rotationJson(transform.rotation);
        object["scale"] = vectorJson(transform.scale, kXyz);
        components[std::string(kTransformKey)] = std::move(object);
    }
    if (scene.hasComponent<StaticMeshComponent>(entity)) {
        const StaticMeshComponent mesh = scene.component<StaticMeshComponent>(entity).value();
        OrderedJson object = OrderedJson::object();
        object["mesh"] = assetJson(mesh.mesh);
        object["material"] = assetJson(mesh.material);
        components[std::string(kStaticMeshKey)] = std::move(object);
    }
    if (scene.hasComponent<CameraComponent>(entity)) {
        const CameraComponent camera = scene.component<CameraComponent>(entity).value();
        OrderedJson object = OrderedJson::object();
        object["verticalFovRadians"] = floatNumber(camera.verticalFovRadians);
        object["nearPlane"] = floatNumber(camera.nearPlane);
        object["farPlane"] = floatNumber(camera.farPlane);
        components[std::string(kCameraKey)] = std::move(object);
    }
    if (scene.hasComponent<DirectionalLightComponent>(entity)) {
        const DirectionalLightComponent light =
            scene.component<DirectionalLightComponent>(entity).value();
        OrderedJson object = OrderedJson::object();
        object["direction"] = vectorJson(light.direction, kXyz);
        object["color"] = vectorJson(light.color, kRgb);
        object["intensity"] = floatNumber(light.intensity);
        components[std::string(kLightKey)] = std::move(object);
    }
    return components;
}

// ---- Reading -------------------------------------------------------------

bool isOneOf(std::string_view key, std::span<const std::string_view> known)
{
    for (const std::string_view candidate : known) {
        if (candidate == key) {
            return true;
        }
    }
    return false;
}

std::string join(std::string_view path, std::string_view key)
{
    return path.empty() ? std::string(key) : std::string(path) + "." + std::string(key);
}

std::string listOf(std::span<const std::string_view> names)
{
    std::string list;
    for (const std::string_view name : names) {
        list += list.empty() ? std::string(name) : ", " + std::string(name);
    }
    return list;
}

// Reads one document: the warnings it collects on the way.
class Reader {
public:
    explicit Reader(const assets::AssetRegistry* registry) noexcept : registry_(registry) {}

    [[nodiscard]] std::vector<std::string>& warnings() noexcept { return warnings_; }

    // Keys of an object the reader does not know, as warnings.
    void warnUnknown(const Json& object, std::string_view path,
                     std::span<const std::string_view> known)
    {
        for (const auto& item : object.items()) {
            if (!isOneOf(item.key(), known)) {
                warnings_.push_back(
                    std::format("unknown field '{}' ignored", join(path, shortened(item.key()))));
            }
        }
    }

    static std::optional<core::Error> expectObject(const Json& value, std::string_view path)
    {
        if (!value.is_object()) {
            return core::Error{
                std::format("{}: expected an object, found {}", path, describe(value))};
        }
        return std::nullopt;
    }

    // An optional float field; a missing one keeps its default.
    static std::optional<core::Error> floatField(const Json& object, std::string_view path,
                                                 std::string_view key, float& out)
    {
        const auto found = object.find(key);
        if (found == object.end()) {
            return std::nullopt;
        }
        return readFloat(*found, join(path, key), out);
    }

    // An optional object of named floats, e.g. {"x": 1, "y": 2, "z": 3}; missing
    // members keep their defaults.
    template <std::size_t N>
    std::optional<core::Error>
    floatsField(const Json& object, std::string_view path, std::string_view key,
                const std::array<std::string_view, N>& names, const std::array<float*, N>& out)
    {
        const auto found = object.find(key);
        if (found == object.end()) {
            return std::nullopt;
        }
        const std::string at = join(path, key);
        if (auto error = expectObject(*found, at)) {
            return error;
        }
        warnUnknown(*found, at, names);
        for (std::size_t i = 0; i < N; ++i) {
            if (auto error = floatField(*found, at, names[i], *out[i])) {
                return error;
            }
        }
        return std::nullopt;
    }

    // A required ID text.
    static core::Result<std::string> requiredString(const Json& object, std::string_view path,
                                                    std::string_view key)
    {
        const auto found = object.find(key);
        if (found == object.end()) {
            return core::Error{std::format("{}: missing", join(path, key))};
        }
        std::string text;
        if (auto error = readString(*found, join(path, key), text)) {
            return *error;
        }
        return text;
    }

    // A component value that fails its validate: the message without its
    // "<type> component: " prefix, after the component's path.
    template <typename T>
    static std::optional<core::Error> checked(const T& component, std::string_view path)
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

    std::optional<core::Error> checkAsset(assets::AssetId id, assets::AssetType type,
                                          std::string_view path) const
    {
        if (registry_ == nullptr || id.isNull()) {
            return std::nullopt;
        }
        if (auto error = registry_->checkType(id, type)) {
            return core::Error{std::format("{}: {}", path, error->message)};
        }
        return std::nullopt;
    }

    core::Result<NameComponent> name(const Json& value, std::string_view path)
    {
        if (auto error = expectObject(value, path)) {
            return *error;
        }
        static constexpr std::array<std::string_view, 1> kFields{"value"};
        warnUnknown(value, path, kFields);
        auto text = requiredString(value, path, "value");
        if (!text) {
            return text.error();
        }
        NameComponent component{std::move(text).value()};
        if (auto error = checked(component, path)) {
            return *error;
        }
        return component;
    }

    core::Result<TransformComponent> transform(const Json& value, std::string_view path)
    {
        if (auto error = expectObject(value, path)) {
            return *error;
        }
        static constexpr std::array<std::string_view, 3> kFields{"position", "rotation", "scale"};
        warnUnknown(value, path, kFields);
        TransformComponent component;
        math::Vec3& position = component.position;
        math::Quat& rotation = component.rotation;
        math::Vec3& scale = component.scale;
        if (auto error = floatsField<3>(value, path, "position", kXyz,
                                        {&position.x, &position.y, &position.z})) {
            return *error;
        }
        if (auto error = floatsField<4>(value, path, "rotation", kWxyz,
                                        {&rotation.w, &rotation.x, &rotation.y, &rotation.z})) {
            return *error;
        }
        if (auto error =
                floatsField<3>(value, path, "scale", kXyz, {&scale.x, &scale.y, &scale.z})) {
            return *error;
        }
        if (auto error = checked(component, path)) {
            return *error;
        }
        return component;
    }

    core::Result<StaticMeshComponent> staticMesh(const Json& value, std::string_view path)
    {
        if (auto error = expectObject(value, path)) {
            return *error;
        }
        static constexpr std::array<std::string_view, 2> kFields{"mesh", "material"};
        warnUnknown(value, path, kFields);
        StaticMeshComponent component;

        const std::string meshPath = join(path, "mesh");
        auto meshText = requiredString(value, path, "mesh");
        if (!meshText) {
            return meshText.error();
        }
        auto mesh = assets::AssetId::parse(meshText.value());
        if (!mesh) {
            return core::Error{std::format("{}: {}", meshPath, mesh.error().message)};
        }
        component.mesh = mesh.value();

        // Missing or null: the default material.
        const std::string materialPath = join(path, "material");
        if (const auto found = value.find("material"); found != value.end() && !found->is_null()) {
            std::string text;
            if (auto error = readString(*found, materialPath, text)) {
                return *error;
            }
            auto material = assets::AssetId::parse(text);
            if (!material) {
                return core::Error{std::format("{}: {}", materialPath, material.error().message)};
            }
            component.material = material.value();
        }
        if (auto error = checked(component, path)) {
            return *error;
        }
        if (auto error = checkAsset(component.mesh, assets::AssetType::Mesh, meshPath)) {
            return *error;
        }
        if (auto error =
                checkAsset(component.material, assets::AssetType::Material, materialPath)) {
            return *error;
        }
        return component;
    }

    core::Result<CameraComponent> camera(const Json& value, std::string_view path)
    {
        if (auto error = expectObject(value, path)) {
            return *error;
        }
        static constexpr std::array<std::string_view, 3> kFields{"verticalFovRadians", "nearPlane",
                                                                 "farPlane"};
        warnUnknown(value, path, kFields);
        CameraComponent component;
        if (auto error =
                floatField(value, path, "verticalFovRadians", component.verticalFovRadians)) {
            return *error;
        }
        if (auto error = floatField(value, path, "nearPlane", component.nearPlane)) {
            return *error;
        }
        if (auto error = floatField(value, path, "farPlane", component.farPlane)) {
            return *error;
        }
        if (auto error = checked(component, path)) {
            return *error;
        }
        return component;
    }

    core::Result<DirectionalLightComponent> light(const Json& value, std::string_view path)
    {
        if (auto error = expectObject(value, path)) {
            return *error;
        }
        static constexpr std::array<std::string_view, 3> kFields{"direction", "color", "intensity"};
        warnUnknown(value, path, kFields);
        DirectionalLightComponent component;
        math::Vec3& direction = component.direction;
        math::Vec3& color = component.color;
        if (auto error = floatsField<3>(value, path, "direction", kXyz,
                                        {&direction.x, &direction.y, &direction.z})) {
            return *error;
        }
        if (auto error =
                floatsField<3>(value, path, "color", kRgb, {&color.r, &color.g, &color.b})) {
            return *error;
        }
        if (auto error = floatField(value, path, "intensity", component.intensity)) {
            return *error;
        }
        if (auto error = checked(component, path)) {
            return *error;
        }
        return component;
    }

private:
    const assets::AssetRegistry* registry_;
    std::vector<std::string> warnings_;
};

// One entity as read, before the scene is built. Plain values with flags
// rather than std::optional (GCC -Wmaybe-uninitialized at -O3).
struct EntityData {
    EntityId id;
    bool hasName = false;
    NameComponent name;
    bool hasTransform = false;
    TransformComponent transform;
    bool hasStaticMesh = false;
    StaticMeshComponent staticMesh;
    bool hasCamera = false;
    CameraComponent camera;
    bool hasLight = false;
    DirectionalLightComponent light;
};

// Takes a component into the entity's data, or returns its error.
template <typename T>
std::optional<core::Error> take(core::Result<T> read, bool& has, T& out)
{
    if (!read) {
        return read.error();
    }
    out = std::move(read).value();
    has = true;
    return std::nullopt;
}

std::optional<core::Error> readComponents(Reader& reader, const Json& components,
                                          std::string_view path, EntityData& data)
{
    if (auto error = Reader::expectObject(components, path)) {
        return error;
    }
    for (const auto& item : components.items()) {
        const std::string& key = item.key();
        const std::string at = join(path, shortened(key));
        const Json& value = item.value();
        std::optional<core::Error> error;
        if (key == kNameKey) {
            error = take(reader.name(value, at), data.hasName, data.name);
        } else if (key == kTransformKey) {
            error = take(reader.transform(value, at), data.hasTransform, data.transform);
        } else if (key == kStaticMeshKey) {
            error = take(reader.staticMesh(value, at), data.hasStaticMesh, data.staticMesh);
        } else if (key == kCameraKey) {
            error = take(reader.camera(value, at), data.hasCamera, data.camera);
        } else if (key == kLightKey) {
            error = take(reader.light(value, at), data.hasLight, data.light);
        } else {
            // Not dropped with a warning: saving the scene again would lose it.
            error = core::Error{
                std::format("{}: unknown component type; known: {}", at, listOf(kComponentKeys))};
        }
        if (error) {
            return error;
        }
    }
    return std::nullopt;
}

template <typename T>
std::optional<core::Error> addIf(Scene& scene, EntityId entity, bool has, const T& component)
{
    return has ? scene.addComponent(entity, component) : std::nullopt;
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
    Reader reader(registry);
    reader.warnUnknown(root, "", kEnvelopeFields);

    auto idText = Reader::requiredString(root, "", "id");
    if (!idText) {
        return idText.error();
    }
    const auto sceneId = SceneId::parse(idText.value());
    if (!sceneId) {
        return core::Error{"id: " + sceneId.error().message};
    }
    auto nameText = Reader::requiredString(root, "", "name");
    if (!nameText) {
        return nameText.error();
    }
    if (auto error = validateName(nameText.value(), "name")) {
        return *error;
    }

    std::vector<EntityData> entities;
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
        std::map<EntityId, std::size_t> seen;
        std::size_t index = 0;
        for (const Json& item : *list) {
            const std::string at = std::format("entities[{}]", index);
            if (auto error = Reader::expectObject(item, at)) {
                return *error;
            }
            reader.warnUnknown(item, at, kEntityFields);
            auto entityText = Reader::requiredString(item, at, "id");
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

    // Stage 4: the new scene. Everything was checked above, so the scene
    // accepts it; an error here would be a bug, still reported, not thrown.
    auto created = Scene::create(sceneId.value(), nameText.value(), generator);
    if (!created) {
        return created.error();
    }
    Scene scene = std::move(created).value();
    for (std::size_t i = 0; i < entities.size(); ++i) {
        const EntityData& data = entities[i];
        std::optional<core::Error> error;
        if (auto made = scene.createEntity(data.id); !made) {
            error = made.error();
        }
        if (!error) {
            error = addIf(scene, data.id, data.hasName, data.name);
        }
        if (!error) {
            error = addIf(scene, data.id, data.hasTransform, data.transform);
        }
        if (!error) {
            error = addIf(scene, data.id, data.hasStaticMesh, data.staticMesh);
        }
        if (!error) {
            error = addIf(scene, data.id, data.hasCamera, data.camera);
        }
        if (!error) {
            error = addIf(scene, data.id, data.hasLight, data.light);
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
