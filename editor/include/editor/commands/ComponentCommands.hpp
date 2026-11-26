#pragma once

// Commands on the components of an entity (M5.5, ADR 0029): add, replace,
// remove. Every value is checked first: its validate, and the assets it names
// against the document's registry (a scene that names an unknown asset could
// not be saved, ADR 0028), then the scene's own rules apply.

#include "editor/document/Command.hpp"
#include "editor/document/ComponentValue.hpp"

#include "engine/scene/EntityId.hpp"

#include <optional>
#include <string>

namespace editor {

// Adds a component the entity does not have.
class AddComponent final : public Command {
public:
    AddComponent(engine::scene::EntityId entity, ComponentValue value);

    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::optional<engine::core::Error> apply(DocumentContent& content) override;
    [[nodiscard]] std::optional<engine::core::Error> revert(DocumentContent& content) override;

private:
    engine::scene::EntityId entity_;
    ComponentValue value_;
};

// Replaces a component the entity has. A continuous edit of one component
// (a dragged field) merges into one step.
class ReplaceComponent final : public Command {
public:
    ReplaceComponent(engine::scene::EntityId entity, ComponentValue value);

    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::optional<engine::core::Error> apply(DocumentContent& content) override;
    [[nodiscard]] std::optional<engine::core::Error> revert(DocumentContent& content) override;
    [[nodiscard]] bool absorb(const Command& next) override;

private:
    engine::scene::EntityId entity_;
    ComponentValue value_;
    std::optional<ComponentValue> previous_;
};

// Removes a component the entity has.
class RemoveComponent final : public Command {
public:
    RemoveComponent(engine::scene::EntityId entity, ComponentType type);

    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::optional<engine::core::Error> apply(DocumentContent& content) override;
    [[nodiscard]] std::optional<engine::core::Error> revert(DocumentContent& content) override;

private:
    engine::scene::EntityId entity_;
    ComponentType type_;
    std::optional<ComponentValue> previous_;
};

} // namespace editor
