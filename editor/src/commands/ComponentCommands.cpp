#include "editor/commands/ComponentCommands.hpp"

#include <string>
#include <utility>

namespace editor {

using engine::core::Error;
using engine::scene::EntityId;

AddComponent::AddComponent(EntityId entity, ComponentValue value)
    : entity_(entity), value_(std::move(value))
{
}

std::string AddComponent::name() const
{
    return "Add " + std::string(typeName(typeOf(value_)));
}

std::optional<Error> AddComponent::apply(DocumentContent& content)
{
    if (auto error = checkComponent(value_, content.assets)) {
        return error;
    }
    return addComponent(content.scene, entity_, value_);
}

std::optional<Error> AddComponent::revert(DocumentContent& content)
{
    return removeComponent(content.scene, entity_, typeOf(value_));
}

ReplaceComponent::ReplaceComponent(EntityId entity, ComponentValue value)
    : entity_(entity), value_(std::move(value))
{
}

std::string ReplaceComponent::name() const
{
    return "Change " + std::string(typeName(typeOf(value_)));
}

std::optional<Error> ReplaceComponent::apply(DocumentContent& content)
{
    if (auto error = checkComponent(value_, content.assets)) {
        return error;
    }
    auto previous = componentOf(content.scene, entity_, typeOf(value_));
    if (!previous) {
        return previous.error();
    }
    if (auto error = replaceComponent(content.scene, entity_, value_)) {
        return error;
    }
    previous_ = std::move(previous).value();
    return std::nullopt;
}

std::optional<Error> ReplaceComponent::revert(DocumentContent& content)
{
    if (!previous_) {
        return Error{"change component: nothing to put back"};
    }
    return replaceComponent(content.scene, entity_, *previous_);
}

bool ReplaceComponent::absorb(const Command& next)
{
    const auto* replace = dynamic_cast<const ReplaceComponent*>(&next);
    if (replace == nullptr || replace->entity_ != entity_ ||
        typeOf(replace->value_) != typeOf(value_)) {
        return false;
    }
    value_ = replace->value_;
    return true;
}

RemoveComponent::RemoveComponent(EntityId entity, ComponentType type) : entity_(entity), type_(type)
{
}

std::string RemoveComponent::name() const
{
    return "Remove " + std::string(typeName(type_));
}

std::optional<Error> RemoveComponent::apply(DocumentContent& content)
{
    auto previous = componentOf(content.scene, entity_, type_);
    if (!previous) {
        return previous.error();
    }
    if (auto error = removeComponent(content.scene, entity_, type_)) {
        return error;
    }
    previous_ = std::move(previous).value();
    return std::nullopt;
}

std::optional<Error> RemoveComponent::revert(DocumentContent& content)
{
    if (!previous_) {
        return Error{"remove component: nothing to put back"};
    }
    return addComponent(content.scene, entity_, *previous_);
}

} // namespace editor
