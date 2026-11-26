#pragma once

// Commands on the scene's entities (M5.5, ADR 0029). Entity IDs never change
// through undo and redo: a deleted entity comes back with its ID, at its
// place in the creation order, with its components and its children's links.

#include "editor/document/Command.hpp"
#include "editor/document/ComponentValue.hpp"

#include "engine/scene/EntityId.hpp"
#include "engine/scene/Scene.hpp"

#include <optional>
#include <string>
#include <vector>

namespace editor {

// A new entity with a new ID from the scene, with the given components (at
// most one of each type). Every component is checked first (its validate,
// its assets in the registry, a parent the scene contains), so a refused
// command changes no entity.
class CreateEntity final : public Command {
public:
    explicit CreateEntity(std::vector<ComponentValue> components = {});

    [[nodiscard]] std::string name() const override { return "Create entity"; }
    [[nodiscard]] std::optional<engine::core::Error> apply(DocumentContent& content) override;
    [[nodiscard]] std::optional<engine::core::Error> revert(DocumentContent& content) override;

    // The created entity; null until the first apply.
    [[nodiscard]] engine::scene::EntityId entity() const noexcept { return entity_; }

private:
    std::vector<ComponentValue> components_;
    engine::scene::EntityId entity_;
    engine::scene::EntityPlace place_;
};

// Deletes an entity with its components. Its children stay, without their
// link to it (Scene::destroyEntity); undo restores the entity with its ID,
// its place, its components (its own parent link included) and the links of
// its children.
class DeleteEntity final : public Command {
public:
    explicit DeleteEntity(engine::scene::EntityId entity);

    [[nodiscard]] std::string name() const override { return "Delete entity"; }
    [[nodiscard]] std::optional<engine::core::Error> apply(DocumentContent& content) override;
    [[nodiscard]] std::optional<engine::core::Error> revert(DocumentContent& content) override;

private:
    engine::scene::EntityId entity_;
    engine::scene::EntityPlace place_;
    std::vector<ComponentValue> components_;
    std::vector<engine::scene::EntityId> children_;
};

// Gives an entity a name (its name component, added if it has none). A
// continuous edit (typing) merges into one step.
class RenameEntity final : public Command {
public:
    RenameEntity(engine::scene::EntityId entity, std::string newName);

    [[nodiscard]] std::string name() const override { return "Rename entity"; }
    [[nodiscard]] std::optional<engine::core::Error> apply(DocumentContent& content) override;
    [[nodiscard]] std::optional<engine::core::Error> revert(DocumentContent& content) override;
    [[nodiscard]] bool absorb(const Command& next) override;

private:
    engine::scene::EntityId entity_;
    std::string name_;
    std::optional<engine::scene::NameComponent> previous_;
};

// Sets an entity's parent, or clears it with std::nullopt. The scene refuses
// an unknown parent and a link that closes a cycle; clearing an entity
// without a parent fails.
class SetParent final : public Command {
public:
    SetParent(engine::scene::EntityId child, std::optional<engine::scene::EntityId> parent);

    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::optional<engine::core::Error> apply(DocumentContent& content) override;
    [[nodiscard]] std::optional<engine::core::Error> revert(DocumentContent& content) override;

private:
    engine::scene::EntityId child_;
    std::optional<engine::scene::EntityId> parent_;
    std::optional<engine::scene::ParentComponent> previous_;
};

} // namespace editor
