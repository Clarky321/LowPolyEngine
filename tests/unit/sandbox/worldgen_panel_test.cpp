// The WorldGen panel's state and actions without the UI (M3.12).
#include "WorldGenPanel.hpp"

#include "engine/core/FileSystem.hpp"
#include "engine/worldgen/SettingsFile.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

using engine::worldgen::WorldGenSettings;
using island_preview::fieldLabel;
using island_preview::PanelGroup;
using island_preview::PanelMessageKind;
using island_preview::WorldGenPanel;

namespace {

// The value of the panel's field with this path.
template <typename T>
T& valueOf(const WorldGenPanel& panel, std::string_view path)
{
    const engine::worldgen::SettingsField* found = nullptr;
    for (const auto& group : panel.groups()) {
        for (const auto& field : group.fields) {
            if (field.path == path) {
                found = &field;
            }
        }
    }
    INFO(path);
    REQUIRE(found != nullptr);
    return *std::get<T*>(found->value);
}

std::filesystem::path scratchFolder()
{
    const auto folder = std::filesystem::temp_directory_path() / "lpe-worldgen-panel-test";
    std::filesystem::create_directories(folder);
    return folder;
}

} // namespace

TEST_CASE("The WorldGen panel groups every settings field once", "[sandbox][panel]")
{
    const WorldGenPanel panel{WorldGenSettings{}};
    std::vector<std::string> titles;
    std::size_t fields = 0;
    for (const PanelGroup& group : panel.groups()) {
        titles.push_back(group.title);
        for (const auto& field : group.fields) {
            CHECK(std::string_view{field.path}.starts_with(group.prefix + "."));
            ++fields;
        }
    }
    CHECK(titles == std::vector<std::string>{"World bounds", "Islands", "Island shape",
                                             "Top surface", "Underside", "Relief noise",
                                             "Local water", "Clouds", "Mesh"});
    CHECK(fields == 45U);

    const PanelGroup& islands = panel.groups()[1];
    CHECK(fieldLabel(islands, islands.fields[1]) == "altitude.min");
    CHECK(fieldLabel(panel.groups()[0], panel.groups()[0].fields[0]) == "min.x");
    CHECK(fieldLabel(panel.groups()[5], panel.groups()[5].fields[1]) == "fractal.octaves");
    CHECK(fieldLabel(panel.groups()[6], panel.groups()[6].fields[0]) == "enabled");
    // A field of another group keeps its whole path.
    CHECK(fieldLabel(islands, panel.groups()[0].fields[0]) == "bounds.min.x");
}

TEST_CASE("The WorldGen panel edits its draft and keeps values in range", "[sandbox][panel]")
{
    WorldGenPanel panel{WorldGenSettings{}};
    CHECK_FALSE(panel.hasChanges());

    // The fields point into the draft.
    valueOf<int>(panel, "islands.count") = 7;
    CHECK(panel.draft().islands.count == 7);
    CHECK(panel.hasChanges());

    // Out of range or NaN: clamped like the settings rules do.
    valueOf<int>(panel, "islands.count") = 500;
    valueOf<float>(panel, "noise.frequency") = std::numeric_limits<float>::quiet_NaN();
    panel.clampDraft();
    CHECK(panel.draft().islands.count == 64);
    CHECK(panel.draft().noise.frequency == WorldGenSettings{}.noise.frequency);
    CHECK_FALSE(panel.problem().has_value());
}

TEST_CASE("The WorldGen panel regenerates only valid settings", "[sandbox][panel]")
{
    WorldGenSettings start;
    start.seed = 9U;
    WorldGenPanel panel{start};

    // A combination the rules reject: the panel says why and keeps the world.
    panel.draft().islands.radius = {50.0F, 20.0F};
    const auto problem = panel.problem();
    REQUIRE(problem.has_value());
    CHECK(problem->starts_with("islands.radius"));
    CHECK_FALSE(panel.regenerate().has_value());
    CHECK(panel.message().kind == PanelMessageKind::Error);
    CHECK(panel.message().text.starts_with("not generated: islands.radius"));
    CHECK(panel.applied() == start);

    // Revert brings back the shown world's settings.
    panel.revert();
    CHECK(panel.draft() == start);
    CHECK_FALSE(panel.hasChanges());

    // A valid change is generated and becomes the applied settings.
    panel.draft().seed = 10U;
    panel.draft().clouds.density = 0.9F;
    const auto generated = panel.regenerate();
    REQUIRE(generated.has_value());
    CHECK(generated->seed == 10U);
    CHECK(generated->clouds.density == 0.9F);
    CHECK(panel.applied() == *generated);
    CHECK_FALSE(panel.hasChanges());

    // Defaults keep the seed.
    panel.draft().islands.count = 3;
    panel.resetToDefaults();
    WorldGenSettings expected;
    expected.seed = 10U;
    CHECK(panel.draft() == expected);
}

TEST_CASE("The WorldGen panel saves and loads settings files", "[sandbox][panel]")
{
    const auto folder = scratchFolder();
    WorldGenSettings start;
    start.seed = 0xABCDEFULL;
    start.water.lakeChance = 0.75F;
    WorldGenPanel panel{start};

    panel.filePath() = engine::core::toUtf8(folder / "panel-world.json");
    panel.save();
    CHECK(panel.message().kind == PanelMessageKind::Info);
    CHECK(panel.message().text.starts_with("saved to '"));
    const auto saved = engine::worldgen::loadSettings(panel.filePath());
    REQUIRE(saved.hasValue());
    CHECK(saved.value().settings == start);

    // Loading replaces the draft, not the shown world.
    panel.draft().islands.count = 2;
    panel.load();
    CHECK(panel.message().kind == PanelMessageKind::Info);
    CHECK(panel.draft() == start);
    CHECK(panel.applied() == start);

    // A missing file is reported and changes nothing.
    panel.draft().islands.count = 5;
    panel.filePath() = engine::core::toUtf8(folder / "missing.json");
    panel.load();
    CHECK(panel.message().kind == PanelMessageKind::Error);
    CHECK(panel.message().text.find("missing.json") != std::string::npos);
    CHECK(panel.draft().islands.count == 5);

    // Settings that break the rules are not saved.
    panel.draft().islands.radius = {50.0F, 20.0F};
    panel.filePath() = engine::core::toUtf8(folder / "invalid.json");
    panel.save();
    CHECK(panel.message().kind == PanelMessageKind::Error);
    CHECK(panel.message().text.starts_with("settings not saved to '"));
}
