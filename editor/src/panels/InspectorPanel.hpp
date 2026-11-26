#pragma once

// The inspector panel (M5.8): the document's primary selection with a
// section per component (editor/layouts/shell.xml), each shown only when the
// entity has the component:
//   - name (a text field: Enter or leaving the field renames, Escape
//     restores);
//   - transform: position, rotation as Euler angles in degrees
//     (EulerAngles.hpp), scale;
//   - camera: vertical field of view in degrees, near and far plane;
//   - directional light: direction, color, intensity;
//   - static mesh: the mesh and the material from the project's assets of
//     those types, "default" for no material;
//   - script: the script asset and whether it is enabled;
//   - the parent link and the prefab instance, shown only.
// A Remove button per editable section, and an Add list of the editable
// components the entity lacks.
//
// Every change is one command of the document (InspectorFields.hpp says what
// it replaces). A continuous edit is one undo step: the changes of one
// gesture on a spin box (a drag from press to release, a held key) carry one
// merge key; each press, Enter, wheel notch or arrow key starts another. A
// value the document refuses (the component's validate, the scene's rules)
// changes nothing: the field shows the stored value again and the message is
// shown under its section until the next accepted change of the component or
// another entity is shown.
//
// update(), once per frame outside Raven's frame, shows the selected entity
// and follows the document's version. The fields of the vector the user is
// editing (the group of the spin box with the keyboard) and a name being
// typed are not overwritten until the keyboard leaves them, so that a value
// is never changed under the user's hands (a rotation dragged through 90
// degrees would otherwise jump to its other angles).
//
// Raven holds a pointer to the panel while it is attached.

#include "editor/document/Command.hpp"
#include "editor/document/ComponentValue.hpp"
#include "editor/document/EditorDocument.hpp"
#include "editor/panels/InspectorFields.hpp"

#include "engine/core/Result.hpp"
#include "engine/scene/EntityId.hpp"

#include <Raven.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace editor {

class InspectorPanel {
public:
    // The number of spin boxes: three per vector, one per single value.
    static constexpr std::size_t kSpinCount = 19;

    InspectorPanel() = default;
    InspectorPanel(const InspectorPanel&) = delete;
    InspectorPanel& operator=(const InspectorPanel&) = delete;
    InspectorPanel(InspectorPanel&&) = delete;
    InspectorPanel& operator=(InspectorPanel&&) = delete;
    ~InspectorPanel() { detach(); }

    // Takes the inspector's widgets of `window` (the shell's layout), connects
    // them and shows `document`'s selection (null: no project, nothing shown).
    // Fails, attaching nothing, when a widget is missing.
    [[nodiscard]] std::optional<engine::core::Error> attach(Rid window, EditorDocument* document);
    void detach() noexcept;
    [[nodiscard]] bool attached() const noexcept { return empty_ != R_INVALID_ID; }

    // Shows the selected entity and follows the document's changes.
    void update();

    // The entity shown; empty when nothing is.
    [[nodiscard]] std::optional<engine::scene::EntityId> entity() const;
    // The message shown under a component's section, or under Add; "" for none.
    [[nodiscard]] const std::string& message(ComponentType type) const;
    [[nodiscard]] const std::string& addMessage() const noexcept { return addMessage_.text; }
    // How often the fields were filled from the document (for the checks).
    [[nodiscard]] std::uint64_t refreshCount() const noexcept { return refreshes_; }

private:
    struct Message {
        Rid label = R_INVALID_ID;
        std::string text;
    };
    struct Section {
        ComponentType type = ComponentType::Name;
        Rid section = R_INVALID_ID;
        Rid remove = R_INVALID_ID;
        Message message;
    };

    static void onSpinChanged(Rid spin, double value, void* user) noexcept;
    static int onSpinEvent(Rid spin, const REvent* event, void* user) noexcept;
    static void onNameCommitted(Rid field, const char* text, void* user) noexcept;
    static int onNameEvent(Rid field, const REvent* event, void* user) noexcept;
    static void onComboChanged(Rid combo, int index, void* user) noexcept;
    static void onEnabledChanged(Rid checkbox, int checked, void* user) noexcept;
    static void onRemove(Rid button, void* user) noexcept;
    static void onAdd(Rid button, void* user) noexcept;

    void spinChanged(Rid spin);
    void startGesture(Rid spin);
    void commitName();
    void restoreName();
    void comboChanged(Rid combo, int index);
    void enabledChanged(bool enabled);
    void remove(Rid button);
    void add();
    // Executes a change of the shown entity's component; a refusal is shown
    // under the component's section.
    void replace(ComponentValue value, MergeKey key);

    void refresh(bool newEntity);
    void showValues(const engine::scene::Scene& scene);
    void showGroup(FieldGroup group, const ComponentValue& component);
    void showAddList(const engine::scene::Scene& scene);
    void showMessages();
    void setMessage(ComponentType type, std::string text);
    [[nodiscard]] Section* sectionOf(ComponentType type);
    // The index in kFieldGroups of the group whose spin box has the keyboard,
    // or kNoGroup.
    [[nodiscard]] int focusedGroup() const;

    EditorDocument* document_ = nullptr;
    Rid empty_ = R_INVALID_ID;
    Rid entityBox_ = R_INVALID_ID;
    Rid id_ = R_INVALID_ID;
    Rid name_ = R_INVALID_ID;
    Rid mesh_ = R_INVALID_ID;
    Rid material_ = R_INVALID_ID;
    Rid script_ = R_INVALID_ID;
    Rid enabled_ = R_INVALID_ID;
    Rid parentSection_ = R_INVALID_ID;
    Rid parent_ = R_INVALID_ID;
    Rid prefabSection_ = R_INVALID_ID;
    Rid prefab_ = R_INVALID_ID;
    Rid addList_ = R_INVALID_ID;
    Rid addButton_ = R_INVALID_ID;
    Message addMessage_;
    std::array<Rid, kSpinCount> spins_{};
    std::array<MergeKey, kSpinCount> keys_{};
    std::array<Section, kEditableComponents.size()> sections_{};

    // What the asset lists offer, in their order; the material list starts
    // with "default" (the null ID).
    std::vector<AssetChoice> meshes_;
    std::vector<AssetChoice> materials_;
    std::vector<AssetChoice> scripts_;
    // The components the Add list offers, in its order.
    std::vector<ComponentType> addable_;

    // The entity shown (null: none), and what the fields were filled from.
    engine::scene::EntityId shown_;
    bool filled_ = false;
    std::uint64_t version_ = 0;
    static constexpr int kNoGroup = -1;
    int editing_ = kNoGroup;
    bool nameEditing_ = false;
    // The panel sets the widgets itself: their callbacks are not the user's.
    bool refreshing_ = false;
    // Merge keys of this panel's gestures; far from the small numbers other
    // panels might count with.
    MergeKey nextKey_ = MergeKey{1} << 40U;
    std::uint64_t refreshes_ = 0;
};

} // namespace editor
