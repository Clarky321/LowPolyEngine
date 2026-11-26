#pragma once

// The editor's document (M5.5, ADR 0029): the open project (its scene, world
// settings and asset registry), the history of commands for undo and redo,
// the dirty state against the last save, and the selection. Everything
// changes through execute(), undo() and redo(); the content is read through
// const accessors only. UI-free: no Raven, SDL or OpenGL.

#include "editor/document/Command.hpp"
#include "editor/selection/Selection.hpp"

#include "engine/core/Result.hpp"
#include "engine/project/Project.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace editor {

// Undo steps kept; the oldest go first.
inline constexpr std::size_t kDefaultHistoryLimit = 1000;

class EditorDocument {
public:
    // A document for an opened or new project, as saved: clean, with an
    // empty history and nothing selected. A limit of 0 is taken as 1.
    explicit EditorDocument(engine::project::Project project,
                            std::size_t historyLimit = kDefaultHistoryLimit);

    EditorDocument(const EditorDocument&) = delete;
    EditorDocument& operator=(const EditorDocument&) = delete;
    EditorDocument(EditorDocument&&) = default;
    EditorDocument& operator=(EditorDocument&&) = default;
    ~EditorDocument() = default;

    [[nodiscard]] const engine::project::Project& project() const noexcept { return project_; }
    [[nodiscard]] const engine::scene::Scene& scene() const noexcept { return project_.startScene; }
    [[nodiscard]] const engine::worldgen::WorldGenSettings& worldSettings() const noexcept
    {
        return project_.worldSettings;
    }
    [[nodiscard]] const engine::assets::AssetRegistry& assets() const noexcept
    {
        return project_.assets;
    }

    // Applies the command and keeps it for undo; the redo steps are dropped.
    // With a merge key other than kNoMerge, a command that continues the last
    // step's edit (the same key, and the last step's command absorbs it) is
    // folded into that step, unless the document was saved right after it.
    // On failure nothing changed: neither the content nor the history.
    [[nodiscard]] std::optional<engine::core::Error> execute(std::unique_ptr<Command> command,
                                                             MergeKey mergeKey = kNoMerge);

    [[nodiscard]] bool canUndo() const noexcept { return current_ > 0; }
    [[nodiscard]] bool canRedo() const noexcept { return current_ < steps_.size(); }
    // The name of the step undo() or redo() would take, for menus.
    [[nodiscard]] std::optional<std::string> undoName() const;
    [[nodiscard]] std::optional<std::string> redoName() const;
    [[nodiscard]] std::size_t undoCount() const noexcept { return current_; }
    [[nodiscard]] std::size_t redoCount() const noexcept { return steps_.size() - current_; }
    [[nodiscard]] std::size_t historyLimit() const noexcept { return limit_; }

    // Take the last step back, or apply the next one again. Fail with nothing
    // to undo or redo, or with the command's error (then the history stays).
    [[nodiscard]] std::optional<engine::core::Error> undo();
    [[nodiscard]] std::optional<engine::core::Error> redo();

    // Whether the content differs from the last save (or the opened project).
    // Undoing past a save makes the document dirty; redoing to it makes it
    // clean again. A saved state no longer in the history (dropped by the
    // limit or by a new command after undoing) stays dirty until the next
    // save.
    [[nodiscard]] bool isDirty() const noexcept { return !clean_ || *clean_ != current_; }
    // Records that the content now equals the project's files.
    void markSaved() noexcept { clean_ = current_; }
    // Saves the project (engine::project::saveProject); on success marks the
    // document saved. On failure the document stays as it was.
    [[nodiscard]] engine::core::Result<engine::project::SaveReport> save();

    [[nodiscard]] Selection& selection() noexcept { return selection_; }
    [[nodiscard]] const Selection& selection() const noexcept { return selection_; }

private:
    struct Step {
        std::unique_ptr<Command> command;
        MergeKey mergeKey = kNoMerge;
        // Selected entities that the step's apply took out of the scene, and
        // those its revert took out; the opposite operation selects them
        // again.
        std::vector<engine::scene::EntityId> deselectedByApply;
        std::vector<engine::scene::EntityId> deselectedByRevert;
    };

    [[nodiscard]] DocumentContent content() noexcept;

    engine::project::Project project_;
    std::size_t limit_;
    // steps_[0, current_) are applied; steps_[current_, size) can be redone.
    std::vector<Step> steps_;
    std::size_t current_ = 0;
    // The number of applied steps when the content equalled the files; empty
    // when that state is no longer in the history.
    std::optional<std::size_t> clean_ = 0;
    Selection selection_;
};

} // namespace editor
