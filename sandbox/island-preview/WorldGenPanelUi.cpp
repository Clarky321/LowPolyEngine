#include "WorldGenPanelUi.hpp"

#include <imgui.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <type_traits>
#include <variant>

namespace island_preview {

namespace {

constexpr float kPanelWidth = 390.0F;
constexpr float kMargin = 16.0F;
const ImVec4 kErrorColor{1.0F, 0.45F, 0.4F, 1.0F};
const ImVec4 kInfoColor{0.7F, 0.9F, 0.75F, 1.0F};

// A text field bound to a std::string: the buffer grows as the text does.
int resizeString(ImGuiInputTextCallbackData* data)
{
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        auto* text = static_cast<std::string*>(data->UserData);
        text->resize(static_cast<std::size_t>(data->BufTextLen));
        data->Buf = text->data();
    }
    return 0;
}

bool inputString(const char* label, std::string& text)
{
    return ImGui::InputText(label, text.data(), text.capacity() + 1U,
                            ImGuiInputTextFlags_CallbackResize, resizeString, &text);
}

// One field: a drag box within its limits for numbers, a check box for the
// switch; the tooltip names the field's path and default.
void drawField(const PanelGroup& group, const engine::worldgen::SettingsField& field)
{
    const std::string label = fieldLabel(group, field);
    const auto range = field.max - field.min;
    std::visit(
        [&](auto* value) {
            using T = std::remove_pointer_t<decltype(value)>;
            if constexpr (std::is_same_v<T, bool>) {
                ImGui::Checkbox(label.c_str(), value);
            } else if constexpr (std::is_same_v<T, int>) {
                const float speed = static_cast<float>(std::max(0.05, range / 200.0));
                ImGui::DragInt(label.c_str(), value, speed, static_cast<int>(field.min),
                               static_cast<int>(field.max), "%d", ImGuiSliderFlags_AlwaysClamp);
            } else {
                const float speed = static_cast<float>(std::max(1.0e-4, range / 400.0));
                ImGui::DragFloat(label.c_str(), value, speed, static_cast<float>(field.min),
                                 static_cast<float>(field.max), "%.4g",
                                 ImGuiSliderFlags_AlwaysClamp);
            }
        },
        field.value);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%.*s  (%g to %g, default %g)", static_cast<int>(field.path.size()),
                          field.path.data(), field.min, field.max, field.defaultValue);
    }
}

} // namespace

std::optional<engine::worldgen::WorldGenSettings> drawWorldGenPanel(WorldGenPanel& panel)
{
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - kMargin, kMargin), ImGuiCond_FirstUseEver,
                            ImVec2(1.0F, 0.0F));
    ImGui::SetNextWindowSize(ImVec2(kPanelWidth, io.DisplaySize.y - 2.0F * kMargin),
                             ImGuiCond_FirstUseEver);
    std::optional<engine::worldgen::WorldGenSettings> generate;
    if (!ImGui::Begin("World generation  (F2)")) {
        ImGui::End();
        return generate;
    }
    ImGui::PushItemWidth(-170.0F);

    engine::worldgen::WorldGenSettings& draft = panel.draft();
    const std::uint64_t step = 1;
    ImGui::InputScalar("seed", ImGuiDataType_U64, &draft.seed, &step);

    const std::optional<std::string> problem = panel.problem();
    ImGui::BeginDisabled(problem.has_value());
    if (ImGui::Button("Regenerate")) {
        generate = panel.regenerate();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!panel.hasChanges());
    if (ImGui::Button("Revert")) {
        panel.revert();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Defaults")) {
        panel.resetToDefaults();
    }
    if (panel.hasChanges() && !problem) {
        ImGui::TextDisabled("changed; press Regenerate");
    }
    if (problem) {
        ImGui::PushStyleColor(ImGuiCol_Text, kErrorColor);
        ImGui::TextWrapped("%s", problem->c_str());
        ImGui::PopStyleColor();
    }
    if (const auto& message = panel.message(); !message.text.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text,
                              message.kind == PanelMessageKind::Error ? kErrorColor : kInfoColor);
        ImGui::TextWrapped("%s", message.text.c_str());
        ImGui::PopStyleColor();
    }

    ImGui::SeparatorText("Settings");
    for (const PanelGroup& group : panel.groups()) {
        const ImGuiTreeNodeFlags open =
            group.prefix == "islands" ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None;
        if (ImGui::CollapsingHeader(group.title.c_str(), open)) {
            ImGui::PushID(group.prefix.c_str());
            for (const auto& field : group.fields) {
                drawField(group, field);
            }
            ImGui::PopID();
        }
    }
    // Every value stays in its range, also when typed in (M3.3 rules).
    panel.clampDraft();

    ImGui::SeparatorText("Settings file");
    inputString("file", panel.filePath());
    if (ImGui::Button("Save")) {
        panel.save();
    }
    ImGui::SameLine();
    if (ImGui::Button("Load")) {
        panel.load();
    }

    ImGui::PopItemWidth();
    ImGui::End();
    return generate;
}

} // namespace island_preview
