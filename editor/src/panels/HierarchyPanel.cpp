#include "HierarchyPanel.hpp"

#include "editor/commands/EntityCommands.hpp"
#include "editor/document/ComponentValue.hpp"

#include "engine/core/Log.hpp"
#include "engine/scene/NameComponent.hpp"
#include "engine/scene/ParentComponent.hpp"
#include "engine/scene/TransformComponent.hpp"

#include <charconv>
#include <exception>
#include <memory>
#include <system_error>
#include <utility>

namespace editor {

using engine::scene::EntityId;

namespace {

// The widgets of the hierarchy in the shell's layout.
constexpr const char* kTreeName = "hierarchy-tree";
constexpr const char* kRenameName = "hierarchy-rename";
constexpr const char* kCreateName = "hierarchy-create";
constexpr const char* kDeleteName = "hierarchy-delete";

// The first item handle of a tree drag's data: the handles in decimal,
// separated by spaces (Rtree_view_set_drag_type).
Rid firstHandle(const char* data) noexcept
{
    if (data == nullptr) {
        return R_INVALID_ID;
    }
    const std::string_view text(data);
    const std::string_view first = text.substr(0, text.find(' '));
    Rid handle = R_INVALID_ID;
    const auto [end, error] = std::from_chars(first.data(), first.data() + first.size(), handle);
    if (error != std::errc{} || end != first.data() + first.size()) {
        return R_INVALID_ID;
    }
    return handle;
}

// Raven calls the panel from C: nothing may be thrown back into it (AGENTS.md
// §4). What fails is logged.
template <typename Action>
void guarded(std::string_view what, Action&& action) noexcept
{
    try {
        std::forward<Action>(action)();
    } catch (const std::exception& error) {
        engine::core::logError("editor", "hierarchy: {} failed: {}", what, error.what());
    } catch (...) {
        engine::core::logError("editor", "hierarchy: {} failed", what);
    }
}

HierarchyPanel& panelOf(void* user) noexcept
{
    return *static_cast<HierarchyPanel*>(user);
}

} // namespace

std::optional<engine::core::Error> HierarchyPanel::attach(Rid window, EditorDocument* document)
{
    detach();
    const auto find = [window](const char* name, Rid& found) -> std::optional<engine::core::Error> {
        found = Rwidget_find(window, name);
        if (found == R_INVALID_ID) {
            return engine::core::Error{std::string("the shell layout has no widget named '") +
                                       name + "'"};
        }
        return std::nullopt;
    };
    Rid treeId = R_INVALID_ID;
    Rid fieldId = R_INVALID_ID;
    Rid createId = R_INVALID_ID;
    Rid deleteId = R_INVALID_ID;
    for (auto error : {find(kTreeName, treeId), find(kRenameName, fieldId),
                       find(kCreateName, createId), find(kDeleteName, deleteId)}) {
        if (error) {
            return error;
        }
    }
    tree_ = treeId;
    renameField_ = fieldId;
    create_ = createId;
    delete_ = deleteId;
    document_ = document;

    Rtree_view_set_selection_changed_callback(tree_, &HierarchyPanel::onSelection, this);
    Rtree_view_set_activated_callback(tree_, &HierarchyPanel::onActivated, this);
    Rwidget_set_event_callback(tree_, &HierarchyPanel::onTreeEvent, this);
    // Without a document nothing is dragged or dropped.
    Rtree_view_set_drag_type(tree_, document_ != nullptr ? kEntityDragType : nullptr);
    Rwidget_set_drop_callback(tree_, document_ != nullptr ? &HierarchyPanel::onDrop : nullptr,
                              this);
    Rbutton_set_clicked_callback(create_, &HierarchyPanel::onCreate, this);
    Rbutton_set_clicked_callback(delete_, &HierarchyPanel::onDelete, this);
    Rtext_input_set_committed_callback(renameField_, &HierarchyPanel::onRenameCommitted, this);
    Rwidget_set_event_callback(renameField_, &HierarchyPanel::onRenameEvent, this);
    Rwidget_set_visibility(renameField_, R_COLLAPSED);

    rows_.clear();
    items_.clear();
    entities_.clear();
    collapsed_.clear();
    version_ = 0;
    shown_ = false;
    shownSelection_ = EntityId{};
    selectionShown_ = false;
    renaming_.reset();
    pendingRename_.reset();
    createEnabled_ = kUnknownState;
    deleteEnabled_ = kUnknownState;
    changingTree_ = true;
    Rtree_view_clear_items(tree_);
    changingTree_ = false;

    follow();
    showSelection();
    refreshButtons();
    return std::nullopt;
}

void HierarchyPanel::detach() noexcept
{
    if (tree_ == R_INVALID_ID) {
        return;
    }
    // After Rshutdown the widgets are gone, and Raven holds no callback.
    if (Ris_initialized() != R_FALSE) {
        if (Rwidget_is_valid(tree_) != R_FALSE) {
            Rtree_view_set_selection_changed_callback(tree_, nullptr, nullptr);
            Rtree_view_set_activated_callback(tree_, nullptr, nullptr);
            Rwidget_set_event_callback(tree_, nullptr, nullptr);
            Rtree_view_set_drag_type(tree_, nullptr);
            Rwidget_set_drop_callback(tree_, nullptr, nullptr);
        }
        if (Rwidget_is_valid(create_) != R_FALSE) {
            Rbutton_set_clicked_callback(create_, nullptr, nullptr);
        }
        if (Rwidget_is_valid(delete_) != R_FALSE) {
            Rbutton_set_clicked_callback(delete_, nullptr, nullptr);
        }
        if (Rwidget_is_valid(renameField_) != R_FALSE) {
            Rtext_input_set_committed_callback(renameField_, nullptr, nullptr);
            Rwidget_set_event_callback(renameField_, nullptr, nullptr);
        }
    }
    tree_ = R_INVALID_ID;
    renameField_ = R_INVALID_ID;
    create_ = R_INVALID_ID;
    delete_ = R_INVALID_ID;
    document_ = nullptr;
    renaming_.reset();
    pendingRename_.reset();
}

void HierarchyPanel::update()
{
    if (!attached()) {
        return;
    }
    follow();
    if (pendingRename_) {
        const EntityId entity = *pendingRename_;
        pendingRename_.reset();
        beginRename(entity);
    }
    showSelection();
    refreshButtons();
}

Rid HierarchyPanel::itemOf(EntityId entity) const
{
    const auto found = items_.find(entity);
    return found != items_.end() ? found->second : R_INVALID_ID;
}

EntityId HierarchyPanel::entityOf(Rid item) const
{
    const auto found = entities_.find(item);
    return found != entities_.end() ? found->second : EntityId{};
}

std::optional<HierarchyPanel::RowRect> HierarchyPanel::rowRect(EntityId entity) const
{
    const Rid item = itemOf(entity);
    if (item == R_INVALID_ID) {
        return std::nullopt;
    }
    float x = 0.0F;
    float y = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
    Rwidget_get_rect(tree_, &x, &y, &width, &height);
    // Raven gives the item at a point, not the point of an item: the row is
    // found by probing the tree from top to bottom, one unit at a time.
    bool found = false;
    float top = 0.0F;
    float bottom = 0.0F;
    const int steps = static_cast<int>(height);
    for (int step = 0; step < steps; ++step) {
        const float probe = static_cast<float>(step) + 0.5F;
        if (Rtree_view_get_item_at(tree_, 1.0F, probe) == item) {
            if (!found) {
                top = probe - 0.5F;
                found = true;
            }
            bottom = probe + 0.5F;
        } else if (found) {
            break;
        }
    }
    if (!found) {
        return std::nullopt;
    }
    return RowRect{0.0F, top, width, bottom - top};
}

void HierarchyPanel::onCreate(Rid /*button*/, void* user) noexcept
{
    guarded("creating an entity", [user] { panelOf(user).createEntity(); });
}

void HierarchyPanel::onDelete(Rid /*button*/, void* user) noexcept
{
    guarded("deleting an entity", [user] { panelOf(user).deleteSelected(); });
}

void HierarchyPanel::onSelection(Rid /*tree*/, Rid item, void* user) noexcept
{
    guarded("selecting", [user, item] { panelOf(user).selectFromTree(item); });
}

void HierarchyPanel::onActivated(Rid /*tree*/, Rid item, void* user) noexcept
{
    guarded("starting a rename", [user, item] {
        HierarchyPanel& panel = panelOf(user);
        const EntityId entity = panel.entityOf(item);
        if (!entity.isNull()) {
            // Started by update(), outside the tree's own handling of the click.
            panel.pendingRename_ = entity;
        }
    });
}

int HierarchyPanel::onTreeEvent(Rid /*tree*/, const REvent* event, void* user) noexcept
{
    bool used = false;
    if (event != nullptr) {
        guarded("a key in the tree",
                [user, event, &used] { used = panelOf(user).treeKey(*event); });
    }
    return used ? R_TRUE : R_FALSE;
}

int HierarchyPanel::onDrop(Rid /*target*/, const RDropEvent* event, void* user) noexcept
{
    int accepted = R_FALSE;
    if (event != nullptr) {
        guarded("a drop", [user, event, &accepted] { accepted = panelOf(user).drop(*event); });
    }
    return accepted;
}

void HierarchyPanel::onRenameCommitted(Rid /*field*/, const char* /*text*/, void* user) noexcept
{
    guarded("renaming", [user] { panelOf(user).commitRename(true); });
}

int HierarchyPanel::onRenameEvent(Rid /*field*/, const REvent* event, void* user) noexcept
{
    bool used = false;
    if (event != nullptr) {
        guarded("renaming", [user, event, &used] {
            HierarchyPanel& panel = panelOf(user);
            if (event->type == R_EVENT_KEY_DOWN && event->key == R_KEY_ESCAPE) {
                panel.cancelRename(true);
                used = true;
            } else if (event->type == R_EVENT_FOCUS_LOST) {
                // A click elsewhere renames, as Enter does; the field still
                // takes the event.
                panel.commitRename(false);
            }
        });
    }
    return used ? R_TRUE : R_FALSE;
}

void HierarchyPanel::createEntity()
{
    if (document_ == nullptr) {
        return;
    }
    std::vector<ComponentValue> components{
        engine::scene::NameComponent{std::string(kNewEntityName)},
        engine::scene::TransformComponent{},
    };
    auto command = std::make_unique<CreateEntity>(std::move(components));
    // The document keeps the command: its result stays readable after execute.
    const CreateEntity& created = *command;
    if (auto error = document_->execute(std::move(command))) {
        report("create an entity", EntityId{}, *error);
        return;
    }
    document_->selection().select(created.entity());
}

void HierarchyPanel::deleteSelected()
{
    if (document_ == nullptr) {
        return;
    }
    const std::optional<EntityId> entity = document_->selection().primary();
    if (!entity) {
        return;
    }
    if (auto error = document_->execute(std::make_unique<DeleteEntity>(*entity))) {
        report("delete", *entity, *error);
    }
}

void HierarchyPanel::selectFromTree(Rid item)
{
    if (changingTree_ || document_ == nullptr) {
        return;
    }
    const EntityId entity = entityOf(item);
    if (entity.isNull()) {
        document_->selection().clear();
    } else {
        document_->selection().select(entity);
    }
    shownSelection_ = entity;
    selectionShown_ = true;
    refreshButtons();
}

bool HierarchyPanel::treeKey(const REvent& event)
{
    if (event.type != R_EVENT_KEY_DOWN || document_ == nullptr) {
        return false;
    }
    if (event.key == R_KEY_DELETE) {
        deleteSelected();
        return true;
    }
    if (event.key == R_KEY_F2) {
        if (const std::optional<EntityId> entity = document_->selection().primary()) {
            pendingRename_ = *entity;
        }
        return true;
    }
    return false;
}

int HierarchyPanel::drop(const RDropEvent& event)
{
    if (document_ == nullptr || event.drag_type == nullptr ||
        std::string_view(event.drag_type) != kEntityDragType || event.drag_source != tree_) {
        return R_FALSE;
    }
    // The data of a drag inside the application is known in every phase.
    const char* data = event.text != nullptr ? event.text : Rdrag_get_data(event.window);
    const EntityId child = entityOf(firstHandle(data));
    const Rid over = Rtree_view_get_item_at(tree_, event.local_x, event.local_y);
    const EntityId parent = entityOf(over);
    // A drop onto the dragged entity itself is refused at once (the pointer
    // shows it), rather than reported: a short drag ends there by accident.
    const bool refused = child.isNull() || child == parent;
    switch (event.phase) {
    case R_DROP_ENTER:
    case R_DROP_MOVE:
        if (refused) {
            Rtree_view_set_drop_highlight(tree_, R_INVALID_ID, R_FALSE);
            return R_FALSE;
        }
        // An entity is highlighted, or the whole tree for the top level.
        Rtree_view_set_drop_highlight(tree_, over, over == R_INVALID_ID ? R_TRUE : R_FALSE);
        return R_TRUE;
    case R_DROP_LEAVE:
        Rtree_view_set_drop_highlight(tree_, R_INVALID_ID, R_FALSE);
        return R_FALSE;
    case R_DROP_DROPPED:
        Rtree_view_set_drop_highlight(tree_, R_INVALID_ID, R_FALSE);
        if (refused) {
            return R_FALSE;
        }
        reparent(child, parent);
        return R_TRUE;
    }
    return R_FALSE;
}

void HierarchyPanel::reparent(EntityId child, EntityId parent)
{
    if (document_ == nullptr || !document_->scene().contains(child)) {
        return;
    }
    const auto link = document_->scene().component<engine::scene::ParentComponent>(child);
    const EntityId current = link ? link.value().entity : EntityId{};
    if (current == parent) {
        // Where it is already: no step.
        return;
    }
    std::optional<EntityId> target;
    if (!parent.isNull()) {
        target = parent;
    }
    if (auto error = document_->execute(std::make_unique<SetParent>(child, target))) {
        report("set the parent of", child, *error);
    }
}

void HierarchyPanel::beginRename(EntityId entity)
{
    if (renaming_) {
        commitRename(false);
    }
    if (document_ == nullptr || !document_->scene().contains(entity)) {
        return;
    }
    const Rid item = itemOf(entity);
    if (item == R_INVALID_ID) {
        return;
    }
    Rtree_view_scroll_to_item(tree_, item);
    const std::optional<RowRect> row = rowRect(entity);
    if (!row) {
        return;
    }
    // The field lies over the row, in the canvas that covers the tree.
    Rslot_set_position(renameField_, row->x, row->y);
    Rslot_set_size(renameField_, row->width, row->height);
    const auto name = document_->scene().component<engine::scene::NameComponent>(entity);
    Rtext_input_set_text(renameField_, name ? name.value().value.c_str() : "");
    Rwidget_set_visibility(renameField_, R_VISIBLE);
    renaming_ = entity;
    Rwidget_focus(renameField_);
    Rtext_input_select_all(renameField_);
}

void HierarchyPanel::commitRename(bool refocus)
{
    if (!renaming_) {
        return;
    }
    const EntityId entity = *renaming_;
    // Reset first: hiding the field takes its focus, which commits again.
    renaming_.reset();
    const char* typed = Rtext_input_get_text(renameField_);
    std::string text = typed != nullptr ? typed : "";
    hideRenameField(refocus);
    if (document_ == nullptr || !document_->scene().contains(entity)) {
        return;
    }
    const auto name = document_->scene().component<engine::scene::NameComponent>(entity);
    if (name ? name.value().value == text : text.empty()) {
        // Unchanged: no step.
        return;
    }
    if (auto error = document_->execute(std::make_unique<RenameEntity>(entity, std::move(text)))) {
        report("rename", entity, *error);
    }
}

void HierarchyPanel::cancelRename(bool refocus)
{
    if (!renaming_) {
        return;
    }
    renaming_.reset();
    hideRenameField(refocus);
}

void HierarchyPanel::hideRenameField(bool refocus)
{
    Rwidget_set_visibility(renameField_, R_COLLAPSED);
    if (refocus) {
        Rwidget_focus(tree_);
    }
}

void HierarchyPanel::follow()
{
    if (document_ == nullptr) {
        if (!shown_) {
            rebuild({});
            shown_ = true;
        }
        return;
    }
    if (shown_ && document_->version() == version_) {
        return;
    }
    std::vector<HierarchyRow> rows = hierarchyRows(document_->scene());
    if (shown_ && sameTree(rows_, rows)) {
        relabel(std::move(rows));
    } else {
        rebuild(std::move(rows));
    }
    version_ = document_->version();
    shown_ = true;
    if (renaming_ && !document_->scene().contains(*renaming_)) {
        cancelRename(false);
    }
}

void HierarchyPanel::rebuild(std::vector<HierarchyRow> rows)
{
    // The branches as the user left them.
    for (const auto& [entity, item] : items_) {
        if (Rtree_item_get_child_count(item) == 0) {
            continue;
        }
        if (Rtree_item_is_expanded(item) != R_FALSE) {
            collapsed_.erase(entity);
        } else {
            collapsed_.insert(entity);
        }
    }
    // The rows may move under the field.
    cancelRename(false);

    changingTree_ = true;
    Rtree_view_clear_items(tree_);
    items_.clear();
    entities_.clear();
    for (const HierarchyRow& row : rows) {
        // A parent's row comes before its children's (hierarchyRows).
        const Rid parent = row.parent.isNull() ? R_INVALID_ID : itemOf(row.parent);
        const Rid item = Rtree_view_add_item(tree_, parent, row.label.c_str());
        if (item == R_INVALID_ID) {
            continue; // Raven reported it through its error callback.
        }
        items_[row.entity] = item;
        entities_[item] = row.entity;
    }
    for (const auto& [entity, item] : items_) {
        Rtree_item_set_expanded(item, collapsed_.contains(entity) ? R_FALSE : R_TRUE);
    }
    changingTree_ = false;
    rows_ = std::move(rows);
    // The new items hold no selection yet.
    selectionShown_ = false;
    ++rebuilds_;
}

void HierarchyPanel::relabel(std::vector<HierarchyRow> rows)
{
    for (std::size_t i = 0; i < rows.size() && i < rows_.size(); ++i) {
        if (rows[i].label == rows_[i].label) {
            continue;
        }
        if (const Rid item = itemOf(rows[i].entity); item != R_INVALID_ID) {
            Rtree_item_set_text(item, rows[i].label.c_str());
        }
    }
    rows_ = std::move(rows);
    ++relabels_;
}

void HierarchyPanel::showSelection()
{
    EntityId primary;
    if (document_ != nullptr) {
        if (const std::optional<EntityId> selected = document_->selection().primary()) {
            primary = *selected;
        }
    }
    if (selectionShown_ && primary == shownSelection_) {
        return;
    }
    const Rid item = itemOf(primary);
    changingTree_ = true;
    Rtree_view_set_selected_item(tree_, item);
    changingTree_ = false;
    shownSelection_ = item != R_INVALID_ID ? primary : EntityId{};
    selectionShown_ = true;
}

void HierarchyPanel::refreshButtons()
{
    const int canCreate = document_ != nullptr ? R_TRUE : R_FALSE;
    const int canDelete =
        document_ != nullptr && !document_->selection().empty() ? R_TRUE : R_FALSE;
    if (createEnabled_ != canCreate) {
        Rwidget_set_enabled(create_, canCreate);
        createEnabled_ = canCreate;
    }
    if (deleteEnabled_ != canDelete) {
        Rwidget_set_enabled(delete_, canDelete);
        deleteEnabled_ = canDelete;
    }
}

void HierarchyPanel::report(std::string_view action, EntityId entity,
                            const engine::core::Error& error) const
{
    if (entity.isNull()) {
        engine::core::logWarning("editor", "hierarchy: cannot {}: {}", action, error.message);
        return;
    }
    const std::string label = document_ != nullptr && document_->scene().contains(entity)
                                  ? entityLabel(document_->scene(), entity)
                                  : entity.toString();
    engine::core::logWarning("editor", "hierarchy: cannot {} '{}': {}", action, label,
                             error.message);
}

} // namespace editor
