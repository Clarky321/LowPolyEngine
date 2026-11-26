# ADR 0029: Editor document and commands

- **Status:** Accepted (2026-11-26, M5.5). The plan asked M5.5 to decide this ADR (`docs/milestones/m5-minimal-editor.md`, M5.5, "Decisions to make"); the decisions were taken under `docs/DEVELOPMENT_WORKFLOW.md` §4.
- **Date:** 2026-11-26
- **Related:** ADR 0028 (the project the document holds), ADR 0021 (the scene; Amendment 1: an entity's place), ADR 0023 (scene documents, written in creation order), ADR 0020 (persistent IDs), M5 plan "Architecture principles" (the editor changes data only through commands)

## Context

The editor's panels and viewport (M5.6–M5.11) change the scene and the world settings. Every change must be undoable and redoable exactly: after any sequence of commands and the same number of undos, the scene document is byte for byte the original, and after the redos byte for byte the edited one. Continuous edits (a dragged value, a name being typed) must be one undo step. The editor must know whether its content differs from the files. All of it must be testable without a window.

A scene document lists entities in creation order (ADR 0023). `Scene::createEntity(id)` puts a re-created entity at the end, so an undone delete would move the entity and change the document's bytes.

## Decision

1. **The document** (`editor/include/editor/document/EditorDocument.hpp`) owns the open project (`project::Project`, ADR 0028): its scene, world settings and asset registry.
   - The content is read through const accessors only, and it changes only through `execute(command)`, `undo()` and `redo()`.
   - `save()` saves the project (ADR 0028) and marks the document saved on success.
   - The document, the commands and the selection are UI-free. A test reads their includes: the standard library, the engine's core, assets, scene, worldgen and project, and each other.
2. **The command interface** (`Command`):
   - `name()` for menus.
   - `apply(content)` makes the change. A failure changes nothing and says why. Every command checks its input before it changes anything: each component's `validate`; each asset a component names against the registry, with its type, since a scene naming an unknown asset could not be saved (ADR 0028); the entities it needs.
   - `revert(content)` takes the last apply back exactly. `apply` and `revert` alternate strictly, each on the state the other left; a redo is an `apply` again.
   - `absorb(next)` folds the next step of a continuous edit into the command.
   - `DocumentContent` gives commands the scene and the world settings to change, and the registry to read.
3. **How a command stores its change:** its input, plus what it found when it applied. No snapshot of the whole document.
   - Old values: the replaced component, the previous name or parent, the previous world settings.
   - For a deleted entity: its place in the creation order, every component (its own parent link included) and its children's IDs.
   - A created entity keeps its ID and place after the first apply, so a redo creates it with the same ID at the same place.
4. **Entity IDs and order never change through undo and redo.** The scene gains two functions (ADR 0021, Amendment 1):
   - `Scene::placeOf(entity)` gives an entity's place in the creation order;
   - `Scene::restoreEntity(id, place)` creates an entity with its ID at a free place the scene gave out before. New entities never get an old place, so a deleted entity's place stays free for its undo.
   - Undoing a delete restores the entity at its place, then its components, then its children's links. Deleting an entity keeps its children; they lose their link to it (`Scene::destroyEntity`).
5. **The commands** (`editor/include/editor/commands/`):
   - `CreateEntity` (with given components, at most one of each type);
   - `DeleteEntity`;
   - `RenameEntity` (adds a name component if there is none);
   - `AddComponent`, `ReplaceComponent`, `RemoveComponent`, for any component type, through the value type `ComponentValue`;
   - `SetParent` (set, change or clear; the scene refuses cycles, and clearing without a parent fails);
   - `SetWorldSettings` (only settings that pass `worldgen::validate()`).
6. **Merging:**
   - Every step of one continuous edit carries the same `MergeKey`, other than `kNoMerge`; the UI chooses a key per edit.
   - A command merges into the last step when it has the same key and the last step's command absorbs it: the same command type on the same value (the same entity and component type for `ReplaceComponent`, the same entity for `RenameEntity`, any `SetWorldSettings`).
   - The merged step reverts to the state before the edit, and its redo gives the state after the last command.
   - Never merged: commands that add or remove things; across a save (the saved state must stay in the history); and after an undo (a new command then drops the redo steps and starts a new step).
7. **The limit:** 1000 undo steps by default (`kDefaultHistoryLimit`), set per document. The oldest steps go first.
8. **The dirty state** is a clean marker: the number of applied steps when the content equalled the files. A new document is clean.
   - Undoing past a save makes it dirty, and redoing back to the save makes it clean again: the history is kept across saves.
   - A saved state that leaves the history stays dirty until the next save: dropped by the limit, or with the redo steps when a new command follows an undo.
9. **The selection** (`editor/include/editor/selection/Selection.hpp`): entity IDs in the order selected; the last one is the primary selection.
   - Selecting is not a document change and not an undo step.
   - The document keeps the selection valid: after every execute, undo and redo, entities the scene no longer contains leave the selection.
   - An entity that an undo or redo brings back is selected again if it was selected when it left. Otherwise undo and redo leave the user's selection as it is.

## Consequences

- Any sequence of commands undoes and redoes byte for byte, which the tests check with 10 000 mixed commands over a scene with entities of its own (with checkpoints every 500 steps), and for each command alone.
- A command costs its input and what it replaced. Deleting an entity costs its components and a scan of the parent links, so the history stays small even for long sessions.
- Panels (M5.7–M5.10) and picking (M5.9) work on the document and its commands; the viewport's scene view (M5.6) reads the document after changes.
- **Not covered:**
  - Several scenes in one document.
  - Changes of the asset registry (asset import is M6).
  - Undoing a save itself (the files stay as saved; undo changes the content, which then differs from them).
