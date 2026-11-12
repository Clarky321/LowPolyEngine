// World generation settings: defaults, ranges and combinations (ADR 0013).
//
// The ranges below are written out from the ADR's table, not read from the
// limits constants, so the tests check the code against the documentation.
#include "engine/worldgen/WorldGenSettings.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <vector>

using engine::worldgen::clampToLimits;
using engine::worldgen::FloatRange;
using engine::worldgen::kGeneratorVersion;
using engine::worldgen::validate;
using engine::worldgen::WorldGenSettings;

namespace {

std::optional<std::string> problemOf(const WorldGenSettings& settings)
{
    const auto error = validate(settings);
    if (!error) {
        return std::nullopt;
    }
    return error->message;
}

bool isRangeProblem(const std::optional<std::string>& problem, const std::string& path)
{
    return problem.has_value() && problem->starts_with(path + " is ") &&
           problem->find("outside its range") != std::string::npos;
}

bool namesField(const std::optional<std::string>& problem, const std::string& path)
{
    return problem.has_value() && problem->starts_with(path);
}

template <typename T>
struct Field {
    const char* path;
    std::function<T&(WorldGenSettings&)> access;
    T min;
    T max;
    // Values at the limits can conflict with other fields at their defaults;
    // only the range check is then expected to accept them.
    bool coupled;
};

// The path is the member path, which is also the field's name in messages.
#define LPE_FIELD(type, member, low, high, coupled)                                                \
    Field<type>                                                                                    \
    {                                                                                              \
        #member, [](WorldGenSettings& s) -> type& { return s.member; }, low, high, coupled         \
    }

std::vector<Field<float>> floatFields()
{
    return {
        LPE_FIELD(float, bounds.min.x, -500.0F, 500.0F, true),
        LPE_FIELD(float, bounds.min.y, -500.0F, 500.0F, true),
        LPE_FIELD(float, bounds.min.z, -500.0F, 500.0F, true),
        LPE_FIELD(float, bounds.max.x, -500.0F, 500.0F, true),
        LPE_FIELD(float, bounds.max.y, -500.0F, 500.0F, true),
        LPE_FIELD(float, bounds.max.z, -500.0F, 500.0F, true),
        LPE_FIELD(float, islands.altitude.min, -500.0F, 500.0F, true),
        LPE_FIELD(float, islands.altitude.max, -500.0F, 500.0F, true),
        LPE_FIELD(float, islands.radius.min, 4.0F, 150.0F, true),
        LPE_FIELD(float, islands.radius.max, 4.0F, 150.0F, true),
        LPE_FIELD(float, islands.minHorizontalSpacing, 0.0F, 200.0F, false),
        LPE_FIELD(float, islands.minVerticalSpacing, 0.0F, 200.0F, false),
        LPE_FIELD(float, shape.ellipticity, 0.0F, 0.6F, false),
        LPE_FIELD(float, shape.outlineRoughness, 0.0F, 1.0F, false),
        LPE_FIELD(float, shape.outlineWarp, 0.0F, 64.0F, false),
        LPE_FIELD(float, elevation.maxHeightRatio, 0.05F, 1.0F, true),
        LPE_FIELD(float, elevation.ridgeWeight, 0.0F, 1.0F, false),
        LPE_FIELD(float, elevation.plateauChance, 0.0F, 1.0F, false),
        LPE_FIELD(float, underside.depthRatio.min, 0.2F, 2.0F, true),
        LPE_FIELD(float, underside.depthRatio.max, 0.2F, 2.0F, true),
        LPE_FIELD(float, underside.protrusion, 0.0F, 1.0F, false),
        LPE_FIELD(float, underside.roughness, 0.0F, 1.0F, false),
        LPE_FIELD(float, noise.frequency, 0.001F, 1.0F, false),
        LPE_FIELD(float, noise.fractal.lacunarity, 1.0F, 4.0F, false),
        LPE_FIELD(float, noise.fractal.persistence, 0.0F, 1.0F, false),
        LPE_FIELD(float, noise.warpStrength, 0.0F, 64.0F, false),
        LPE_FIELD(float, water.lakeChance, 0.0F, 1.0F, false),
        LPE_FIELD(float, water.streamChance, 0.0F, 1.0F, false),
        LPE_FIELD(float, water.waterfallChance, 0.0F, 1.0F, false),
        LPE_FIELD(float, clouds.altitude.min, -500.0F, 500.0F, true),
        LPE_FIELD(float, clouds.altitude.max, -500.0F, 500.0F, true),
        LPE_FIELD(float, clouds.size.min, 1.0F, 100.0F, true),
        LPE_FIELD(float, clouds.size.max, 1.0F, 100.0F, true),
        LPE_FIELD(float, clouds.density, 0.0F, 1.0F, false),
        LPE_FIELD(float, clouds.windDirectionDegrees, 0.0F, 360.0F, false),
        LPE_FIELD(float, clouds.windSpeed, 0.0F, 20.0F, false),
        LPE_FIELD(float, clouds.islandClearance, 0.0F, 50.0F, false),
    };
}

std::vector<Field<int>> intFields()
{
    return {
        LPE_FIELD(int, islands.count, 1, 64, false),
        LPE_FIELD(int, elevation.terraceSteps, 0, 8, false),
        LPE_FIELD(int, underside.ledgeCount, 0, 6, false),
        LPE_FIELD(int, noise.fractal.octaves, 1, 12, false),
        LPE_FIELD(int, water.maxLakesPerIsland, 0, 4, false),
        LPE_FIELD(int, clouds.clusterCount, 0, 128, false),
        LPE_FIELD(int, mesh.resolution, 8, 256, false),
    };
}

#undef LPE_FIELD

template <typename T>
WorldGenSettings withValue(const Field<T>& field, T value)
{
    WorldGenSettings settings;
    field.access(settings) = value;
    return settings;
}

template <typename T>
T defaultOf(const Field<T>& field)
{
    WorldGenSettings settings;
    return field.access(settings);
}

// A combination of fields and the problem it must cause.
struct Combination {
    const char* description;
    std::function<void(WorldGenSettings&)> change;
    const char* expectedPrefix;
};

} // namespace

TEST_CASE("Default settings are valid", "[worldgen][settings]")
{
    const WorldGenSettings settings;
    CHECK_FALSE(problemOf(settings).has_value());
    CHECK(settings.generatorVersion == kGeneratorVersion);
    CHECK(kGeneratorVersion == 1U);
    CHECK(settings.seed == 1U);
    CHECK(settings.water.enabled);
}

TEST_CASE("Every float field is checked against its range", "[worldgen][settings]")
{
    const float infinity = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    for (const Field<float>& field : floatFields()) {
        INFO(field.path);
        // Just outside each limit, and values that are never in range.
        for (const float outside :
             {std::nextafter(field.min, -infinity), std::nextafter(field.max, infinity), nan,
              infinity, -infinity}) {
            INFO("value " << outside);
            CHECK(isRangeProblem(problemOf(withValue(field, outside)), field.path));
        }
        for (const float atLimit : {field.min, field.max}) {
            INFO("value " << atLimit);
            const auto problem = problemOf(withValue(field, atLimit));
            CHECK_FALSE(isRangeProblem(problem, field.path));
            if (!field.coupled) {
                CHECK_FALSE(problem.has_value());
            }
        }
        CHECK_FALSE(problemOf(withValue(field, defaultOf(field))).has_value());
    }
}

TEST_CASE("Every integer field is checked against its range", "[worldgen][settings]")
{
    for (const Field<int>& field : intFields()) {
        INFO(field.path);
        for (const int outside : {field.min - 1, field.max + 1, std::numeric_limits<int>::min(),
                                  std::numeric_limits<int>::max()}) {
            INFO("value " << outside);
            CHECK(isRangeProblem(problemOf(withValue(field, outside)), field.path));
        }
        for (const int atLimit : {field.min, field.max}) {
            INFO("value " << atLimit);
            CHECK_FALSE(problemOf(withValue(field, atLimit)).has_value());
        }
    }
}

TEST_CASE("Range problems name the field, its value and its range", "[worldgen][settings]")
{
    WorldGenSettings settings;
    settings.islands.count = 0;
    CHECK(problemOf(settings) == "islands.count is 0, outside its range 1 to 64");

    settings = WorldGenSettings{};
    settings.noise.frequency = 0.0005F;
    CHECK(problemOf(settings) == "noise.frequency is 0.0005, outside its range 0.001 to 1");

    settings = WorldGenSettings{};
    settings.clouds.windDirectionDegrees = std::numeric_limits<float>::quiet_NaN();
    CHECK(problemOf(settings) == "clouds.windDirectionDegrees is nan, outside its range 0 to 360");
}

TEST_CASE("The generator version must be the one of this build", "[worldgen][settings]")
{
    for (const std::uint32_t version : {0U, 2U, std::numeric_limits<std::uint32_t>::max()}) {
        WorldGenSettings settings;
        settings.generatorVersion = version;
        const auto problem = problemOf(settings);
        CHECK(namesField(problem, "generatorVersion is " + std::to_string(version)));
        CHECK(problem.has_value());
        if (problem) {
            CHECK(problem->ends_with("but this build generates version 1"));
        }
    }
}

TEST_CASE("Any seed is valid", "[worldgen][settings]")
{
    for (const std::uint64_t seed : {std::uint64_t{0}, std::numeric_limits<std::uint64_t>::max()}) {
        WorldGenSettings settings;
        settings.seed = seed;
        CHECK_FALSE(problemOf(settings).has_value());
    }
}

TEST_CASE("Problems are reported in a fixed order", "[worldgen][settings]")
{
    // The version first.
    WorldGenSettings settings;
    settings.generatorVersion = 3;
    settings.islands.count = 0;
    CHECK(namesField(problemOf(settings), "generatorVersion"));

    // Then the ranges in the order of declaration.
    settings = WorldGenSettings{};
    settings.mesh.resolution = 0;
    settings.islands.count = 0;
    CHECK(namesField(problemOf(settings), "islands.count"));

    // Then the combinations.
    settings = WorldGenSettings{};
    settings.islands.altitude = FloatRange{100.0F, 50.0F};
    settings.mesh.resolution = 0;
    CHECK(namesField(problemOf(settings), "mesh.resolution"));
}

TEST_CASE("Inconsistent combinations are rejected with the field's path", "[worldgen][settings]")
{
    const std::vector<Combination> combinations{
        {"bounds narrower than the minimum extent",
         [](WorldGenSettings& s) { s.bounds.max.x = s.bounds.min.x + 19.5F; }, "bounds.max.x ("},
        {"bounds without height", [](WorldGenSettings& s) { s.bounds.max.y = s.bounds.min.y; },
         "bounds.max.y ("},
        {"inverted bounds", [](WorldGenSettings& s) { s.bounds.max.z = s.bounds.min.z - 10.0F; },
         "bounds.max.z ("},
        {"bounds beyond the far plane",
         [](WorldGenSettings& s) {
             s.bounds.min = {-400.0F, -400.0F, -400.0F};
             s.bounds.max = {400.0F, 400.0F, 400.0F};
         },
         "bounds span a diagonal of 1385.6 m"},
        {"inverted island altitudes",
         [](WorldGenSettings& s) { s.islands.altitude = FloatRange{100.0F, 50.0F}; },
         "islands.altitude.min (100) is above islands.altitude.max (50)"},
        {"inverted island radii",
         [](WorldGenSettings& s) { s.islands.radius = FloatRange{60.0F, 50.0F}; },
         "islands.radius.min (60) is above islands.radius.max (50)"},
        {"inverted underside depths",
         [](WorldGenSettings& s) { s.underside.depthRatio = FloatRange{1.25F, 0.75F}; },
         "underside.depthRatio.min (1.25) is above underside.depthRatio.max (0.75)"},
        {"inverted cloud altitudes",
         [](WorldGenSettings& s) { s.clouds.altitude = FloatRange{100.0F, 0.0F}; },
         "clouds.altitude.min (100) is above clouds.altitude.max (0)"},
        {"inverted cloud sizes",
         [](WorldGenSettings& s) { s.clouds.size = FloatRange{30.0F, 10.0F}; },
         "clouds.size.min (30) is above clouds.size.max (10)"},
        {"size range that cannot fit the bounds",
         [](WorldGenSettings& s) {
             s.bounds.min.x = -100.0F;
             s.bounds.max.x = 100.0F;
             s.islands.radius.max = 120.0F;
         },
         "islands.radius.max is 120, so an island needs 240 m, but the bounds are only 200 m wide"},
        {"altitude range above the Y bounds",
         [](WorldGenSettings& s) { s.islands.altitude = FloatRange{200.0F, 300.0F}; },
         "islands.altitude.max is 300, so island tops reach"},
        {"island tops above the Y bounds",
         [](WorldGenSettings& s) { s.islands.altitude.max = 150.0F; },
         "islands.altitude.max is 150, so island tops reach"},
        {"island undersides below the Y bounds",
         [](WorldGenSettings& s) { s.islands.altitude.min = -40.0F; },
         "islands.altitude.min is -40, so island undersides reach"},
        {"altitude range below the Y bounds",
         [](WorldGenSettings& s) { s.islands.altitude = FloatRange{-300.0F, -200.0F}; },
         "islands.altitude.min is -300, so island undersides reach"},
        {"horizontal spacing larger than the world",
         [](WorldGenSettings& s) {
             s.bounds.min.x = -90.0F;
             s.bounds.max.x = 90.0F;
             s.islands.minHorizontalSpacing = 190.0F;
         },
         "islands.minHorizontalSpacing is 190, larger than the world (180 m across)"},
        {"vertical spacing larger than the world",
         [](WorldGenSettings& s) {
             s.bounds.max.y = 100.0F;
             s.islands.altitude = FloatRange{-20.0F, 70.0F};
             s.islands.minVerticalSpacing = 190.0F;
         },
         "islands.minVerticalSpacing is 190, larger than the world (180 m high)"},
        {"clouds below the bounds", [](WorldGenSettings& s) { s.clouds.altitude.min = -90.0F; },
         "clouds.altitude.min is -90, below bounds.min.y (-80)"},
        {"clouds above the bounds", [](WorldGenSettings& s) { s.clouds.altitude.max = 170.0F; },
         "clouds.altitude.max is 170, above bounds.max.y (160)"},
    };
    for (const Combination& combination : combinations) {
        INFO(combination.description);
        WorldGenSettings settings;
        combination.change(settings);
        const auto problem = problemOf(settings);
        INFO("problem: " << problem.value_or("none"));
        CHECK(namesField(problem, combination.expectedPrefix));
    }
}

TEST_CASE("Combinations exactly at their limits are valid", "[worldgen][settings]")
{
    WorldGenSettings settings;
    settings.islands.altitude = FloatRange{40.0F, 40.0F};
    settings.clouds.size = FloatRange{12.0F, 12.0F};
    CHECK_FALSE(problemOf(settings).has_value());

    // The largest island exactly as wide as the bounds.
    settings = WorldGenSettings{};
    settings.bounds.min.x = -55.0F;
    settings.bounds.max.x = 55.0F;
    CHECK_FALSE(problemOf(settings).has_value());

    // Island tops exactly at the top of the bounds (110 + 0.5 * 50 = 135) and
    // undersides exactly at the bottom (-20 - 1.0 * 50 = -70).
    settings = WorldGenSettings{};
    settings.islands.radius.max = 50.0F;
    settings.elevation.maxHeightRatio = 0.5F;
    settings.bounds.max.y = 135.0F;
    settings.bounds.min.y = -70.0F;
    settings.clouds.altitude = FloatRange{-70.0F, 135.0F};
    CHECK_FALSE(problemOf(settings).has_value());

    // Spacings exactly as large as the world.
    settings = WorldGenSettings{};
    settings.bounds.min.x = -100.0F;
    settings.bounds.max.x = 100.0F;
    settings.islands.minHorizontalSpacing = 200.0F;
    settings.islands.minVerticalSpacing = 200.0F;
    settings.bounds.min.y = -80.0F;
    settings.bounds.max.y = 120.0F;
    settings.islands.altitude = FloatRange{-20.0F, 90.0F};
    settings.clouds.altitude = FloatRange{-80.0F, 120.0F};
    CHECK_FALSE(problemOf(settings).has_value());

    // A diagonal just below the far plane (600 x 240 x 760: 997.6 m), and one
    // just above it (600 x 240 x 780: 1012.9 m).
    settings = WorldGenSettings{};
    settings.bounds.min = {-300.0F, -80.0F, -380.0F};
    settings.bounds.max = {300.0F, 160.0F, 380.0F};
    CHECK_FALSE(problemOf(settings).has_value());
    settings.bounds.min.z = -390.0F;
    settings.bounds.max.z = 390.0F;
    CHECK(namesField(problemOf(settings), "bounds span a diagonal of 1012.9 m"));
}

TEST_CASE("clampToLimits moves every field into its range", "[worldgen][settings]")
{
    const float infinity = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    for (const Field<float>& field : floatFields()) {
        INFO(field.path);
        struct Case {
            float given;
            float expected;
        };
        for (const Case c :
             {Case{field.min - 1.0F, field.min}, Case{field.max + 1.0F, field.max},
              Case{-infinity, field.min}, Case{infinity, field.max}, Case{nan, defaultOf(field)},
              Case{field.min, field.min}, Case{field.max, field.max}}) {
            INFO("value " << c.given);
            // Every other field keeps its value.
            CHECK(clampToLimits(withValue(field, c.given)) == withValue(field, c.expected));
        }
        if (!field.coupled) {
            CHECK_FALSE(problemOf(clampToLimits(withValue(field, field.max + 1.0F))).has_value());
        }
    }
    for (const Field<int>& field : intFields()) {
        INFO(field.path);
        CHECK(clampToLimits(withValue(field, field.min - 1)) == withValue(field, field.min));
        CHECK(clampToLimits(withValue(field, field.max + 1)) == withValue(field, field.max));
        CHECK(clampToLimits(withValue(field, std::numeric_limits<int>::min())) ==
              withValue(field, field.min));
        CHECK(clampToLimits(withValue(field, std::numeric_limits<int>::max())) ==
              withValue(field, field.max));
        CHECK_FALSE(problemOf(clampToLimits(withValue(field, field.max + 1))).has_value());
    }
}

TEST_CASE("clampToLimits keeps valid settings and leaves combinations to validate",
          "[worldgen][settings]")
{
    const WorldGenSettings defaults;
    CHECK(clampToLimits(defaults) == defaults);

    WorldGenSettings inverted;
    inverted.islands.altitude = FloatRange{100.0F, 50.0F};
    CHECK(clampToLimits(inverted) == inverted);
    CHECK(namesField(problemOf(clampToLimits(inverted)), "islands.altitude.min"));

    WorldGenSettings other;
    other.seed = 0xDEADBEEFULL;
    other.generatorVersion = 7;
    other.water.enabled = false;
    const WorldGenSettings clamped = clampToLimits(other);
    CHECK(clamped.seed == 0xDEADBEEFULL);
    CHECK(clamped.generatorVersion == 7U);
    CHECK_FALSE(clamped.water.enabled);
}
