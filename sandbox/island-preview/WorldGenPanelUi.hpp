#pragma once

// The WorldGen panel's Dear ImGui window (M3.12, ADR 0018): the seed, every
// field of the settings in collapsible groups, the problem that keeps the
// draft from being generated, Regenerate, Revert, Defaults, and a settings
// file to save and load. Call it inside a debug UI frame.

#include "WorldGenPanel.hpp"

#include "engine/worldgen/WorldGenSettings.hpp"

#include <optional>

namespace island_preview {

// Draws the panel and applies its edits to the panel's draft. Returns the
// settings to generate when Regenerate was pressed and the draft is valid.
[[nodiscard]] std::optional<engine::worldgen::WorldGenSettings>
drawWorldGenPanel(WorldGenPanel& panel);

} // namespace island_preview
