#include "InspectorPanel.hpp"

#include "editor/commands/ComponentCommands.hpp"
#include "editor/panels/HierarchyRows.hpp"

#include "engine/core/Log.hpp"
#include "engine/scene/NameComponent.hpp"
#include "engine/scene/ParentComponent.hpp"
#include "engine/scene/PrefabInstanceComponent.hpp"
#include "engine/scene/ScriptComponent.hpp"
#include "engine/scene/StaticMeshComponent.hpp"

#include <exception>
#include <memory>
#include <string_view>
#include <utility>
#include <variant>

namespace editor {

using engine::scene::EntityId;

namespace {

struct SpinSpec {
    const char* name;
    FieldGroup group;
    std::size_t axis;
};

// Every spin box: its widget, its group and its place in the group.
constexpr std::array<SpinSpec, InspectorPanel::kSpinCount> kSpins{{
    {"inspector-position-x", FieldGroup::Position, 0},
    {"inspector-position-y", FieldGroup::Position, 1},
    {"inspector-position-z", FieldGroup::Position, 2},
    {"inspector-rotation-x", FieldGroup::Rotation, 0},
    {"inspector-rotation-y", FieldGroup::Rotation, 1},
    {"inspector-rotation-z", FieldGroup::Rotation, 2},
    {"inspector-scale-x", FieldGroup::Scale, 0},
    {"inspector-scale-y", FieldGroup::Scale, 1},
    {"inspector-scale-z", FieldGroup::Scale, 2},
    {"inspector-fov", FieldGroup::VerticalFov, 0},
    {"inspector-near", FieldGroup::NearPlane, 0},
    {"inspector-far", FieldGroup::FarPlane, 0},
    {"inspector-direction-x", FieldGroup::Direction, 0},
    {"inspector-direction-y", FieldGroup::Direction, 1},
    {"inspector-direction-z", FieldGroup::Direction, 2},
    {"inspector-color-r", FieldGroup::Color, 0},
    {"inspector-color-g", FieldGroup::Color, 1},
    {"inspector-color-b", FieldGroup::Color, 2},
    {"inspector-intensity", FieldGroup::Intensity, 0},
}};

struct SectionSpec {
    ComponentType type;
    const char* section;
    const char* remove;
    const char* message;
};

// In the order of kEditableComponents.
constexpr std::array<SectionSpec, kEditableComponents.size()> kSections{{
    {ComponentType::Name, "inspector-section-name", "inspector-remove-name",
     "inspector-message-name"},
    {ComponentType::Transform, "inspector-section-transform", "inspector-remove-transform",
     "inspector-message-transform"},
    {ComponentType::Camera, "inspector-section-camera", "inspector-remove-camera",
     "inspector-message-camera"},
    {ComponentType::DirectionalLight, "inspector-section-light", "inspector-remove-light",
     "inspector-message-light"},
    {ComponentType::StaticMesh, "inspector-section-mesh", "inspector-remove-mesh",
     "inspector-message-mesh"},
    {ComponentType::Script, "inspector-section-script", "inspector-remove-script",
     "inspector-message-script"},
}};
static_assert(kSections[0].type == kEditableComponents[0]);
static_assert(kSections[5].type == kEditableComponents[5]);

// The color of a message: the console's error color.
constexpr std::array<float, 4> kMessageColor{0.95F, 0.42F, 0.38F, 1.0F};

// The name the Add list shows a component type with.
const char* displayName(ComponentType type) noexcept
{
    switch (type) {
    case ComponentType::Name:
        return "Name";
    case ComponentType::Transform:
        return "Transform";
    case ComponentType::Camera:
        return "Camera";
    case ComponentType::DirectionalLight:
        return "Directional light";
    case ComponentType::StaticMesh:
        return "Static mesh";
    case ComponentType::Script:
        return "Script";
    case ComponentType::Parent:
        return "Parent";
    case ComponentType::PrefabInstance:
        return "Prefab instance";
    }
    return "Component";
}

void setShown(Rid widget, bool shown)
{
    const RVisibility visibility = shown ? R_VISIBLE : R_COLLAPSED;
    if (Rwidget_get_visibility(widget) != visibility) {
        Rwidget_set_visibility(widget, visibility);
    }
}

// The index of an asset in a list, or -1.
int indexOf(const std::vector<AssetChoice>& choices, engine::assets::AssetId id)
{
    for (std::size_t i = 0; i < choices.size(); ++i) {
        if (choices[i].id == id) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void fillList(Rid combo, const std::vector<AssetChoice>& choices)
{
    Rcombo_box_clear_items(combo);
    for (const AssetChoice& choice : choices) {
        Rcombo_box_add_item(combo, choice.label.c_str());
    }
}

// Raven calls the panel from C: nothing may be thrown back into it (AGENTS.md
// §4). What fails is logged.
template <typename Action>
void guarded(std::string_view what, Action&& action) noexcept
{
    try {
        std::forward<Action>(action)();
    } catch (const std::exception& error) {
        engine::core::logError("editor", "inspector: {} failed: {}", what, error.what());
    } catch (...) {
        engine::core::logError("editor", "inspector: {} failed", what);
    }
}

InspectorPanel& panelOf(void* user) noexcept
{
    return *static_cast<InspectorPanel*>(user);
}

const std::string kNoMessage;

} // namespace

std::optional<engine::core::Error> InspectorPanel::attach(Rid window, EditorDocument* document)
{
    detach();
    std::optional<engine::core::Error> missing;
    const auto find = [window, &missing](const char* name) {
        const Rid found = Rwidget_find(window, name);
        if (found == R_INVALID_ID && !missing) {
            missing = engine::core::Error{std::string("the shell layout has no widget named '") +
                                          name + "'"};
        }
        return found;
    };
    const Rid empty = find("inspector-empty");
    const Rid entityBox = find("inspector-entity");
    const Rid id = find("inspector-id");
    const Rid name = find("inspector-name");
    const Rid mesh = find("inspector-mesh");
    const Rid material = find("inspector-material");
    const Rid script = find("inspector-script");
    const Rid enabled = find("inspector-script-enabled");
    const Rid parentSection = find("inspector-section-parent");
    const Rid parent = find("inspector-parent");
    const Rid prefabSection = find("inspector-section-prefab");
    const Rid prefab = find("inspector-prefab");
    const Rid addList = find("inspector-add");
    const Rid addButton = find("inspector-add-button");
    const Rid addMessageLabel = find("inspector-message-add");
    std::array<Rid, kSpinCount> spins{};
    for (std::size_t i = 0; i < kSpins.size(); ++i) {
        spins[i] = find(kSpins[i].name);
    }
    std::array<Section, kEditableComponents.size()> sections{};
    for (std::size_t i = 0; i < kSections.size(); ++i) {
        sections[i].type = kSections[i].type;
        sections[i].section = find(kSections[i].section);
        sections[i].remove = find(kSections[i].remove);
        sections[i].message.label = find(kSections[i].message);
    }
    if (missing) {
        return missing;
    }

    document_ = document;
    empty_ = empty;
    entityBox_ = entityBox;
    id_ = id;
    name_ = name;
    mesh_ = mesh;
    material_ = material;
    script_ = script;
    enabled_ = enabled;
    parentSection_ = parentSection;
    parent_ = parent;
    prefabSection_ = prefabSection;
    prefab_ = prefab;
    addList_ = addList;
    addButton_ = addButton;
    addMessage_ = Message{addMessageLabel, {}};
    spins_ = spins;
    sections_ = sections;
    keys_.fill(kNoMerge);

    for (const Rid spin : spins_) {
        Rspin_box_set_changed_callback(spin, &InspectorPanel::onSpinChanged, this);
        Rwidget_set_event_callback(spin, &InspectorPanel::onSpinEvent, this);
    }
    Rtext_input_set_committed_callback(name_, &InspectorPanel::onNameCommitted, this);
    Rwidget_set_event_callback(name_, &InspectorPanel::onNameEvent, this);
    for (const Rid combo : {mesh_, material_, script_}) {
        Rcombo_box_set_changed_callback(combo, &InspectorPanel::onComboChanged, this);
    }
    Rcheckbox_set_changed_callback(enabled_, &InspectorPanel::onEnabledChanged, this);
    for (const Section& section : sections_) {
        Rbutton_set_clicked_callback(section.remove, &InspectorPanel::onRemove, this);
        Rlabel_set_color(section.message.label, kMessageColor[0], kMessageColor[1],
                         kMessageColor[2], kMessageColor[3]);
    }
    Rbutton_set_clicked_callback(addButton_, &InspectorPanel::onAdd, this);
    Rlabel_set_color(addMessage_.label, kMessageColor[0], kMessageColor[1], kMessageColor[2],
                     kMessageColor[3]);

    // The asset lists: the registry does not change while the document is open
    // (asset import is M6).
    meshes_.clear();
    materials_.clear();
    scripts_.clear();
    if (document_ != nullptr) {
        using engine::assets::AssetType;
        meshes_ = assetChoices(document_->assets(), AssetType::Mesh);
        materials_.push_back({engine::assets::AssetId{}, "default"});
        for (AssetChoice& choice : assetChoices(document_->assets(), AssetType::Material)) {
            materials_.push_back(std::move(choice));
        }
        scripts_ = assetChoices(document_->assets(), AssetType::Script);
    }
    refreshing_ = true;
    fillList(mesh_, meshes_);
    fillList(material_, materials_);
    fillList(script_, scripts_);
    refreshing_ = false;

    addable_.clear();
    shown_ = EntityId{};
    filled_ = false;
    version_ = 0;
    editing_ = kNoGroup;
    nameEditing_ = false;
    update();
    return std::nullopt;
}

void InspectorPanel::detach() noexcept
{
    if (empty_ == R_INVALID_ID) {
        return;
    }
    // After Rshutdown the widgets are gone, and Raven holds no callback.
    if (Ris_initialized() != R_FALSE) {
        const auto valid = [](Rid widget) { return Rwidget_is_valid(widget) != R_FALSE; };
        for (const Rid spin : spins_) {
            if (valid(spin)) {
                Rspin_box_set_changed_callback(spin, nullptr, nullptr);
                Rwidget_set_event_callback(spin, nullptr, nullptr);
            }
        }
        if (valid(name_)) {
            Rtext_input_set_committed_callback(name_, nullptr, nullptr);
            Rwidget_set_event_callback(name_, nullptr, nullptr);
        }
        for (const Rid combo : {mesh_, material_, script_}) {
            if (valid(combo)) {
                Rcombo_box_set_changed_callback(combo, nullptr, nullptr);
            }
        }
        if (valid(enabled_)) {
            Rcheckbox_set_changed_callback(enabled_, nullptr, nullptr);
        }
        for (const Section& section : sections_) {
            if (valid(section.remove)) {
                Rbutton_set_clicked_callback(section.remove, nullptr, nullptr);
            }
        }
        if (valid(addButton_)) {
            Rbutton_set_clicked_callback(addButton_, nullptr, nullptr);
        }
    }
    empty_ = R_INVALID_ID;
    document_ = nullptr;
}

void InspectorPanel::update()
{
    if (!attached()) {
        return;
    }
    EntityId selected;
    if (document_ != nullptr) {
        if (const std::optional<EntityId> primary = document_->selection().primary()) {
            selected = *primary;
        }
    }
    const bool newEntity = !filled_ || selected != shown_;
    const int editing = focusedGroup();
    const bool nameEditing = Rwidget_is_focused(name_) != R_FALSE;
    const bool changed = document_ != nullptr && document_->version() != version_;
    if (newEntity || changed || editing != editing_ || nameEditing != nameEditing_) {
        shown_ = selected;
        editing_ = editing;
        nameEditing_ = nameEditing;
        refresh(newEntity);
    }
}

std::optional<EntityId> InspectorPanel::entity() const
{
    if (!attached() || shown_.isNull() || document_ == nullptr ||
        !document_->scene().contains(shown_)) {
        return std::nullopt;
    }
    return shown_;
}

const std::string& InspectorPanel::message(ComponentType type) const
{
    for (const Section& section : sections_) {
        if (section.type == type) {
            return section.message.text;
        }
    }
    return kNoMessage;
}

void InspectorPanel::onSpinChanged(Rid spin, double /*value*/, void* user) noexcept
{
    guarded("editing a value", [user, spin] { panelOf(user).spinChanged(spin); });
}

int InspectorPanel::onSpinEvent(Rid spin, const REvent* event, void* user) noexcept
{
    if (event == nullptr) {
        return R_FALSE;
    }
    // A press, a key, a wheel notch or the keyboard arriving starts a gesture:
    // its changes are one undo step. The spin box handles the event itself.
    const bool starts = event->type == R_EVENT_MOUSE_DOWN || event->type == R_EVENT_MOUSE_WHEEL ||
                        event->type == R_EVENT_FOCUS_GAINED ||
                        (event->type == R_EVENT_KEY_DOWN && event->repeat == R_FALSE);
    if (starts) {
        panelOf(user).startGesture(spin);
    }
    return R_FALSE;
}

void InspectorPanel::onNameCommitted(Rid /*field*/, const char* /*text*/, void* user) noexcept
{
    guarded("renaming", [user] { panelOf(user).commitName(); });
}

int InspectorPanel::onNameEvent(Rid /*field*/, const REvent* event, void* user) noexcept
{
    bool used = false;
    if (event != nullptr) {
        guarded("renaming", [user, event, &used] {
            if (event->type == R_EVENT_KEY_DOWN && event->key == R_KEY_ESCAPE) {
                panelOf(user).restoreName();
                used = true;
            } else if (event->type == R_EVENT_FOCUS_LOST) {
                panelOf(user).commitName();
            }
        });
    }
    return used ? R_TRUE : R_FALSE;
}

void InspectorPanel::onComboChanged(Rid combo, int index, void* user) noexcept
{
    guarded("choosing an asset",
            [user, combo, index] { panelOf(user).comboChanged(combo, index); });
}

void InspectorPanel::onEnabledChanged(Rid /*checkbox*/, int checked, void* user) noexcept
{
    guarded("enabling a script",
            [user, checked] { panelOf(user).enabledChanged(checked != R_FALSE); });
}

void InspectorPanel::onRemove(Rid button, void* user) noexcept
{
    guarded("removing a component", [user, button] { panelOf(user).remove(button); });
}

void InspectorPanel::onAdd(Rid /*button*/, void* user) noexcept
{
    guarded("adding a component", [user] { panelOf(user).add(); });
}

void InspectorPanel::startGesture(Rid spin)
{
    for (std::size_t i = 0; i < spins_.size(); ++i) {
        if (spins_[i] == spin) {
            keys_[i] = nextKey_++;
        }
    }
}

void InspectorPanel::spinChanged(Rid spin)
{
    if (refreshing_ || document_ == nullptr || !document_->scene().contains(shown_)) {
        return;
    }
    std::size_t index = kSpins.size();
    for (std::size_t i = 0; i < spins_.size(); ++i) {
        if (spins_[i] == spin) {
            index = i;
        }
    }
    if (index == kSpins.size()) {
        return;
    }
    const FieldGroup group = kSpins[index].group;
    auto stored = componentOf(document_->scene(), shown_, componentTypeOf(group));
    if (!stored) {
        return;
    }
    // The group's fields as they are now.
    FieldValues values{0.0, 0.0, 0.0};
    for (std::size_t i = 0; i < kSpins.size(); ++i) {
        if (kSpins[i].group == group) {
            values[kSpins[i].axis] = Rspin_box_get_value(spins_[i]);
        }
    }
    ComponentValue edited = withFieldValues(stored.value(), group, values);
    if (edited == stored.value()) {
        return;
    }
    // A key of kNoMerge would merge nothing: a change without a gesture is a
    // step of its own.
    replace(std::move(edited), keys_[index]);
}

void InspectorPanel::commitName()
{
    if (refreshing_ || document_ == nullptr || !document_->scene().contains(shown_)) {
        return;
    }
    const auto stored = document_->scene().component<engine::scene::NameComponent>(shown_);
    if (!stored) {
        return;
    }
    const char* typed = Rtext_input_get_text(name_);
    std::string text = typed != nullptr ? typed : "";
    if (text == stored.value().value) {
        return;
    }
    replace(ComponentValue{engine::scene::NameComponent{std::move(text)}}, kNoMerge);
}

void InspectorPanel::restoreName()
{
    if (document_ == nullptr) {
        return;
    }
    const auto stored = document_->scene().component<engine::scene::NameComponent>(shown_);
    refreshing_ = true;
    Rtext_input_set_text(name_, stored ? stored.value().value.c_str() : "");
    Rtext_input_select_all(name_);
    refreshing_ = false;
}

void InspectorPanel::comboChanged(Rid combo, int index)
{
    if (refreshing_ || document_ == nullptr || index < 0 || !document_->scene().contains(shown_)) {
        return;
    }
    const auto chosen = static_cast<std::size_t>(index);
    if (combo == mesh_ || combo == material_) {
        auto stored = document_->scene().component<engine::scene::StaticMeshComponent>(shown_);
        if (!stored) {
            return;
        }
        engine::scene::StaticMeshComponent value = stored.value();
        if (combo == mesh_ && chosen < meshes_.size()) {
            value.mesh = meshes_[chosen].id;
        } else if (combo == material_ && chosen < materials_.size()) {
            value.material = materials_[chosen].id;
        }
        if (value != stored.value()) {
            replace(ComponentValue{value}, kNoMerge);
        }
    } else if (combo == script_ && chosen < scripts_.size()) {
        auto stored = document_->scene().component<engine::scene::ScriptComponent>(shown_);
        if (stored && stored.value().asset != scripts_[chosen].id) {
            engine::scene::ScriptComponent value = stored.value();
            value.asset = scripts_[chosen].id;
            replace(ComponentValue{value}, kNoMerge);
        }
    }
}

void InspectorPanel::enabledChanged(bool enabled)
{
    if (refreshing_ || document_ == nullptr || !document_->scene().contains(shown_)) {
        return;
    }
    auto stored = document_->scene().component<engine::scene::ScriptComponent>(shown_);
    if (stored && stored.value().enabled != enabled) {
        engine::scene::ScriptComponent value = stored.value();
        value.enabled = enabled;
        replace(ComponentValue{value}, kNoMerge);
    }
}

void InspectorPanel::remove(Rid button)
{
    if (document_ == nullptr || !document_->scene().contains(shown_)) {
        return;
    }
    for (const Section& section : sections_) {
        if (section.remove != button) {
            continue;
        }
        if (auto error =
                document_->execute(std::make_unique<RemoveComponent>(shown_, section.type))) {
            setMessage(section.type, error->message);
        } else {
            setMessage(section.type, {});
        }
        return;
    }
}

void InspectorPanel::add()
{
    if (document_ == nullptr || !document_->scene().contains(shown_)) {
        return;
    }
    const int index = Rcombo_box_get_selected(addList_);
    if (index < 0 || static_cast<std::size_t>(index) >= addable_.size()) {
        return;
    }
    const ComponentType type = addable_[static_cast<std::size_t>(index)];
    auto value = newComponent(type, document_->assets());
    if (!value) {
        addMessage_.text = value.error().message;
        showMessages();
        return;
    }
    if (auto error =
            document_->execute(std::make_unique<AddComponent>(shown_, std::move(value).value()))) {
        addMessage_.text = error->message;
    } else {
        addMessage_.text.clear();
        setMessage(type, {});
    }
    showMessages();
}

void InspectorPanel::replace(ComponentValue value, MergeKey key)
{
    const ComponentType type = typeOf(value);
    if (auto error =
            document_->execute(std::make_unique<ReplaceComponent>(shown_, std::move(value)), key)) {
        setMessage(type, error->message);
        // The fields show what is stored again.
        if (auto stored = componentOf(document_->scene(), shown_, type)) {
            refreshing_ = true;
            for (const FieldGroup group : kFieldGroups) {
                if (componentTypeOf(group) == type) {
                    showGroup(group, stored.value());
                }
            }
            refreshing_ = false;
        }
        if (type == ComponentType::Name) {
            restoreName();
        }
        return;
    }
    setMessage(type, {});
}

void InspectorPanel::refresh(bool newEntity)
{
    refreshing_ = true;
    if (newEntity) {
        for (Section& section : sections_) {
            section.message.text.clear();
        }
        addMessage_.text.clear();
    }
    const bool show =
        document_ != nullptr && !shown_.isNull() && document_->scene().contains(shown_);
    setShown(empty_, !show);
    setShown(entityBox_, show);
    if (show) {
        showValues(document_->scene());
    }
    showMessages();
    refreshing_ = false;
    version_ = document_ != nullptr ? document_->version() : 0;
    filled_ = true;
    ++refreshes_;
}

void InspectorPanel::showValues(const engine::scene::Scene& scene)
{
    const std::string id = "Entity " + shown_.toString();
    Rlabel_set_text(id_, id.c_str());
    for (const Section& section : sections_) {
        setShown(section.section, hasComponent(scene, shown_, section.type));
    }

    if (!nameEditing_) {
        const auto name = scene.component<engine::scene::NameComponent>(shown_);
        Rtext_input_set_text(name_, name ? name.value().value.c_str() : "");
    }
    for (std::size_t i = 0; i < kFieldGroups.size(); ++i) {
        const FieldGroup group = kFieldGroups[i];
        if (editing_ == static_cast<int>(i)) {
            continue;
        }
        if (auto component = componentOf(scene, shown_, componentTypeOf(group))) {
            showGroup(group, component.value());
        }
    }
    if (const auto mesh = scene.component<engine::scene::StaticMeshComponent>(shown_)) {
        Rcombo_box_set_selected(mesh_, indexOf(meshes_, mesh.value().mesh));
        Rcombo_box_set_selected(material_, indexOf(materials_, mesh.value().material));
    }
    if (const auto script = scene.component<engine::scene::ScriptComponent>(shown_)) {
        Rcombo_box_set_selected(script_, indexOf(scripts_, script.value().asset));
        Rcheckbox_set_checked(enabled_, script.value().enabled ? R_TRUE : R_FALSE);
    }

    const auto link = scene.component<engine::scene::ParentComponent>(shown_);
    setShown(parentSection_, link.hasValue());
    if (link) {
        const std::string text = "Parent: " + entityLabel(scene, link.value().entity);
        Rlabel_set_text(parent_, text.c_str());
    }
    const auto instance = scene.component<engine::scene::PrefabInstanceComponent>(shown_);
    setShown(prefabSection_, instance.hasValue());
    if (instance) {
        const auto asset = document_->assets().find(instance.value().prefab);
        const std::string prefab =
            asset ? asset.value().path.text() : instance.value().prefab.toString();
        const std::string text = "Prefab instance of " + prefab + ", entity " +
                                 std::to_string(instance.value().localId) +
                                 " (edited with prefabs in M6)";
        Rlabel_set_text(prefab_, text.c_str());
    }
    showAddList(scene);
}

void InspectorPanel::showGroup(FieldGroup group, const ComponentValue& component)
{
    const FieldValues values = fieldValues(component, group);
    for (std::size_t i = 0; i < kSpins.size(); ++i) {
        if (kSpins[i].group == group) {
            Rspin_box_set_value(spins_[i], values[kSpins[i].axis]);
        }
    }
}

void InspectorPanel::showAddList(const engine::scene::Scene& scene)
{
    std::vector<ComponentType> addable = addableComponents(scene, shown_);
    if (addable != addable_) {
        Rcombo_box_clear_items(addList_);
        for (const ComponentType type : addable) {
            Rcombo_box_add_item(addList_, displayName(type));
        }
        Rcombo_box_set_selected(addList_, addable.empty() ? -1 : 0);
        addable_ = std::move(addable);
    }
    Rwidget_set_enabled(addButton_, addable_.empty() ? R_FALSE : R_TRUE);
    Rwidget_set_enabled(addList_, addable_.empty() ? R_FALSE : R_TRUE);
}

void InspectorPanel::showMessages()
{
    for (const Section& section : sections_) {
        Rlabel_set_text(section.message.label, section.message.text.c_str());
        setShown(section.message.label, !section.message.text.empty());
    }
    Rlabel_set_text(addMessage_.label, addMessage_.text.c_str());
    setShown(addMessage_.label, !addMessage_.text.empty());
}

void InspectorPanel::setMessage(ComponentType type, std::string text)
{
    if (Section* section = sectionOf(type)) {
        if (section->message.text == text) {
            return;
        }
        section->message.text = std::move(text);
        showMessages();
    }
}

InspectorPanel::Section* InspectorPanel::sectionOf(ComponentType type)
{
    for (Section& section : sections_) {
        if (section.type == type) {
            return &section;
        }
    }
    return nullptr;
}

int InspectorPanel::focusedGroup() const
{
    for (std::size_t i = 0; i < spins_.size(); ++i) {
        if (Rwidget_is_focused(spins_[i]) == R_FALSE) {
            continue;
        }
        for (std::size_t group = 0; group < kFieldGroups.size(); ++group) {
            if (kFieldGroups[group] == kSpins[i].group) {
                return static_cast<int>(group);
            }
        }
    }
    return kNoGroup;
}

} // namespace editor
