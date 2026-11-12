// Deterministic noise for world generation (ADR 0003, Amendment 2).
//
// Golden samples were computed with an independent implementation of the
// formulas in engine/worldgen/Noise.hpp. They are compared with a tolerance of
// 1e-6, as the M3 plan asks for float results across platforms; the formulas
// use only correctly rounded IEEE operations, so they are expected to match
// exactly.
#include "engine/worldgen/Noise.hpp"

#include "engine/worldgen/Random.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <vector>

using engine::math::Vec2;
using engine::worldgen::domainWarp;
using engine::worldgen::fbm;
using engine::worldgen::FractalNoiseParams;
using engine::worldgen::gradientNoise;
using engine::worldgen::kMaxNoiseOctaves;
using engine::worldgen::Pcg32;
using engine::worldgen::ridgeNoise;

namespace {

constexpr float kGoldenTolerance = 1.0e-6F;

FractalNoiseParams fractalParams(int octaves = 4, float lacunarity = 2.0F, float persistence = 0.5F)
{
    FractalNoiseParams params;
    params.octaves = octaves;
    params.lacunarity = lacunarity;
    params.persistence = persistence;
    return params;
}

bool near(float actual, float expected)
{
    return std::abs(actual - expected) <= kGoldenTolerance;
}

std::uint32_t bitsOf(float value)
{
    return std::bit_cast<std::uint32_t>(value);
}

// Positions uniform in [-extent, extent)^2, the same in every run.
std::vector<Vec2> samplePositions(std::uint64_t seed, int count, float extent)
{
    Pcg32 rng{seed};
    std::vector<Vec2> positions;
    positions.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        const float x = rng.nextFloat(-extent, extent);
        const float y = rng.nextFloat(-extent, extent);
        positions.emplace_back(x, y);
    }
    return positions;
}

using ScalarNoise = std::function<float(Vec2)>;

struct NamedNoise {
    const char* name;
    ScalarNoise noise;
};

std::vector<NamedNoise> allScalarNoises()
{
    const FractalNoiseParams params = fractalParams();
    return {
        {"gradientNoise", [](Vec2 p) { return gradientNoise(11U, p); }},
        {"fbm", [params](Vec2 p) { return fbm(11U, p, params); }},
        {"ridgeNoise", [params](Vec2 p) { return ridgeNoise(11U, p, params); }},
        {"domainWarp x", [params](Vec2 p) { return domainWarp(11U, p, 1.5F, params).x; }},
        {"domainWarp y", [params](Vec2 p) { return domainWarp(11U, p, 1.5F, params).y; }},
    };
}

struct SeededSample {
    std::uint64_t seed;
    Vec2 position;
    float expected;
};

const std::vector<Vec2> kFractalPositions{
    {0.3F, 0.7F}, {12.5F, -8.25F}, {-100.125F, 55.5F}, {1234.5F, 4321.25F}};

} // namespace

TEST_CASE("gradientNoise matches its golden samples", "[worldgen][noise]")
{
    const std::vector<SeededSample> samples{
        {0x0ULL, {0.5F, 0.5F}, 0.407746583F},
        {0x0ULL, {1.25F, -3.75F}, 0.114872977F},
        {0x0ULL, {123.456F, 789.012F}, -0.597682238F},
        {0x0ULL, {-0.1F, 0.9F}, 0.0671491697F},
        {0x0ULL, {1000.5F, -2000.25F}, 0.311034024F},
        {0x2AULL, {0.5F, 0.5F}, -0.407746583F},
        {0x2AULL, {1.25F, -3.75F}, -0.370289773F},
        {0x2AULL, {123.456F, 789.012F}, 0.565350235F},
        {0x2AULL, {-0.1F, 0.9F}, -0.140885726F},
        {0x2AULL, {1000.5F, -2000.25F}, 0.198512942F},
        {0xFFFFFFFFFFFFFFFFULL, {0.5F, 0.5F}, 0.29147768F},
        {0xFFFFFFFFFFFFFFFFULL, {1.25F, -3.75F}, -0.31076771F},
        {0xFFFFFFFFFFFFFFFFULL, {123.456F, 789.012F}, 0.0630807281F},
        {0xFFFFFFFFFFFFFFFFULL, {-0.1F, 0.9F}, 0.000413444359F},
        {0xFFFFFFFFFFFFFFFFULL, {1000.5F, -2000.25F}, -0.37859574F},
    };
    for (const SeededSample& sample : samples) {
        INFO("seed " << sample.seed << " at (" << sample.position.x << ", " << sample.position.y
                     << ")");
        CHECK(near(gradientNoise(sample.seed, sample.position), sample.expected));
    }
}

TEST_CASE("fbm matches its golden samples", "[worldgen][noise]")
{
    const std::vector<float> defaults{-0.0940906182F, -0.0235388093F, -0.25872156F, -0.0318014473F};
    const std::vector<float> custom{-0.0421330519F, 0.336992711F, -0.327836871F, -0.254334092F};
    for (std::size_t i = 0; i < kFractalPositions.size(); ++i) {
        CHECK(near(fbm(42U, kFractalPositions[i], fractalParams()), defaults[i]));
        CHECK(near(fbm(7U, kFractalPositions[i], fractalParams(7, 2.5F, 0.6F)), custom[i]));
    }
}

TEST_CASE("ridgeNoise matches its golden samples", "[worldgen][noise]")
{
    const std::vector<float> defaults{0.511227131F, 0.89924264F, 0.534084618F, 0.794039071F};
    const std::vector<float> custom{0.497538239F, 0.678744614F, 0.391315192F, 0.229775146F};
    for (std::size_t i = 0; i < kFractalPositions.size(); ++i) {
        CHECK(near(ridgeNoise(42U, kFractalPositions[i], fractalParams()), defaults[i]));
        CHECK(near(ridgeNoise(9U, kFractalPositions[i], fractalParams(3, 3.0F, 0.4F)), custom[i]));
    }
}

TEST_CASE("domainWarp matches its golden samples", "[worldgen][noise]")
{
    const std::vector<Vec2> expected{{0.072872065F, 0.713757575F},
                                     {12.4687757F, -8.00203419F},
                                     {-99.8027954F, 55.4001122F},
                                     {1234.10461F, 4321.14209F}};
    for (std::size_t i = 0; i < kFractalPositions.size(); ++i) {
        const Vec2 warped = domainWarp(42U, kFractalPositions[i], 1.5F, fractalParams());
        // Relative to the coordinate: one float step at 4321 is about 5e-4.
        const float tolerance = kGoldenTolerance * std::max(1.0F, std::abs(expected[i].x));
        CHECK(std::abs(warped.x - expected[i].x) <= tolerance);
        CHECK(std::abs(warped.y - expected[i].y) <=
              kGoldenTolerance * std::max(1.0F, std::abs(expected[i].y)));
    }
}

TEST_CASE("gradientNoise is 0 at every lattice point", "[worldgen][noise]")
{
    int nonZero = 0;
    for (const std::uint64_t seed : {std::uint64_t{0}, std::uint64_t{42}, std::uint64_t{999}}) {
        for (int y = -20; y <= 20; ++y) {
            for (int x = -20; x <= 20; ++x) {
                if (gradientNoise(seed, Vec2{static_cast<float>(x), static_cast<float>(y)}) !=
                    0.0F) {
                    ++nonZero;
                }
            }
        }
    }
    CHECK(nonZero == 0);
}

TEST_CASE("Noise values stay in their ranges", "[worldgen][noise]")
{
    const std::vector<Vec2> positions = samplePositions(1U, 100000, 1000.0F);
    const FractalNoiseParams params = fractalParams();

    float gradientMin = 1.0F;
    float gradientMax = -1.0F;
    double absoluteSum = 0.0;
    float fbmMin = 1.0F;
    float fbmMax = -1.0F;
    float ridgeMin = 1.0F;
    float ridgeMax = 0.0F;
    for (const Vec2 position : positions) {
        const float gradient = gradientNoise(5U, position);
        gradientMin = std::min(gradientMin, gradient);
        gradientMax = std::max(gradientMax, gradient);
        absoluteSum += static_cast<double>(std::abs(gradient));
        const float fractal = fbm(5U, position, params);
        fbmMin = std::min(fbmMin, fractal);
        fbmMax = std::max(fbmMax, fractal);
        const float ridge = ridgeNoise(5U, position, params);
        ridgeMin = std::min(ridgeMin, ridge);
        ridgeMax = std::max(ridgeMax, ridge);
    }
    CHECK(gradientMin >= -1.0F);
    CHECK(gradientMax <= 1.0F);
    // The range is used, not squeezed into a small part of it.
    CHECK(gradientMin < -0.8F);
    CHECK(gradientMax > 0.8F);
    const double meanAbsolute = absoluteSum / static_cast<double>(positions.size());
    CHECK(meanAbsolute > 0.2);
    CHECK(meanAbsolute < 0.3);
    CHECK(fbmMin >= -1.0F);
    CHECK(fbmMax <= 1.0F);
    CHECK(fbmMin < -0.4F);
    CHECK(fbmMax > 0.4F);
    CHECK(ridgeMin >= 0.0F);
    CHECK(ridgeMax <= 1.0F);
    CHECK(ridgeMin < 0.3F);
    CHECK(ridgeMax > 0.9F);
}

TEST_CASE("Extreme fractal parameters stay in range", "[worldgen][noise]")
{
    const std::vector<Vec2> positions = samplePositions(2U, 5000, 500.0F);
    int outside = 0;
    for (const FractalNoiseParams& params :
         {fractalParams(1, 1.0F, 0.0F), fractalParams(kMaxNoiseOctaves, 4.0F, 1.0F),
          fractalParams(kMaxNoiseOctaves, 1.0F, 1.0F), fractalParams(6, 3.7F, 0.95F)}) {
        for (const Vec2 position : positions) {
            const float fractal = fbm(3U, position, params);
            const float ridge = ridgeNoise(3U, position, params);
            if (!(fractal >= -1.0F && fractal <= 1.0F && ridge >= 0.0F && ridge <= 1.0F)) {
                ++outside;
            }
        }
    }
    CHECK(outside == 0);
}

TEST_CASE("domainWarp moves each coordinate by at most its strength", "[worldgen][noise]")
{
    const std::vector<Vec2> positions = samplePositions(3U, 20000, 100.0F);
    int tooFar = 0;
    float largestMove = 0.0F;
    for (const float strength : {0.25F, 1.5F, 4.0F}) {
        for (const Vec2 position : positions) {
            const Vec2 warped = domainWarp(8U, position, strength, fractalParams());
            const float moveX = std::abs(warped.x - position.x);
            const float moveY = std::abs(warped.y - position.y);
            // One float step at 100 is below 1e-5.
            if (moveX > strength + 1.0e-4F || moveY > strength + 1.0e-4F) {
                ++tooFar;
            }
            largestMove = std::max(largestMove, std::max(moveX, moveY) / strength);
        }
    }
    CHECK(tooFar == 0);
    // The warp does move positions by a noticeable part of its strength.
    CHECK(largestMove > 0.4F);
}

TEST_CASE("Noise functions change little over small steps", "[worldgen][noise]")
{
    // The steepest slope found over many samples is below 6 per cell for all of
    // them (ridge noise is steepest); 10 leaves room.
    constexpr float maxSlope = 10.0F;
    const std::vector<Vec2> positions = samplePositions(4U, 20000, 100.0F);
    Pcg32 directions{5U};
    for (const NamedNoise& named : allScalarNoises()) {
        INFO(named.name);
        int jumps = 0;
        for (const Vec2 position : positions) {
            const Vec2 step{directions.nextFloat(-1.0e-3F, 1.0e-3F),
                            directions.nextFloat(-1.0e-3F, 1.0e-3F)};
            const Vec2 next = position + step;
            // The step the float positions actually took.
            const float distance =
                std::max(std::abs(next.x - position.x), std::abs(next.y - position.y)) * 1.4142136F;
            const float change = std::abs(named.noise(next) - named.noise(position));
            // domainWarp returns positions, which move along with the input.
            // The slack covers float rounding of the results (a float step at
            // 100 is 8e-6).
            const float allowed = (maxSlope + 1.0F) * distance + 1.0e-4F;
            if (change > allowed) {
                ++jumps;
            }
        }
        CHECK(jumps == 0);
    }
}

TEST_CASE("Noise functions repeat bit for bit", "[worldgen][noise]")
{
    const std::vector<Vec2> positions = samplePositions(6U, 2000, 300.0F);
    for (const NamedNoise& named : allScalarNoises()) {
        INFO(named.name);
        std::vector<std::uint32_t> first;
        std::vector<std::uint32_t> second;
        for (const Vec2 position : positions) {
            first.push_back(bitsOf(named.noise(position)));
        }
        for (const Vec2 position : positions) {
            second.push_back(bitsOf(named.noise(position)));
        }
        CHECK(first == second);
    }
}

TEST_CASE("Different seeds give different noise", "[worldgen][noise]")
{
    const std::vector<Vec2> positions = samplePositions(7U, 10000, 500.0F);
    const FractalNoiseParams params = fractalParams();
    double gradientDifference = 0.0;
    double fbmDifference = 0.0;
    double ridgeDifference = 0.0;
    for (const Vec2 position : positions) {
        gradientDifference += static_cast<double>(
            std::abs(gradientNoise(1U, position) - gradientNoise(2U, position)));
        fbmDifference +=
            static_cast<double>(std::abs(fbm(1U, position, params) - fbm(2U, position, params)));
        ridgeDifference += static_cast<double>(
            std::abs(ridgeNoise(1U, position, params) - ridgeNoise(2U, position, params)));
    }
    const auto count = static_cast<double>(positions.size());
    // Independent fields differ by about a third of their typical value; a
    // shared or shifted pattern would give much less.
    CHECK(gradientDifference / count > 0.2);
    CHECK(fbmDifference / count > 0.1);
    CHECK(ridgeDifference / count > 0.1);
}

TEST_CASE("fbm octaves do not share lattice points", "[worldgen][noise]")
{
    // With lacunarity 2, every octave without its offset would have a lattice
    // point at each integer position, and fbm would be 0 there.
    int nearZero = 0;
    int total = 0;
    for (int y = 0; y < 50; ++y) {
        for (int x = 0; x < 50; ++x) {
            const Vec2 position{static_cast<float>(x), static_cast<float>(y)};
            if (std::abs(fbm(13U, position, fractalParams())) < 0.01F) {
                ++nearZero;
            }
            ++total;
        }
    }
    CHECK(nearZero < total / 10);
}

TEST_CASE("Fractal parameters are clamped to their ranges", "[worldgen][noise]")
{
    const std::vector<Vec2> positions = samplePositions(9U, 500, 200.0F);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    struct Equivalent {
        FractalNoiseParams given;
        FractalNoiseParams meant;
    };
    const std::vector<Equivalent> cases{
        {fractalParams(0), fractalParams(1)},
        {fractalParams(-5), fractalParams(1)},
        {fractalParams(100), fractalParams(kMaxNoiseOctaves)},
        {fractalParams(4, 10.0F), fractalParams(4, 4.0F)},
        {fractalParams(4, infinity), fractalParams(4, 4.0F)},
        {fractalParams(4, 0.5F), fractalParams(4, 1.0F)},
        {fractalParams(4, nan), fractalParams(4, 1.0F)},
        {fractalParams(4, 2.0F, 2.0F), fractalParams(4, 2.0F, 1.0F)},
        {fractalParams(4, 2.0F, -1.0F), fractalParams(4, 2.0F, 0.0F)},
        {fractalParams(4, 2.0F, nan), fractalParams(4, 2.0F, 0.0F)},
        // One octave ignores lacunarity and persistence; persistence 0 leaves
        // only the first octave.
        {fractalParams(1, 3.0F, 0.9F), fractalParams(1)},
        {fractalParams(6, 2.0F, 0.0F), fractalParams(1)},
    };
    int different = 0;
    for (const Equivalent& equivalent : cases) {
        for (const Vec2 position : positions) {
            if (bitsOf(fbm(4U, position, equivalent.given)) !=
                    bitsOf(fbm(4U, position, equivalent.meant)) ||
                bitsOf(ridgeNoise(4U, position, equivalent.given)) !=
                    bitsOf(ridgeNoise(4U, position, equivalent.meant))) {
                ++different;
            }
        }
    }
    CHECK(different == 0);

    // More octaves do change the result.
    int same = 0;
    for (const Vec2 position : positions) {
        if (bitsOf(fbm(4U, position, fractalParams(1))) ==
            bitsOf(fbm(4U, position, fractalParams(2)))) {
            ++same;
        }
    }
    CHECK(same < 50);
}

TEST_CASE("domainWarp strength is clamped and 0 leaves positions unchanged", "[worldgen][noise]")
{
    const std::vector<Vec2> positions = samplePositions(10U, 500, 200.0F);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    int changed = 0;
    int differentFromMax = 0;
    for (const Vec2 position : positions) {
        for (const float none : {0.0F, -0.0F, -2.0F, nan}) {
            const Vec2 warped = domainWarp(5U, position, none, fractalParams());
            if (bitsOf(warped.x) != bitsOf(position.x) || bitsOf(warped.y) != bitsOf(position.y)) {
                ++changed;
            }
        }
        const Vec2 huge = domainWarp(5U, position, 1.0e9F, fractalParams());
        const Vec2 largest =
            domainWarp(5U, position, engine::worldgen::kMaxWarpStrength, fractalParams());
        if (bitsOf(huge.x) != bitsOf(largest.x) || bitsOf(huge.y) != bitsOf(largest.y)) {
            ++differentFromMax;
        }
    }
    CHECK(changed == 0);
    CHECK(differentFromMax == 0);
}

TEST_CASE("Samples outside the noise domain are 0", "[worldgen][noise]")
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    const float huge = 1.0e17F; // above 2^52
    for (const Vec2 position : {Vec2{nan, 0.5F}, Vec2{0.5F, nan}, Vec2{infinity, 0.5F},
                                Vec2{0.5F, -infinity}, Vec2{huge, 0.5F}, Vec2{0.5F, -huge}}) {
        CHECK(gradientNoise(1U, position) == 0.0F);
        CHECK(fbm(1U, position, fractalParams()) == 0.0F);
        // Every octave of ridge noise folds 0 into 1.
        CHECK(ridgeNoise(1U, position, fractalParams()) == 1.0F);
    }

    // Inside the domain the cell index wraps every 2^32 cells, so the pattern
    // repeats with that period (float steps are 512 cells there, so the
    // positions lie on lattice lines).
    for (const float y : {0.25F, 0.5F, 0.75F}) {
        CHECK(gradientNoise(1U, Vec2{0x1p32F, y}) == gradientNoise(1U, Vec2{0.0F, y}));
        CHECK(gradientNoise(1U, Vec2{y, -0x1p33F}) == gradientNoise(1U, Vec2{y, 0.0F}));
    }
}
