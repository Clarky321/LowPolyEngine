#pragma once

// A change to the editor's document (M5.5, ADR 0029). Panels and the
// viewport never change the scene or the world settings directly: they make
// a command and give it to the document, which applies it and keeps it for
// undo and redo.
//
// A command stores what it needs to take its change back exactly: its own
// input, and what it found before applying (an old value, a deleted
// entity's place, components and children). apply() and revert() alternate
// strictly: apply, revert, apply (a redo), revert, ... each on the state the
// other left, so a revert never has to guess.

#include "engine/assets/AssetRegistry.hpp"
#include "engine/core/Result.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/worldgen/WorldGenSettings.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace editor {

// What commands change: the open project's scene and world settings. The
// asset registry is read only (asset import is M6).
struct DocumentContent {
    engine::scene::Scene& scene;
    engine::worldgen::WorldGenSettings& worldSettings;
    const engine::assets::AssetRegistry& assets;
};

// One continuous edit of one value (a dragged field, a name being typed):
// the commands of the edit carry the same key, other than kNoMerge, and the
// document folds them into one undo step.
using MergeKey = std::uint64_t;
inline constexpr MergeKey kNoMerge = 0;

class Command {
public:
    Command(const Command&) = delete;
    Command& operator=(const Command&) = delete;
    Command(Command&&) = delete;
    Command& operator=(Command&&) = delete;
    virtual ~Command() = default;

    // A short name for menus and messages, e.g. "Rename entity".
    [[nodiscard]] virtual std::string name() const = 0;

    // Makes the change. On failure nothing changed and the error says why;
    // the command may then be dropped.
    [[nodiscard]] virtual std::optional<engine::core::Error> apply(DocumentContent& content) = 0;

    // Takes the change of the last apply() back exactly. Called only right
    // after a successful apply(), on the state it left.
    [[nodiscard]] virtual std::optional<engine::core::Error> revert(DocumentContent& content) = 0;

    // Folds `next`, the following step of the same continuous edit, into this
    // command, which was applied just before it: afterwards this command's
    // revert() goes back to the state before itself, and its apply() to the
    // state after `next`. Returns false, changing nothing, when `next` is not
    // the same change to the same value. Never merges by default.
    [[nodiscard]] virtual bool absorb(const Command& next);

protected:
    Command() = default;
};

} // namespace editor
