#include "WorldGenPanel.hpp"

#include "engine/core/FileSystem.hpp"
#include "engine/worldgen/SettingsFile.hpp"

#include <array>
#include <cstdint>
#include <format>
#include <string_view>
#include <utility>

namespace island_preview {

namespace {

struct GroupTitle {
    std::string_view prefix;
    std::string_view title;
};

// Headers for the first parts of the settings paths, in the order of the
// settings.
constexpr std::array<GroupTitle, 9> kGroupTitles{{
    {"bounds", "World bounds"},
    {"islands", "Islands"},
    {"shape", "Island shape"},
    {"elevation", "Top surface"},
    {"underside", "Underside"},
    {"noise", "Relief noise"},
    {"water", "Local water"},
    {"clouds", "Clouds"},
    {"mesh", "Mesh"},
}};

std::string titleOf(std::string_view prefix)
{
    for (const auto& entry : kGroupTitles) {
        if (entry.prefix == prefix) {
            return std::string{entry.title};
        }
    }
    return std::string{prefix};
}

} // namespace

std::string fieldLabel(const PanelGroup& group, const engine::worldgen::SettingsField& field)
{
    const std::string_view path = field.path;
    if (path.size() > group.prefix.size() + 1U && path.starts_with(group.prefix) &&
        path[group.prefix.size()] == '.') {
        return std::string{path.substr(group.prefix.size() + 1U)};
    }
    return std::string{path};
}

WorldGenPanel::WorldGenPanel(const engine::worldgen::WorldGenSettings& applied)
    : draft_(applied), applied_(applied)
{
    for (const auto& field : engine::worldgen::settingsFields(draft_)) {
        const std::string_view path = field.path;
        const std::string prefix{path.substr(0, path.find('.'))};
        if (groups_.empty() || groups_.back().prefix != prefix) {
            groups_.push_back(PanelGroup{.title = titleOf(prefix), .prefix = prefix, .fields = {}});
        }
        groups_.back().fields.push_back(field);
    }
}

void WorldGenPanel::clampDraft()
{
    draft_ = engine::worldgen::clampToLimits(draft_);
}

std::optional<std::string> WorldGenPanel::problem() const
{
    if (auto error = engine::worldgen::validate(draft_)) {
        return std::move(error->message);
    }
    return std::nullopt;
}

std::optional<engine::worldgen::WorldGenSettings> WorldGenPanel::regenerate()
{
    if (auto reason = problem()) {
        setMessage(PanelMessageKind::Error, "not generated: " + *reason);
        return std::nullopt;
    }
    applied_ = draft_;
    return applied_;
}

void WorldGenPanel::revert()
{
    draft_ = applied_;
    setMessage(PanelMessageKind::Info, "back to the settings of the shown world");
}

void WorldGenPanel::resetToDefaults()
{
    const std::uint64_t seed = draft_.seed;
    draft_ = engine::worldgen::WorldGenSettings{};
    draft_.seed = seed;
    setMessage(PanelMessageKind::Info, "default settings (seed kept); press Regenerate");
}

void WorldGenPanel::save()
{
    if (auto error = engine::worldgen::saveSettings(filePath_, draft_)) {
        setMessage(PanelMessageKind::Error, error->message);
        return;
    }
    setMessage(PanelMessageKind::Info, std::format("saved to '{}'", filePath_));
}

void WorldGenPanel::load()
{
    auto loaded = engine::worldgen::loadSettings(filePath_);
    if (!loaded) {
        setMessage(PanelMessageKind::Error, loaded.error().message);
        return;
    }
    draft_ = loaded.value().settings;
    std::string text = std::format("loaded '{}'; press Regenerate", filePath_);
    for (const auto& warning : loaded.value().warnings) {
        text += "; " + warning;
    }
    setMessage(PanelMessageKind::Info, std::move(text));
}

void WorldGenPanel::setMessage(PanelMessageKind kind, std::string text)
{
    message_ = PanelMessage{.kind = kind, .text = std::move(text)};
}

} // namespace island_preview
