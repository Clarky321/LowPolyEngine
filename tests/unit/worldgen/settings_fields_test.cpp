// The editable fields of the world generation settings (M3.12), as the
// WorldGen panel uses them.
#include "engine/worldgen/WorldGenSettings.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

using engine::worldgen::settingsFields;
using engine::worldgen::validate;
using engine::worldgen::WorldGenSettings;

TEST_CASE("Settings fields list every field once with its limits and default",
          "[worldgen][settings]")
{
    WorldGenSettings settings;
    const auto fields = settingsFields(settings);
    // 44 numeric fields and the water switch.
    REQUIRE(fields.size() == 45U);
    CHECK(fields.front().path == "bounds.min.x");
    CHECK(fields.back().path == "mesh.resolution");

    std::set<std::string_view> paths;
    const auto* first = reinterpret_cast<const unsigned char*>(&settings);
    const auto* last = first + sizeof(settings);
    for (std::size_t i = 0; i < fields.size(); ++i) {
        const auto& field = fields[i];
        INFO(field.path);
        CHECK(paths.insert(field.path).second);
        CHECK(field.min <= field.defaultValue);
        CHECK(field.defaultValue <= field.max);
        std::visit(
            [&](auto* value) {
                // Points into the settings it was taken from.
                const auto* address = reinterpret_cast<const unsigned char*>(value);
                CHECK(address >= first);
                CHECK(address < last);
                CHECK(static_cast<double>(*value) == field.defaultValue);
            },
            field.value);
    }
    // The water switch sits before the other water fields.
    std::size_t enabled = 0;
    std::size_t lakeChance = 0;
    for (std::size_t i = 0; i < fields.size(); ++i) {
        if (fields[i].path == "water.enabled") {
            enabled = i;
        }
        if (fields[i].path == "water.lakeChance") {
            lakeChance = i;
        }
    }
    CHECK(enabled + 1U == lakeChance);
    CHECK(std::holds_alternative<bool*>(fields[enabled].value));
}

TEST_CASE("Settings fields are bound to the fields validation names", "[worldgen][settings]")
{
    const WorldGenSettings defaults;
    WorldGenSettings probe;
    const auto paths = settingsFields(probe);
    for (std::size_t i = 0; i < paths.size(); ++i) {
        WorldGenSettings settings;
        const auto fields = settingsFields(settings);
        const auto& field = fields[i];
        INFO(field.path);
        bool numeric = true;
        // One step beyond the field's maximum: validate() names exactly this
        // field, the first numeric one out of range.
        std::visit(
            [&](auto* value) {
                using T = std::remove_pointer_t<decltype(value)>;
                if constexpr (std::is_same_v<T, bool>) {
                    numeric = false;
                } else {
                    *value = static_cast<T>(field.max + 1.0);
                }
            },
            field.value);
        if (!numeric) {
            *std::get<bool*>(field.value) = false;
            CHECK_FALSE(settings.water.enabled);
            continue;
        }
        const auto error = validate(settings);
        REQUIRE(error.has_value());
        CHECK(error->message.starts_with(std::string{field.path} + " is "));
        CHECK_FALSE(settings == defaults);
    }
}
