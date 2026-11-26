#include "editor/commands/EntityCommands.hpp"

#include <format>
#include <utility>

namespace editor {

namespace {

using engine::core::Error;
using engine::scene::EntityId;
using engine::scene::NameComponent;
using engine::scene::ParentComponent;
using engine::scene::Scene;

// Takes back the parent links given to `children` so far, after a failure.
void unlink(Scene& scene, const std::vector<EntityId>& children, std::size_t count)
{
    for (std::size_t i = 0; i < count; ++i) {
        (void)scene.removeComponent<ParentComponent>(children[i]);
    }
}

} // namespace

CreateEntity::CreateEntity(std::vector<ComponentValue> components)
    : components_(std::move(components))
{
}

std::optional<Error> CreateEntity::apply(DocumentContent& content)
{
    Scene& scene = content.scene;
    // Every value first, so that a refused command changes nothing.
    for (std::size_t i = 0; i < components_.size(); ++i) {
        const ComponentValue& value = components_[i];
        for (std::size_t j = 0; j < i; ++j) {
            if (typeOf(components_[j]) == typeOf(value)) {
                return Error{
                    std::format("create entity: two {} components", typeName(typeOf(value)))};
            }
        }
        if (auto error = checkComponent(value, content.assets)) {
            return Error{"create entity: " + error->message};
        }
        if (const auto* parent = std::get_if<ParentComponent>(&value)) {
            if (auto error = scene.findEntity(parent->entity)) {
                return Error{"create entity: parent: " + error->message};
            }
        }
    }

    // The first time a new ID; a redo the same ID at the same place.
    const bool first = entity_.isNull();
    auto created = first ? scene.createEntity() : scene.restoreEntity(entity_, place_);
    if (!created) {
        return created.error();
    }
    const EntityId entity = created.value();
    for (const ComponentValue& value : components_) {
        if (auto error = addComponent(scene, entity, value)) {
            (void)scene.destroyEntity(entity);
            return error;
        }
    }
    if (first) {
        entity_ = entity;
        place_ = scene.placeOf(entity).value();
    }
    return std::nullopt;
}

std::optional<Error> CreateEntity::revert(DocumentContent& content)
{
    return content.scene.destroyEntity(entity_);
}

DeleteEntity::DeleteEntity(EntityId entity) : entity_(entity) {}

std::optional<Error> DeleteEntity::apply(DocumentContent& content)
{
    Scene& scene = content.scene;
    auto place = scene.placeOf(entity_);
    if (!place) {
        return place.error();
    }
    std::vector<EntityId> children;
    for (const EntityId candidate : scene.entitiesWith<ParentComponent>()) {
        const auto parent = scene.component<ParentComponent>(candidate);
        if (parent && parent.value().entity == entity_) {
            children.push_back(candidate);
        }
    }
    std::vector<ComponentValue> components = componentsOf(scene, entity_);
    if (auto error = scene.destroyEntity(entity_)) {
        return error;
    }
    place_ = place.value();
    components_ = std::move(components);
    children_ = std::move(children);
    return std::nullopt;
}

std::optional<Error> DeleteEntity::revert(DocumentContent& content)
{
    Scene& scene = content.scene;
    if (auto restored = scene.restoreEntity(entity_, place_); !restored) {
        return restored.error();
    }
    for (const ComponentValue& value : components_) {
        if (auto error = addComponent(scene, entity_, value)) {
            (void)scene.destroyEntity(entity_);
            return error;
        }
    }
    for (std::size_t i = 0; i < children_.size(); ++i) {
        if (auto error = scene.addComponent(children_[i], ParentComponent{entity_})) {
            unlink(scene, children_, i);
            (void)scene.destroyEntity(entity_);
            return error;
        }
    }
    return std::nullopt;
}

RenameEntity::RenameEntity(EntityId entity, std::string newName)
    : entity_(entity), name_(std::move(newName))
{
}

std::optional<Error> RenameEntity::apply(DocumentContent& content)
{
    Scene& scene = content.scene;
    const NameComponent value{name_};
    if (auto error = engine::scene::validate(value)) {
        return error;
    }
    if (auto error = scene.findEntity(entity_)) {
        return error;
    }
    std::optional<NameComponent> previous;
    if (scene.hasComponent<NameComponent>(entity_)) {
        previous = scene.component<NameComponent>(entity_).value();
    }
    auto error =
        previous ? scene.replaceComponent(entity_, value) : scene.addComponent(entity_, value);
    if (error) {
        return error;
    }
    previous_ = std::move(previous);
    return std::nullopt;
}

std::optional<Error> RenameEntity::revert(DocumentContent& content)
{
    if (previous_) {
        return content.scene.replaceComponent(entity_, *previous_);
    }
    return content.scene.removeComponent<NameComponent>(entity_);
}

bool RenameEntity::absorb(const Command& next)
{
    const auto* rename = dynamic_cast<const RenameEntity*>(&next);
    if (rename == nullptr || rename->entity_ != entity_) {
        return false;
    }
    name_ = rename->name_;
    return true;
}

SetParent::SetParent(EntityId child, std::optional<EntityId> parent)
    : child_(child), parent_(parent)
{
}

std::string SetParent::name() const
{
    return parent_ ? "Set parent" : "Clear parent";
}

std::optional<Error> SetParent::apply(DocumentContent& content)
{
    Scene& scene = content.scene;
    if (auto error = scene.findEntity(child_)) {
        return error;
    }
    std::optional<ParentComponent> previous;
    if (scene.hasComponent<ParentComponent>(child_)) {
        previous = scene.component<ParentComponent>(child_).value();
    }
    std::optional<Error> error;
    if (parent_) {
        const ParentComponent value{*parent_};
        error =
            previous ? scene.replaceComponent(child_, value) : scene.addComponent(child_, value);
    } else {
        // Without a parent to clear, the scene's own message.
        error = scene.removeComponent<ParentComponent>(child_);
    }
    if (error) {
        return error;
    }
    previous_ = previous;
    return std::nullopt;
}

std::optional<Error> SetParent::revert(DocumentContent& content)
{
    Scene& scene = content.scene;
    if (!previous_) {
        return scene.removeComponent<ParentComponent>(child_);
    }
    if (scene.hasComponent<ParentComponent>(child_)) {
        return scene.replaceComponent(child_, *previous_);
    }
    return scene.addComponent(child_, *previous_);
}

} // namespace editor
