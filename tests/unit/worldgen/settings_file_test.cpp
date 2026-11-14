// World generation settings as a versioned JSON file (M3.11, ADR 0004).
#include "engine/worldgen/SettingsFile.hpp"

#include "worldgen/SettingsFields.hpp"

#include "engine/core/FileSystem.hpp"
#include "engine/worldgen/Archipelago.hpp"
#include "engine/worldgen/IslandMesh.hpp"
#include "engine/worldgen/Random.hpp"

#include <catch2/catch_test_macros.hpp>

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

using engine::worldgen::loadSettings;
using engine::worldgen::saveSettings;
using engine::worldgen::settingsFromJson;
using engine::worldgen::settingsToJson;
using engine::worldgen::WorldGenSettings;

namespace {

const std::filesystem::path kDefaultFile =
    std::filesystem::path(LPE_ASSET_DIRECTORY) / "worlds" / "default.json";

// A settings file with the envelope and `body` (members after it, if any).
std::string fileWith(std::string_view body)
{
    std::string text = R"({"format": "lpe.worldgen.settings", "formatVersion": 1)";
    if (!body.empty()) {
        text += ", ";
        text += body;
    }
    return text + "}";
}

WorldGenSettings roundTrip(const WorldGenSettings& settings)
{
    auto loaded = settingsFromJson(settingsToJson(settings));
    REQUIRE(loaded.hasValue());
    CHECK(loaded.value().warnings.empty());
    return loaded.value().settings;
}

std::string errorOf(std::string_view text)
{
    const auto loaded = settingsFromJson(text);
    REQUIRE_FALSE(loaded.hasValue());
    return loaded.error().message;
}

std::filesystem::path scratchFolder()
{
    const auto folder = std::filesystem::temp_directory_path() / "lpe-settings-file-test";
    std::filesystem::create_directories(folder);
    return folder;
}

} // namespace

TEST_CASE("The shipped default settings file holds the default settings", "[worldgen][settings]")
{
    const auto text = engine::core::readTextFile(kDefaultFile);
    REQUIRE(text.hasValue());
    // Written by settingsToJson, byte for byte: the file stays in step with
    // the code.
    CHECK(text.value() == settingsToJson(WorldGenSettings{}));
    const auto loaded = loadSettings(kDefaultFile);
    REQUIRE(loaded.hasValue());
    CHECK(loaded.value().settings == WorldGenSettings{});
    CHECK(loaded.value().warnings.empty());
}

TEST_CASE("Settings round trip through JSON", "[worldgen][settings]")
{
    SECTION("the text starts with the envelope and uses short numbers")
    {
        const std::string text = settingsToJson(WorldGenSettings{});
        CHECK(text.starts_with("{\n  \"format\": \"lpe.worldgen.settings\",\n"
                               "  \"formatVersion\": 1,\n  \"generatorVersion\": 1,\n"
                               "  \"seed\": \"0x0000000000000001\",\n"));
        CHECK(text.ends_with("}\n"));
        CHECK(text.find("\"frequency\": 0.045,") != std::string::npos);
        CHECK(text.find("\"enabled\": true,") != std::string::npos);
        CHECK(text.find("\"resolution\": 48") != std::string::npos);
    }
    SECTION("every field on its own path")
    {
        // Each numeric field changed alone to another valid value comes back
        // in that field and nowhere else.
        const WorldGenSettings defaults;
        int changed = 0;
        int fields = 0;
        engine::worldgen::detail::forEachNumericField(
            defaults, defaults,
            [&](std::string_view path, const auto& value, const auto& limits, const auto&) {
                ++fields;
                using T = std::decay_t<decltype(value)>;
                for (const T candidate :
                     {static_cast<T>(value + static_cast<T>(1)),
                      static_cast<T>(value - static_cast<T>(1)),
                      static_cast<T>((value + limits.min) / static_cast<T>(2)),
                      static_cast<T>((value + limits.max) / static_cast<T>(2))}) {
                    WorldGenSettings settings = defaults;
                    bool set = false;
                    engine::worldgen::detail::forEachNumericField(
                        settings, defaults,
                        [&](std::string_view other, auto& field, const auto&, const auto&) {
                            using U = std::decay_t<decltype(field)>;
                            if constexpr (std::is_same_v<U, T>) {
                                if (other == path) {
                                    field = candidate;
                                    set = true;
                                }
                            }
                        });
                    REQUIRE(set);
                    if (candidate == value || engine::worldgen::validate(settings).has_value()) {
                        continue;
                    }
                    INFO(path);
                    CHECK(roundTrip(settings) == settings);
                    ++changed;
                    break;
                }
            });
        CHECK(fields == 44);
        // Every field can be changed alone into a valid world.
        CHECK(changed == fields);
    }
    SECTION("water switched off")
    {
        WorldGenSettings settings;
        settings.water.enabled = false;
        CHECK(roundTrip(settings) == settings);
    }
    SECTION("floats keep every bit")
    {
        engine::worldgen::Pcg32 random{7U};
        WorldGenSettings settings;
        bool exact = true;
        for (int i = 0; i < 3000; ++i) {
            settings.noise.frequency = random.nextFloat(0.001F, 1.0F);
            settings.clouds.windDirectionDegrees = random.nextFloat(0.0F, 360.0F);
            const WorldGenSettings back = roundTrip(settings);
            exact = exact &&
                    std::bit_cast<std::uint32_t>(back.noise.frequency) ==
                        std::bit_cast<std::uint32_t>(settings.noise.frequency) &&
                    std::bit_cast<std::uint32_t>(back.clouds.windDirectionDegrees) ==
                        std::bit_cast<std::uint32_t>(settings.clouds.windDirectionDegrees);
        }
        CHECK(exact);
        for (const float value : {0.1F, 1.0F / 3.0F, 0.001F, 1.0F, std::nextafter(0.5F, 1.0F),
                                  std::nextafter(1.0F, 0.0F)}) {
            settings.noise.frequency = value;
            CHECK(roundTrip(settings).noise.frequency == value);
        }
    }
    SECTION("seeds above 2^53 keep every bit")
    {
        for (const std::uint64_t seed :
             {std::uint64_t{0}, std::uint64_t{1}, (std::uint64_t{1} << 53U) + 1U,
              std::uint64_t{0x9E3779B97F4A7C15ULL}, std::numeric_limits<std::uint64_t>::max()}) {
            WorldGenSettings settings;
            settings.seed = seed;
            CHECK(roundTrip(settings).seed == seed);
        }
        WorldGenSettings largest;
        largest.seed = std::numeric_limits<std::uint64_t>::max();
        CHECK(settingsToJson(largest).find("\"seed\": \"0xFFFFFFFFFFFFFFFF\"") !=
              std::string::npos);
        // Lower-case digits and fewer than 16 of them read too.
        const auto loaded = settingsFromJson(fileWith(R"("seed": "0x2a")"));
        REQUIRE(loaded.hasValue());
        CHECK(loaded.value().settings.seed == 42U);
    }
}

TEST_CASE("Missing settings fields take their defaults", "[worldgen][settings]")
{
    const auto minimal = settingsFromJson(fileWith(""));
    REQUIRE(minimal.hasValue());
    CHECK(minimal.value().settings == WorldGenSettings{});
    CHECK(minimal.value().warnings.empty());

    const auto some = settingsFromJson(fileWith(
        R"("islands": {"count": 5, "altitude": {"max": 90}}, "water": {"enabled": false})"));
    REQUIRE(some.hasValue());
    WorldGenSettings expected;
    expected.islands.count = 5;
    expected.islands.altitude.max = 90.0F;
    expected.water.enabled = false;
    CHECK(some.value().settings == expected);
}

TEST_CASE("Unknown settings fields are ignored with a warning", "[worldgen][settings]")
{
    const auto loaded = settingsFromJson(fileWith(
        R"("author": "me", "islands": {"count": 6, "colour": "red"}, "noise": {"fractal": {"gain": 2}})"));
    REQUIRE(loaded.hasValue());
    CHECK(loaded.value().settings.islands.count == 6);
    CHECK(loaded.value().warnings ==
          std::vector<std::string>{"unknown field 'author' ignored",
                                   "unknown field 'islands.colour' ignored",
                                   "unknown field 'noise.fractal.gain' ignored"});
}

TEST_CASE("Settings fields of the wrong type are named with their path", "[worldgen][settings]")
{
    CHECK(errorOf(fileWith(R"("islands": {"count": "many"})")) ==
          "islands.count: expected an integer, found string \"many\"");
    CHECK(errorOf(fileWith(R"("islands": {"count": 12.5})")) ==
          "islands.count: expected an integer, found number 12.5");
    CHECK(errorOf(fileWith(R"("islands": {"count": 99999999999})"))
              .starts_with("islands.count: 99999999999 is outside the range of the field's type"));
    CHECK(errorOf(fileWith(R"("shape": {"ellipticity": "0.3"})")) ==
          "shape.ellipticity: expected a number, found string \"0.3\"");
    CHECK(errorOf(fileWith(R"("shape": {"ellipticity": 1e300})")) ==
          "shape.ellipticity: 1e+300 is outside the range of a float");
    CHECK(errorOf(fileWith(R"("water": {"enabled": 1})")) ==
          "water.enabled: expected true or false, found number 1");
    CHECK(errorOf(fileWith(R"("islands": 5)")) == "islands: expected an object, found number 5");
    CHECK(errorOf(fileWith(R"("islands": {"altitude": []})")) ==
          "islands.altitude: expected an object, found array");
    CHECK(errorOf(fileWith(R"("clouds": {"size": {"min": null}})")) ==
          "clouds.size.min: expected a number, found null");
    CHECK(errorOf(fileWith(R"("generatorVersion": "1")")) ==
          "generatorVersion: expected an integer, found string \"1\"");
    CHECK(errorOf(fileWith(R"("seed": 42)"))
              .starts_with("seed: expected a hex string such as \"0x0000000000000001\", found "
                           "number 42; seeds are "
                           "strings"));
    for (const char* seed : {R"("0x")", R"("42")", R"("0xG1")", R"("0x10000000000000000")"}) {
        INFO(seed);
        CHECK(errorOf(fileWith(std::string{R"("seed": )"} + seed)).starts_with("seed: "));
    }
}

TEST_CASE("Settings files must be JSON of this format", "[worldgen][settings]")
{
    const std::string broken =
        "{\n  \"format\": \"lpe.worldgen.settings\",\n  \"formatVersion\": 1,\n}";
    const std::string message = errorOf(broken);
    CHECK(message.starts_with("malformed JSON: parse error at line 4, column 1"));
    CHECK(errorOf("").starts_with("malformed JSON: "));
    CHECK(errorOf("{\"format\": ").starts_with("malformed JSON: "));
    CHECK(errorOf("[1, 2]") == "expected a JSON object, found array");
    CHECK(errorOf(R"({"formatVersion": 1})").starts_with("format: missing"));
    CHECK(errorOf(R"({"format": "lpe.scene", "formatVersion": 1})") ==
          "format: expected \"lpe.worldgen.settings\", found string \"lpe.scene\"");
    CHECK(errorOf(R"({"format": "lpe.worldgen.settings"})") == "formatVersion: missing");
    CHECK(errorOf(R"({"format": "lpe.worldgen.settings", "formatVersion": "1"})") ==
          "formatVersion: expected an integer, found string \"1\"");
}

TEST_CASE("A newer settings format version is refused", "[worldgen][settings]")
{
    CHECK(errorOf(R"({"format": "lpe.worldgen.settings", "formatVersion": 2})") ==
          "formatVersion 2 is newer than this build reads (up to 1); update the program");
    CHECK(errorOf(R"({"format": "lpe.worldgen.settings", "formatVersion": 0})") ==
          "formatVersion: 0 is not a version (versions start at 1)");
}

TEST_CASE("Loaded settings pass the settings validation", "[worldgen][settings]")
{
    CHECK(errorOf(fileWith(R"("islands": {"count": 0})")) ==
          "islands.count is 0, outside its range 1 to 64");
    CHECK(errorOf(fileWith(R"("islands": {"radius": {"min": 50, "max": 20}})"))
              .starts_with("islands.radius"));
    CHECK(errorOf(fileWith(R"("generatorVersion": 2)")).starts_with("generatorVersion is 2"));
}

TEST_CASE("Settings files are saved and loaded", "[worldgen][settings]")
{
    const auto folder = scratchFolder();
    WorldGenSettings settings;
    settings.seed = 0xDEADBEEFCAFEF00DULL;
    settings.clouds.density = 0.8F;
    const auto file = folder / "nested" / "world.json";
    std::filesystem::remove_all(folder / "nested");
    REQUIRE_FALSE(saveSettings(file, settings).has_value());
    const auto loaded = loadSettings(file);
    REQUIRE(loaded.hasValue());
    CHECK(loaded.value().settings == settings);

    WorldGenSettings invalid;
    invalid.islands.count = 0;
    const auto refused = saveSettings(folder / "invalid.json", invalid);
    REQUIRE(refused.has_value());
    CHECK(refused->message.starts_with("settings not saved to '"));
    CHECK(refused->message.find("islands.count is 0") != std::string::npos);

    const auto missing = loadSettings(folder / "missing.json");
    REQUIRE_FALSE(missing.hasValue());
    CHECK(missing.error().message.starts_with("cannot open file '"));

    const std::string bad = "{\"format\": \"lpe.worldgen.settings\", \"formatVersion\": 1, "
                            "\"mesh\": {\"resolution\": \"high\"}}";
    REQUIRE_FALSE(
        engine::core::writeBinaryFile(
            folder / "bad.json", std::span<const std::uint8_t>(
                                     reinterpret_cast<const std::uint8_t*>(bad.data()), bad.size()))
            .has_value());
    const auto broken = loadSettings(folder / "bad.json");
    REQUIRE_FALSE(broken.hasValue());
    CHECK(broken.error().message.find("bad.json': mesh.resolution: expected an integer") !=
          std::string::npos);
}

TEST_CASE("A loaded settings file reproduces the same world", "[worldgen][settings]")
{
    WorldGenSettings settings;
    settings.seed = (std::uint64_t{1} << 53U) + 7U;
    settings.mesh.resolution = 16;
    settings.water.lakeChance = 0.8F;
    settings.clouds.density = 0.7F;
    settings.noise.frequency = 0.0517F;
    const auto loaded = settingsFromJson(settingsToJson(settings));
    REQUIRE(loaded.hasValue());
    const auto original = engine::worldgen::generateArchipelago(settings);
    const auto again = engine::worldgen::generateArchipelago(loaded.value().settings);
    REQUIRE(original.hasValue());
    REQUIRE(again.hasValue());
    const auto& a = original.value();
    const auto& b = again.value();
    REQUIRE(a.islands.size() == b.islands.size());
    REQUIRE(a.clouds.clusters.size() == b.clouds.clusters.size());
    for (std::size_t i = 0; i < a.islands.size(); ++i) {
        CHECK(a.islands[i].shape.island == b.islands[i].shape.island);
        CHECK(engine::worldgen::meshHash(a.islands[i].mesh) ==
              engine::worldgen::meshHash(b.islands[i].mesh));
        CHECK(engine::worldgen::meshHash(a.islands[i].water.mesh) ==
              engine::worldgen::meshHash(b.islands[i].water.mesh));
    }
    for (std::size_t i = 0; i < a.clouds.clusters.size(); ++i) {
        CHECK(engine::worldgen::meshHash(a.clouds.clusters[i].mesh) ==
              engine::worldgen::meshHash(b.clouds.clusters[i].mesh));
    }
    CHECK(a.triangleCount() == b.triangleCount());
}
