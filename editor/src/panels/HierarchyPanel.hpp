#pragma once

// The hierarchy panel (M5.7): the scene's entities as Raven's TreeView, in
// the rows of hierarchyRows (creation order, children under their parent),
// each shown by its name or its ID. Every action is one command of the
// editor's document, so one undo step:
//   - Create (button): a new top-level entity named "Entity" with a transform
//     at the defaults, then selected;
//   - Delete (button, or the Delete key in the tree): the selected entity; its
//     children stay, at the top level;
//   - rename in place (F2, Enter or a double click in the tree): a text field
//     over the row; Enter or leaving the field renames, Escape cancels;
//   - drag an entity onto another to make it its child, onto the empty space
//     below the rows to put it at the top level.
// A command the document refuses (a name the scene refuses, a parent link that
// would close a cycle) changes nothing; its message goes to the log as a
// warning, and so to the console.
//
// The tree follows the document: update(), once per frame outside Raven's
// frame, compares the document's version (ADR 0029 Amendment 1) and, when it
// changed, relabels the items if the tree kept its shape and rebuilds them
// otherwise, keeping the branches the user collapsed. The selection is the
// document's: a click in the tree selects in the document, and update() shows
// the document's primary selection in the tree. Without a document (no
// project) the tree is empty and the buttons are disabled.
//
// The widgets are in the shell's layout (editor/layouts/shell.xml); Raven
// holds a pointer to the panel while it is attached.

#include "editor/document/EditorDocument.hpp"
#include "editor/panels/HierarchyRows.hpp"

#include "engine/core/Result.hpp"
#include "engine/scene/EntityId.hpp"

#include <Raven.h>

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace editor {

// The name of an entity made by the Create button.
inline constexpr std::string_view kNewEntityName = "Entity";
// The type of the hierarchy's drags (Rdrag_begin): the dragged item's handle.
inline constexpr const char* kEntityDragType = "lpe/entity";

class HierarchyPanel {
public:
    HierarchyPanel() = default;
    HierarchyPanel(const HierarchyPanel&) = delete;
    HierarchyPanel& operator=(const HierarchyPanel&) = delete;
    HierarchyPanel(HierarchyPanel&&) = delete;
    HierarchyPanel& operator=(HierarchyPanel&&) = delete;
    ~HierarchyPanel() { detach(); }

    // Takes the hierarchy's widgets of `window` (the shell's layout), connects
    // them and shows `document` (null: no project). Fails, attaching nothing,
    // when a widget is missing.
    [[nodiscard]] std::optional<engine::core::Error> attach(Rid window, EditorDocument* document);
    // Disconnects the widgets; nothing happens when nothing is attached.
    void detach() noexcept;
    [[nodiscard]] bool attached() const noexcept { return tree_ != R_INVALID_ID; }

    // Follows the document's changes and selection, and starts a rename the
    // tree asked for. Once per frame, outside Raven's frame.
    void update();

    // The rows the tree shows, in its order.
    [[nodiscard]] const std::vector<HierarchyRow>& rows() const noexcept { return rows_; }
    [[nodiscard]] Rid tree() const noexcept { return tree_; }
    [[nodiscard]] Rid renameField() const noexcept { return renameField_; }
    [[nodiscard]] Rid createButton() const noexcept { return create_; }
    [[nodiscard]] Rid deleteButton() const noexcept { return delete_; }
    // The tree's item of an entity, or R_INVALID_ID.
    [[nodiscard]] Rid itemOf(engine::scene::EntityId entity) const;
    // The entity of one of the tree's items, or the null ID.
    [[nodiscard]] engine::scene::EntityId entityOf(Rid item) const;
    // The entity being renamed.
    [[nodiscard]] std::optional<engine::scene::EntityId> renaming() const noexcept
    {
        return renaming_;
    }

    // Where an entity's row is, relative to the tree, in logical units; empty
    // when the row is not in the tree's visible part (or not laid out yet).
    struct RowRect {
        float x = 0.0F;
        float y = 0.0F;
        float width = 0.0F;
        float height = 0.0F;
    };
    [[nodiscard]] std::optional<RowRect> rowRect(engine::scene::EntityId entity) const;

    // How often the items were built anew and relabelled (for the checks).
    [[nodiscard]] std::uint64_t rebuildCount() const noexcept { return rebuilds_; }
    [[nodiscard]] std::uint64_t relabelCount() const noexcept { return relabels_; }

private:
    static void onCreate(Rid button, void* user) noexcept;
    static void onDelete(Rid button, void* user) noexcept;
    static void onSelection(Rid tree, Rid item, void* user) noexcept;
    static void onActivated(Rid tree, Rid item, void* user) noexcept;
    static int onTreeEvent(Rid tree, const REvent* event, void* user) noexcept;
    static int onDrop(Rid target, const RDropEvent* event, void* user) noexcept;
    static void onRenameCommitted(Rid field, const char* text, void* user) noexcept;
    static int onRenameEvent(Rid field, const REvent* event, void* user) noexcept;

    void createEntity();
    void deleteSelected();
    void selectFromTree(Rid item);
    bool treeKey(const REvent& event);
    int drop(const RDropEvent& event);
    void reparent(engine::scene::EntityId child, engine::scene::EntityId parent);
    void beginRename(engine::scene::EntityId entity);
    // Renames with the field's text when it differs from the name; `refocus`
    // gives the tree the keyboard again (Enter), unlike a click elsewhere.
    void commitRename(bool refocus);
    void cancelRename(bool refocus);
    void hideRenameField(bool refocus);

    void follow();
    void rebuild(std::vector<HierarchyRow> rows);
    void relabel(std::vector<HierarchyRow> rows);
    void showSelection();
    void refreshButtons();
    void report(std::string_view action, engine::scene::EntityId entity,
                const engine::core::Error& error) const;

    EditorDocument* document_ = nullptr;
    Rid tree_ = R_INVALID_ID;
    Rid renameField_ = R_INVALID_ID;
    Rid create_ = R_INVALID_ID;
    Rid delete_ = R_INVALID_ID;

    std::vector<HierarchyRow> rows_;
    std::map<engine::scene::EntityId, Rid> items_;
    std::map<Rid, engine::scene::EntityId> entities_;
    // Entities whose branch the user closed; kept while they have no children
    // or are gone, so an undo brings the branch back closed.
    std::set<engine::scene::EntityId> collapsed_;
    std::uint64_t version_ = 0;
    bool shown_ = false;
    // The entity the tree shows selected (null for none), once shown.
    engine::scene::EntityId shownSelection_;
    bool selectionShown_ = false;
    // The panel changes the tree itself: its selection callback is not the user's.
    bool changingTree_ = false;
    // What the buttons were last set to: R_TRUE, R_FALSE, or not yet set.
    static constexpr int kUnknownState = -1;
    int createEnabled_ = kUnknownState;
    int deleteEnabled_ = kUnknownState;

    std::optional<engine::scene::EntityId> renaming_;
    // A rename the tree asked for, started by the next update().
    std::optional<engine::scene::EntityId> pendingRename_;

    std::uint64_t rebuilds_ = 0;
    std::uint64_t relabels_ = 0;
};

} // namespace editor
