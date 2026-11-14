#include "ComponentJson.hpp"

#include <utility>

namespace engine::scene::json {

namespace {

using core::json::describe;
using core::json::floatNumber;
using core::json::readBool;
using core::json::readFloat;
using core::json::readString;
using core::json::shortened;

constexpr std::array<std::string_view, 3> kXyz{"x", "y", "z"};
constexpr std::array<std::string_view, 3> kRgb{"r", "g", "b"};
constexpr std::array<std::string_view, 4> kWxyz{"w", "x", "y", "z"};

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

// Takes a read component, or returns its error.
template <typename T>
core::Result<bool> take(core::Result<T> read, std::optional<T>& out)
{
    if (!read) {
        return read.error();
    }
    out = std::move(read).value();
    return true;
}

} // namespace

OrderedJson assetJson(assets::AssetId id)
{
    return id.isNull() ? OrderedJson(nullptr) : OrderedJson(id.toString());
}

void writeComponents(OrderedJson& object, const EntityComponents& components)
{
    if (const auto& name = components.name) {
        OrderedJson value = OrderedJson::object();
        value["value"] = name->value;
        object[std::string(kComponentKeys[0])] = std::move(value);
    }
    if (const auto& transform = components.transform) {
        OrderedJson value = OrderedJson::object();
        value["position"] = vectorJson(transform->position, kXyz);
        value["rotation"] = rotationJson(transform->rotation);
        value["scale"] = vectorJson(transform->scale, kXyz);
        object[std::string(kComponentKeys[1])] = std::move(value);
    }
    if (const auto& mesh = components.staticMesh) {
        OrderedJson value = OrderedJson::object();
        value["mesh"] = assetJson(mesh->mesh);
        value["material"] = assetJson(mesh->material);
        object[std::string(kComponentKeys[2])] = std::move(value);
    }
    if (const auto& camera = components.camera) {
        OrderedJson value = OrderedJson::object();
        value["verticalFovRadians"] = floatNumber(camera->verticalFovRadians);
        value["nearPlane"] = floatNumber(camera->nearPlane);
        value["farPlane"] = floatNumber(camera->farPlane);
        object[std::string(kComponentKeys[3])] = std::move(value);
    }
    if (const auto& light = components.directionalLight) {
        OrderedJson value = OrderedJson::object();
        value["direction"] = vectorJson(light->direction, kXyz);
        value["color"] = vectorJson(light->color, kRgb);
        value["intensity"] = floatNumber(light->intensity);
        object[std::string(kComponentKeys[4])] = std::move(value);
    }
    if (const auto& script = components.script) {
        OrderedJson value = OrderedJson::object();
        value["asset"] = assetJson(script->asset);
        value["enabled"] = script->enabled;
        object[std::string(kComponentKeys[5])] = std::move(value);
    }
}

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

void ComponentReader::warnUnknown(const Json& object, std::string_view path,
                                  std::span<const std::string_view> known)
{
    for (const auto& item : object.items()) {
        if (!isOneOf(item.key(), known)) {
            warnings_.push_back(
                std::format("unknown field '{}' ignored", join(path, shortened(item.key()))));
        }
    }
}

std::optional<core::Error> ComponentReader::expectObject(const Json& value, std::string_view path)
{
    if (!value.is_object()) {
        return core::Error{std::format("{}: expected an object, found {}", path, describe(value))};
    }
    return std::nullopt;
}

core::Result<std::string> ComponentReader::requiredString(const Json& object, std::string_view path,
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

std::optional<core::Error> ComponentReader::checkAsset(assets::AssetId id, assets::AssetType type,
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

std::optional<core::Error> ComponentReader::floatField(const Json& object, std::string_view path,
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
ComponentReader::floatsField(const Json& object, std::string_view path, std::string_view key,
                             const std::array<std::string_view, N>& names,
                             const std::array<float*, N>& out)
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

core::Result<bool> ComponentReader::readComponent(std::string_view key, const Json& value,
                                                  std::string_view path,
                                                  EntityComponents& components)
{
    if (key == kComponentKeys[0]) {
        return take(name(value, path), components.name);
    }
    if (key == kComponentKeys[1]) {
        return take(transform(value, path), components.transform);
    }
    if (key == kComponentKeys[2]) {
        return take(staticMesh(value, path), components.staticMesh);
    }
    if (key == kComponentKeys[3]) {
        return take(camera(value, path), components.camera);
    }
    if (key == kComponentKeys[4]) {
        return take(light(value, path), components.directionalLight);
    }
    if (key == kComponentKeys[5]) {
        return take(script(value, path), components.script);
    }
    return false;
}

core::Result<NameComponent> ComponentReader::name(const Json& value, std::string_view path)
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

core::Result<TransformComponent> ComponentReader::transform(const Json& value,
                                                            std::string_view path)
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
    if (auto error = floatsField<3>(value, path, "scale", kXyz, {&scale.x, &scale.y, &scale.z})) {
        return *error;
    }
    if (auto error = checked(component, path)) {
        return *error;
    }
    return component;
}

core::Result<StaticMeshComponent> ComponentReader::staticMesh(const Json& value,
                                                              std::string_view path)
{
    if (auto error = expectObject(value, path)) {
        return *error;
    }
    static constexpr std::array<std::string_view, 2> kFields{"mesh", "material"};
    warnUnknown(value, path, kFields);
    StaticMeshComponent component;
    const std::string meshPath = join(path, "mesh");
    auto mesh = requiredId<assets::AssetId>(value, path, "mesh");
    if (!mesh) {
        return mesh.error();
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
    if (auto error = checkAsset(component.material, assets::AssetType::Material, materialPath)) {
        return *error;
    }
    return component;
}

core::Result<CameraComponent> ComponentReader::camera(const Json& value, std::string_view path)
{
    if (auto error = expectObject(value, path)) {
        return *error;
    }
    static constexpr std::array<std::string_view, 3> kFields{"verticalFovRadians", "nearPlane",
                                                             "farPlane"};
    warnUnknown(value, path, kFields);
    CameraComponent component;
    if (auto error = floatField(value, path, "verticalFovRadians", component.verticalFovRadians)) {
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

core::Result<DirectionalLightComponent> ComponentReader::light(const Json& value,
                                                               std::string_view path)
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
    if (auto error = floatsField<3>(value, path, "color", kRgb, {&color.r, &color.g, &color.b})) {
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

core::Result<ScriptComponent> ComponentReader::script(const Json& value, std::string_view path)
{
    if (auto error = expectObject(value, path)) {
        return *error;
    }
    static constexpr std::array<std::string_view, 2> kFields{"asset", "enabled"};
    warnUnknown(value, path, kFields);
    ScriptComponent component;
    auto asset = requiredId<assets::AssetId>(value, path, "asset");
    if (!asset) {
        return asset.error();
    }
    component.asset = asset.value();
    if (const auto found = value.find("enabled"); found != value.end()) {
        if (auto error = readBool(*found, join(path, "enabled"), component.enabled)) {
            return *error;
        }
    }
    if (auto error = checked(component, path)) {
        return *error;
    }
    if (auto error = checkAsset(component.asset, assets::AssetType::Script, join(path, "asset"))) {
        return *error;
    }
    return component;
}

} // namespace engine::scene::json
