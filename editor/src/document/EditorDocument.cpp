#include "editor/document/EditorDocument.hpp"

#include <algorithm>
#include <iterator>
#include <utility>

namespace editor {

bool Command::absorb(const Command& /*next*/)
{
    return false;
}

EditorDocument::EditorDocument(engine::project::Project project, std::size_t historyLimit)
    : project_(std::move(project)), limit_(std::max<std::size_t>(historyLimit, 1))
{
}

DocumentContent EditorDocument::content() noexcept
{
    return DocumentContent{project_.startScene, project_.worldSettings, project_.assets};
}

std::optional<engine::core::Error> EditorDocument::execute(std::unique_ptr<Command> command,
                                                           MergeKey mergeKey)
{
    if (!command) {
        return engine::core::Error{"no command to execute"};
    }
    DocumentContent target = content();
    if (auto error = command->apply(target)) {
        return error;
    }
    ++version_;
    std::vector<engine::scene::EntityId> deselected = selection_.prune(project_.startScene);

    // The next step of the last step's continuous edit: one undo step. Not
    // across a save, so that the saved state stays in the history.
    const bool atEnd = current_ == steps_.size();
    if (mergeKey != kNoMerge && atEnd && current_ > 0 && clean_ != current_) {
        Step& last = steps_[current_ - 1];
        if (last.mergeKey == mergeKey && last.command->absorb(*command)) {
            last.deselectedByApply.insert(last.deselectedByApply.end(), deselected.begin(),
                                          deselected.end());
            return std::nullopt;
        }
    }

    // A new step: the redo steps go, and with them a saved state among them.
    steps_.erase(steps_.begin() + static_cast<std::ptrdiff_t>(current_), steps_.end());
    if (clean_ && *clean_ > current_) {
        clean_.reset();
    }
    steps_.push_back(Step{std::move(command), mergeKey, std::move(deselected), {}});
    ++current_;
    if (steps_.size() > limit_) {
        steps_.erase(steps_.begin());
        --current_;
        if (clean_) {
            if (*clean_ == 0) {
                clean_.reset();
            } else {
                --*clean_;
            }
        }
    }
    return std::nullopt;
}

std::optional<std::string> EditorDocument::undoName() const
{
    if (!canUndo()) {
        return std::nullopt;
    }
    return steps_[current_ - 1].command->name();
}

std::optional<std::string> EditorDocument::redoName() const
{
    if (!canRedo()) {
        return std::nullopt;
    }
    return steps_[current_].command->name();
}

std::optional<engine::core::Error> EditorDocument::undo()
{
    if (!canUndo()) {
        return engine::core::Error{"nothing to undo"};
    }
    Step& step = steps_[current_ - 1];
    DocumentContent target = content();
    if (auto error = step.command->revert(target)) {
        return engine::core::Error{"cannot undo '" + step.command->name() + "': " + error->message};
    }
    --current_;
    ++version_;
    step.deselectedByRevert = selection_.prune(project_.startScene);
    selection_.restore(step.deselectedByApply, project_.startScene);
    return std::nullopt;
}

std::optional<engine::core::Error> EditorDocument::redo()
{
    if (!canRedo()) {
        return engine::core::Error{"nothing to redo"};
    }
    Step& step = steps_[current_];
    DocumentContent target = content();
    if (auto error = step.command->apply(target)) {
        return engine::core::Error{"cannot redo '" + step.command->name() + "': " + error->message};
    }
    ++current_;
    ++version_;
    step.deselectedByApply = selection_.prune(project_.startScene);
    selection_.restore(step.deselectedByRevert, project_.startScene);
    return std::nullopt;
}

engine::core::Result<engine::project::SaveReport> EditorDocument::save()
{
    auto saved = engine::project::saveProject(project_);
    if (saved) {
        markSaved();
    }
    return saved;
}

} // namespace editor
