#include "engine/scene/EntityComponents.hpp"

#include "engine/scene/Scene.hpp"

namespace engine::scene {

namespace {

template <typename T>
std::optional<core::Error> checkOne(const std::optional<T>& component)
{
    return component ? validate(*component) : std::nullopt;
}

template <typename T>
void copyOne(const Scene& scene, EntityId entity, std::optional<T>& out)
{
    if (auto found = scene.component<T>(entity)) {
        out = std::move(found).value();
    }
}

template <typename T>
std::optional<core::Error> addOne(Scene& scene, EntityId entity, const std::optional<T>& component)
{
    return component ? scene.addComponent(entity, *component) : std::nullopt;
}

} // namespace

std::optional<core::Error> validate(const EntityComponents& components)
{
    if (auto error = checkOne(components.name)) {
        return error;
    }
    if (auto error = checkOne(components.transform)) {
        return error;
    }
    if (auto error = checkOne(components.staticMesh)) {
        return error;
    }
    if (auto error = checkOne(components.camera)) {
        return error;
    }
    if (auto error = checkOne(components.directionalLight)) {
        return error;
    }
    return checkOne(components.script);
}

EntityComponents componentsOf(const Scene& scene, EntityId entity)
{
    EntityComponents components;
    if (!scene.contains(entity)) {
        return components;
    }
    copyOne(scene, entity, components.name);
    copyOne(scene, entity, components.transform);
    copyOne(scene, entity, components.staticMesh);
    copyOne(scene, entity, components.camera);
    copyOne(scene, entity, components.directionalLight);
    copyOne(scene, entity, components.script);
    return components;
}

std::optional<core::Error> addComponents(Scene& scene, EntityId entity,
                                         const EntityComponents& components)
{
    if (auto error = addOne(scene, entity, components.name)) {
        return error;
    }
    if (auto error = addOne(scene, entity, components.transform)) {
        return error;
    }
    if (auto error = addOne(scene, entity, components.staticMesh)) {
        return error;
    }
    if (auto error = addOne(scene, entity, components.camera)) {
        return error;
    }
    if (auto error = addOne(scene, entity, components.directionalLight)) {
        return error;
    }
    return addOne(scene, entity, components.script);
}

} // namespace engine::scene
