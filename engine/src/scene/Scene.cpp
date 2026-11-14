#include "engine/scene/Scene.hpp"

#include "engine/core/Utf8.hpp"

#include <entt/entity/registry.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <map>
#include <utility>

namespace engine::scene {

namespace {

// Every entity in the registry carries its persistent ID and its place in the
// creation order; queries sort by it, so results do not depend on the order
// of EnTT's pools.
struct EntityRecord {
    EntityId id;
    std::uint64_t sequence = 0;
};

// A unit length already: values this close to 1 are stored unchanged, so
// storing a stored value again (a saved scene loaded back) keeps every bit.
// Normalizing in double ends far closer to 1 than this.
constexpr double kUnitLengthEpsilon = 1.0e-6;

double lengthOf(const math::Quat& q) noexcept
{
    const auto w = static_cast<double>(q.w);
    const auto x = static_cast<double>(q.x);
    const auto y = static_cast<double>(q.y);
    const auto z = static_cast<double>(q.z);
    return std::sqrt((w * w) + (x * x) + (y * y) + (z * z));
}

double lengthOf(const math::Vec3& v) noexcept
{
    const auto x = static_cast<double>(v.x);
    const auto y = static_cast<double>(v.y);
    const auto z = static_cast<double>(v.z);
    return std::sqrt((x * x) + (y * y) + (z * z));
}

// The form a scene stores; only valid values reach these functions.
template <typename T>
T storedForm(const T& value)
{
    return value;
}

TransformComponent storedForm(const TransformComponent& value)
{
    TransformComponent stored = value;
    const double length = lengthOf(value.rotation);
    if (std::abs(length - 1.0) > kUnitLengthEpsilon) {
        stored.rotation =
            math::Quat{static_cast<float>(static_cast<double>(value.rotation.w) / length),
                       static_cast<float>(static_cast<double>(value.rotation.x) / length),
                       static_cast<float>(static_cast<double>(value.rotation.y) / length),
                       static_cast<float>(static_cast<double>(value.rotation.z) / length)};
    }
    return stored;
}

DirectionalLightComponent storedForm(const DirectionalLightComponent& value)
{
    DirectionalLightComponent stored = value;
    const double length = lengthOf(value.direction);
    if (std::abs(length - 1.0) > kUnitLengthEpsilon) {
        stored.direction =
            math::Vec3{static_cast<float>(static_cast<double>(value.direction.x) / length),
                       static_cast<float>(static_cast<double>(value.direction.y) / length),
                       static_cast<float>(static_cast<double>(value.direction.z) / length)};
    }
    return stored;
}

// The persistent IDs of the given EnTT entities, in creation order. The
// record pool is reached through a checked pointer (a const registry has no
// pool for a type it has never stored).
std::vector<EntityId> inCreationOrder(const entt::registry& registry,
                                      const entt::sparse_set& entities)
{
    const auto* const recordPool = registry.storage<EntityRecord>();
    if (recordPool == nullptr) {
        return {};
    }
    std::vector<EntityRecord> records;
    records.reserve(entities.size());
    for (const entt::entity handle : entities) {
        records.push_back(recordPool->get(handle));
    }
    std::ranges::sort(records, {}, &EntityRecord::sequence);
    std::vector<EntityId> ids;
    ids.reserve(records.size());
    for (const EntityRecord& record : records) {
        ids.push_back(record.id);
    }
    return ids;
}

} // namespace

struct Scene::Storage {
    entt::registry registry;
    // Persistent ID -> EnTT entity. The EnTT handle never leaves this file.
    std::map<EntityId, entt::entity> handles;
    std::uint64_t nextSequence = 0;

    [[nodiscard]] const entt::entity* find(EntityId entity) const noexcept
    {
        const auto found = handles.find(entity);
        return found == handles.end() ? nullptr : &found->second;
    }
};

std::optional<core::Error> validateName(std::string_view name, std::string_view what)
{
    if (name.empty()) {
        return core::Error{std::format("{}: must not be empty", what)};
    }
    if (name.size() > kMaxNameBytes) {
        return core::Error{
            std::format("{}: {} bytes, at most {} allowed", what, name.size(), kMaxNameBytes)};
    }
    std::size_t offset = 0;
    while (offset < name.size()) {
        const std::size_t start = offset;
        const auto codePoint = core::decodeUtf8At(name, offset);
        if (!codePoint) {
            return core::Error{std::format("{}: invalid UTF-8 at byte {}", what, start)};
        }
        const auto value = static_cast<std::uint32_t>(*codePoint);
        if (value < 0x20U || (value >= 0x7FU && value <= 0x9FU)) {
            return core::Error{
                std::format("{}: control character U+{:04X} at byte {}", what, value, start)};
        }
    }
    return std::nullopt;
}

core::Result<Scene> Scene::create(SceneId sceneId, std::string_view name,
                                  core::UuidGenerator generator)
{
    if (sceneId.isNull()) {
        return core::Error{"scene: the null scene ID is not an identity"};
    }
    if (auto error = validateName(name, "scene name")) {
        return *error;
    }
    return Scene{sceneId, std::string(name), generator};
}

core::Result<Scene> Scene::create(std::string_view name, core::UuidGenerator generator)
{
    const SceneId sceneId{generator.next()};
    return create(sceneId, name, generator);
}

Scene::Scene(SceneId sceneId, std::string name, core::UuidGenerator generator) noexcept
    : id_(sceneId), name_(std::move(name)), generator_(generator)
{
}

Scene::Scene(Scene&& other) noexcept
    : id_(std::exchange(other.id_, SceneId{})), name_(std::exchange(other.name_, std::string{})),
      generator_(other.generator_), storage_(std::move(other.storage_))
{
}

Scene& Scene::operator=(Scene&& other) noexcept
{
    if (this != &other) {
        id_ = std::exchange(other.id_, SceneId{});
        name_ = std::exchange(other.name_, std::string{});
        generator_ = other.generator_;
        storage_ = std::move(other.storage_);
    }
    return *this;
}

Scene::~Scene() = default;

std::string Scene::describe(EntityId entity) const
{
    return std::format("scene '{}': entity {}", name_, entity.toString());
}

Scene::Storage& Scene::storage()
{
    if (!storage_) {
        storage_ = std::make_unique<Storage>();
    }
    return *storage_;
}

void Scene::insert(EntityId entity)
{
    Storage& store = storage();
    const entt::entity handle = store.registry.create();
    store.registry.emplace<EntityRecord>(handle, entity, store.nextSequence++);
    store.handles.emplace(entity, handle);
}

core::Result<EntityId> Scene::createEntity()
{
    for (int attempt = 0; attempt < kMaxIdAttempts; ++attempt) {
        const EntityId candidate{generator_.next()};
        if (!candidate.isNull() && !contains(candidate)) {
            insert(candidate);
            return candidate;
        }
    }
    return core::Error{
        std::format("scene '{}': no unused entity ID after {} attempts", name_, kMaxIdAttempts)};
}

core::Result<EntityId> Scene::createEntity(EntityId entity)
{
    if (entity.isNull()) {
        return core::Error{std::format("scene '{}': the null entity ID is not an identity", name_)};
    }
    if (contains(entity)) {
        return core::Error{describe(entity) + ": already exists"};
    }
    insert(entity);
    return entity;
}

std::optional<core::Error> Scene::destroyEntity(EntityId entity)
{
    Storage* const store = storage_.get();
    if (store == nullptr) {
        return entityError(entity);
    }
    const auto found = store->handles.find(entity);
    if (found == store->handles.end()) {
        return entityError(entity);
    }
    // Destroying the EnTT entity removes all of its components.
    (void)store->registry.destroy(found->second);
    store->handles.erase(found);
    return std::nullopt;
}

bool Scene::contains(EntityId entity) const noexcept
{
    const Storage* const store = storage_.get();
    return store != nullptr && store->find(entity) != nullptr;
}

core::Error Scene::entityError(EntityId entity) const
{
    if (entity.isNull()) {
        return core::Error{std::format("scene '{}': the null entity ID is not an identity", name_)};
    }
    return core::Error{describe(entity) + ": does not exist"};
}

std::optional<core::Error> Scene::findEntity(EntityId entity) const
{
    if (contains(entity)) {
        return std::nullopt;
    }
    return entityError(entity);
}

std::vector<EntityId> Scene::entityIds() const
{
    const Storage* const store = storage_.get();
    if (store == nullptr) {
        return {};
    }
    const entt::sparse_set* const all = store->registry.storage<EntityRecord>();
    if (all == nullptr) {
        return {};
    }
    // Every entity has a record, so the record pool holds all of them.
    return inCreationOrder(store->registry, *all);
}

std::size_t Scene::entityCount() const noexcept
{
    const Storage* const store = storage_.get();
    return store != nullptr ? store->handles.size() : 0;
}

void Scene::clear() noexcept
{
    storage_.reset();
}

// Each component operation first resolves the entity's EnTT handle, checking
// every pointer on the way (GCC's -Wnull-dereference at -O3 cannot see that
// findEntity already did).

template <SceneComponent T>
std::optional<core::Error> Scene::addComponent(EntityId entity, const T& value)
{
    Storage* const store = storage_.get();
    const entt::entity* const handle = store != nullptr ? store->find(entity) : nullptr;
    if (store == nullptr || handle == nullptr) {
        return entityError(entity);
    }
    if (store->registry.all_of<T>(*handle)) {
        return core::Error{
            std::format("{}: already has a {} component", describe(entity), T::kTypeName)};
    }
    if (auto error = validate(value)) {
        return core::Error{std::format("{}: {}", describe(entity), error->message)};
    }
    store->registry.emplace<T>(*handle, storedForm(value));
    return std::nullopt;
}

template <SceneComponent T>
std::optional<core::Error> Scene::replaceComponent(EntityId entity, const T& value)
{
    Storage* const store = storage_.get();
    const entt::entity* const handle = store != nullptr ? store->find(entity) : nullptr;
    if (store == nullptr || handle == nullptr) {
        return entityError(entity);
    }
    T* const stored = store->registry.try_get<T>(*handle);
    if (stored == nullptr) {
        return core::Error{std::format("{}: has no {} component", describe(entity), T::kTypeName)};
    }
    if (auto error = validate(value)) {
        return core::Error{std::format("{}: {}", describe(entity), error->message)};
    }
    *stored = storedForm(value);
    return std::nullopt;
}

template <SceneComponent T>
std::optional<core::Error> Scene::removeComponent(EntityId entity)
{
    Storage* const store = storage_.get();
    const entt::entity* const handle = store != nullptr ? store->find(entity) : nullptr;
    if (store == nullptr || handle == nullptr) {
        return entityError(entity);
    }
    if (store->registry.remove<T>(*handle) == 0) {
        return core::Error{std::format("{}: has no {} component", describe(entity), T::kTypeName)};
    }
    return std::nullopt;
}

template <SceneComponent T>
bool Scene::hasComponent(EntityId entity) const noexcept
{
    const Storage* const store = storage_.get();
    const entt::entity* const handle = store != nullptr ? store->find(entity) : nullptr;
    return store != nullptr && handle != nullptr && store->registry.all_of<T>(*handle);
}

template <SceneComponent T>
core::Result<T> Scene::component(EntityId entity) const
{
    const Storage* const store = storage_.get();
    const entt::entity* const handle = store != nullptr ? store->find(entity) : nullptr;
    if (store == nullptr || handle == nullptr) {
        return entityError(entity);
    }
    const T* const found = store->registry.try_get<T>(*handle);
    if (found == nullptr) {
        return core::Error{std::format("{}: has no {} component", describe(entity), T::kTypeName)};
    }
    return *found;
}

template <SceneComponent T>
std::vector<EntityId> Scene::entitiesWithComponent() const
{
    const Storage* const store = storage_.get();
    if (store == nullptr) {
        return {};
    }
    const entt::sparse_set* const pool = store->registry.storage<T>();
    if (pool == nullptr) {
        return {};
    }
    return inCreationOrder(store->registry, *pool);
}

// The component operations exist for exactly the SceneComponent types.
#define LPE_INSTANTIATE_SCENE_COMPONENT(Type)                                                      \
    template std::optional<core::Error> Scene::addComponent<Type>(EntityId, const Type&);          \
    template std::optional<core::Error> Scene::replaceComponent<Type>(EntityId, const Type&);      \
    template std::optional<core::Error> Scene::removeComponent<Type>(EntityId);                    \
    template bool Scene::hasComponent<Type>(EntityId) const noexcept;                              \
    template core::Result<Type> Scene::component<Type>(EntityId) const;                            \
    template std::vector<EntityId> Scene::entitiesWithComponent<Type>() const;

LPE_INSTANTIATE_SCENE_COMPONENT(NameComponent)
LPE_INSTANTIATE_SCENE_COMPONENT(TransformComponent)
LPE_INSTANTIATE_SCENE_COMPONENT(StaticMeshComponent)
LPE_INSTANTIATE_SCENE_COMPONENT(CameraComponent)
LPE_INSTANTIATE_SCENE_COMPONENT(DirectionalLightComponent)

#undef LPE_INSTANTIATE_SCENE_COMPONENT

} // namespace engine::scene
