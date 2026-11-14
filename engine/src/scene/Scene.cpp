#include "engine/scene/Scene.hpp"

#include "engine/core/Utf8.hpp"

#include <format>
#include <utility>

namespace engine::scene {

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
      generator_(other.generator_), byId_(std::move(other.byId_)),
      bySequence_(std::move(other.bySequence_)),
      nextSequence_(std::exchange(other.nextSequence_, 0))
{
    other.byId_.clear();
    other.bySequence_.clear();
}

Scene& Scene::operator=(Scene&& other) noexcept
{
    if (this != &other) {
        id_ = std::exchange(other.id_, SceneId{});
        name_ = std::exchange(other.name_, std::string{});
        generator_ = other.generator_;
        byId_ = std::move(other.byId_);
        bySequence_ = std::move(other.bySequence_);
        nextSequence_ = std::exchange(other.nextSequence_, 0);
        other.byId_.clear();
        other.bySequence_.clear();
    }
    return *this;
}

std::string Scene::describe(EntityId entity) const
{
    return std::format("scene '{}': entity {}", name_, entity.toString());
}

void Scene::insert(EntityId entity)
{
    const std::uint64_t sequence = nextSequence_++;
    byId_.emplace(entity, sequence);
    bySequence_.emplace(sequence, entity);
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
    if (auto error = findEntity(entity)) {
        return error;
    }
    const auto found = byId_.find(entity);
    bySequence_.erase(found->second);
    byId_.erase(found);
    return std::nullopt;
}

bool Scene::contains(EntityId entity) const noexcept
{
    return byId_.find(entity) != byId_.end();
}

std::optional<core::Error> Scene::findEntity(EntityId entity) const
{
    if (entity.isNull()) {
        return core::Error{std::format("scene '{}': the null entity ID is not an identity", name_)};
    }
    if (!contains(entity)) {
        return core::Error{describe(entity) + ": does not exist"};
    }
    return std::nullopt;
}

std::vector<EntityId> Scene::entityIds() const
{
    std::vector<EntityId> ids;
    ids.reserve(bySequence_.size());
    for (const auto& entry : bySequence_) {
        ids.push_back(entry.second);
    }
    return ids;
}

void Scene::clear() noexcept
{
    byId_.clear();
    bySequence_.clear();
}

} // namespace engine::scene
