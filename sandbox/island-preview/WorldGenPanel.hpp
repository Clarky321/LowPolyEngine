#pragma once

// The state of island-preview's WorldGen panel without the UI (M3.12): the
// settings being edited (the draft), the settings the shown world was
// generated with, the fields grouped for display, and the actions Regenerate,
// Revert, Defaults, Save and Load. Values are kept in their ranges with the
// rules of the settings (clampToLimits), and the draft is generated only if it
// passes validate(). The ImGui drawing is in WorldGenPanelUi.cpp; this part is
// tested headless.

#include "engine/worldgen/WorldGenSettings.hpp"

#include <optional>
#include <string>
#include <vector>

namespace island_preview {

// The fields of one settings group, e.g. "islands", pointing into the draft.
struct PanelGroup {
    // Shown as the group's header, e.g. "Islands".
    std::string title;
    // The first part of the fields' paths, e.g. "islands".
    std::string prefix;
    std::vector<engine::worldgen::SettingsField> fields;
};

// The field's name inside its group: its path without the group's prefix,
// e.g. "altitude.min" for "islands.altitude.min".
[[nodiscard]] std::string fieldLabel(const PanelGroup& group,
                                     const engine::worldgen::SettingsField& field);

enum class PanelMessageKind { Info, Error };

struct PanelMessage {
    PanelMessageKind kind = PanelMessageKind::Info;
    std::string text;
};

class WorldGenPanel {
public:
    // Starts with the settings the shown world was generated with.
    explicit WorldGenPanel(const engine::worldgen::WorldGenSettings& applied);

    // The groups point into the panel's own draft: it cannot be copied or moved.
    WorldGenPanel(const WorldGenPanel&) = delete;
    WorldGenPanel& operator=(const WorldGenPanel&) = delete;
    WorldGenPanel(WorldGenPanel&&) = delete;
    WorldGenPanel& operator=(WorldGenPanel&&) = delete;
    ~WorldGenPanel() = default;

    [[nodiscard]] engine::worldgen::WorldGenSettings& draft() noexcept { return draft_; }
    [[nodiscard]] const engine::worldgen::WorldGenSettings& applied() const noexcept
    {
        return applied_;
    }
    // In the order of the settings; every field once.
    [[nodiscard]] const std::vector<PanelGroup>& groups() const noexcept { return groups_; }

    // Moves every field of the draft back into its range (after an edit).
    void clampDraft();

    // What validate() finds wrong with the draft (a combination of fields,
    // since single fields stay in range), or nothing.
    [[nodiscard]] std::optional<std::string> problem() const;

    // Whether the draft differs from the settings of the shown world.
    [[nodiscard]] bool hasChanges() const noexcept { return !(draft_ == applied_); }

    // Regenerate: the draft, if it passes validate(); it becomes the applied
    // settings. Otherwise nothing, and the message says why.
    [[nodiscard]] std::optional<engine::worldgen::WorldGenSettings> regenerate();

    // The draft back to the settings of the shown world.
    void revert();

    // The draft back to the default settings, keeping its seed.
    void resetToDefaults();

    // Writes the draft to filePath() (settings that do not pass validate() are
    // not written), or reads filePath() into the draft without generating it.
    // The message reports the result.
    void save();
    void load();

    [[nodiscard]] std::string& filePath() noexcept { return filePath_; }

    [[nodiscard]] const PanelMessage& message() const noexcept { return message_; }
    void setMessage(PanelMessageKind kind, std::string text);

private:
    engine::worldgen::WorldGenSettings draft_;
    engine::worldgen::WorldGenSettings applied_;
    std::vector<PanelGroup> groups_;
    std::string filePath_ = "worlds/world.json";
    PanelMessage message_;
};

} // namespace island_preview
