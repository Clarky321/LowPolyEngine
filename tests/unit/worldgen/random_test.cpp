// Deterministic randomness for world generation (ADR 0003, Amendment 1).
//
// Golden values for SplitMix64 (seed 0) and PCG32 (seed 42, sequence 54) are the
// published outputs of their reference implementations. All other golden values
// were computed with an independent implementation of the formulas documented in
// engine/worldgen/Random.hpp. They are integers or exact float bit patterns and
// must match on every platform and configuration in CI.
#include "engine/worldgen/Random.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <set>
#include <vector>

using engine::worldgen::deriveSeed;
using engine::worldgen::mix64;
using engine::worldgen::Pcg32;
using engine::worldgen::RandomStream;

namespace {

constexpr std::uint64_t kMax64 = std::numeric_limits<std::uint64_t>::max();
constexpr std::uint32_t kMax32 = std::numeric_limits<std::uint32_t>::max();
constexpr std::int32_t kMinInt = std::numeric_limits<std::int32_t>::min();
constexpr std::int32_t kMaxInt = std::numeric_limits<std::int32_t>::max();

constexpr std::array kAllStreams{RandomStream::IslandLayout, RandomStream::IslandShape,
                                 RandomStream::Terrain,      RandomStream::Vegetation,
                                 RandomStream::Clouds,       RandomStream::IslandPalette};

// The demo program of the PCG reference library seeds with pcg32_srandom_r(42, 54).
Pcg32 referenceGenerator()
{
    return Pcg32{42U, 54U};
}

std::uint32_t bitsOf(float value)
{
    return std::bit_cast<std::uint32_t>(value);
}

// True if `after` is `before` advanced by exactly `count` outputs.
bool advancedBy(Pcg32 before, const Pcg32& after, int count)
{
    for (int i = 0; i < count; ++i) {
        static_cast<void>(before.next());
    }
    return before == after;
}

} // namespace

TEST_CASE("mix64 is the SplitMix64 finalizer", "[worldgen][random]")
{
    CHECK(mix64(0U) == 0U);
    CHECK(mix64(1U) == 0x5692161D100B05E5ULL);
    CHECK(mix64(kMax64) == 0xB4D055FCF2CBBD7BULL);
    CHECK(mix64(0x0123456789ABCDEFULL) == 0xB2C058E4EBB5112CULL);

    // SplitMix64 seeded with 0: the state grows by the golden gamma, each output
    // is mix64(state).
    std::uint64_t state = 0;
    for (const std::uint64_t expected :
         {0xE220A8397B1DCDAFULL, 0x6E789E6AA1B965F4ULL, 0x06C45D188009454FULL}) {
        state += 0x9E3779B97F4A7C15ULL;
        CHECK(mix64(state) == expected);
    }
}

TEST_CASE("Every input bit of mix64 changes about half of the output bits", "[worldgen][random]")
{
    constexpr int inputs = 256;
    constexpr std::uint64_t step = 0x9E3779B97F4A7C15ULL;
    std::array<int, 64> flipsPerBit{};
    std::uint64_t value = 0;
    for (int i = 0; i < inputs; ++i) {
        value += step;
        for (std::size_t bit = 0; bit < flipsPerBit.size(); ++bit) {
            const std::uint64_t flipped = value ^ (std::uint64_t{1} << bit);
            flipsPerBit[bit] += std::popcount(mix64(value) ^ mix64(flipped));
        }
    }
    int total = 0;
    int weakest = std::numeric_limits<int>::max();
    int strongest = 0;
    for (const int flips : flipsPerBit) {
        total += flips;
        weakest = std::min(weakest, flips);
        strongest = std::max(strongest, flips);
    }
    // 32 changed bits on average; per input bit the average over 256 inputs has a
    // standard deviation of 0.25 bits.
    const double average = static_cast<double>(total) / (64.0 * inputs);
    CHECK(average > 31.5);
    CHECK(average < 32.5);
    CHECK(weakest > 29 * inputs);
    CHECK(strongest < 35 * inputs);
}

TEST_CASE("RandomStream values are fixed", "[worldgen][random]")
{
    CHECK(static_cast<std::uint64_t>(RandomStream::IslandLayout) == 1U);
    CHECK(static_cast<std::uint64_t>(RandomStream::IslandShape) == 2U);
    CHECK(static_cast<std::uint64_t>(RandomStream::Terrain) == 3U);
    CHECK(static_cast<std::uint64_t>(RandomStream::Vegetation) == 4U);
    CHECK(static_cast<std::uint64_t>(RandomStream::Clouds) == 5U);
    CHECK(static_cast<std::uint64_t>(RandomStream::IslandPalette) == 6U);
}

TEST_CASE("deriveSeed matches its golden values", "[worldgen][random]")
{
    CHECK(deriveSeed(0U, RandomStream::IslandLayout, 0U, 0U) == 0xD1C0270687984B37ULL);
    CHECK(deriveSeed(1U, RandomStream::IslandLayout, 0U, 1U) == 0x2DE5A4E32AE6D264ULL);
    CHECK(deriveSeed(0x0123456789ABCDEFULL, RandomStream::IslandShape, 7U, 1U) ==
          0x88D9AEE4E1E1FC62ULL);
    CHECK(deriveSeed(42U, RandomStream::Terrain, 3U, 2U) == 0x34A1525F8255B947ULL);
    CHECK(deriveSeed(kMax64, RandomStream::Clouds, kMax64, kMax32) == 0x8BA0188EE6459FE2ULL);
}

TEST_CASE("Different streams, indices and versions give different seeds", "[worldgen][random]")
{
    for (const std::uint64_t master : {std::uint64_t{0}, std::uint64_t{42}, kMax64}) {
        std::set<std::uint64_t> seeds;
        std::size_t count = 0;
        for (const RandomStream stream : kAllStreams) {
            for (std::uint64_t index = 0; index < 1000; ++index) {
                for (std::uint32_t version = 0; version < 3; ++version) {
                    seeds.insert(deriveSeed(master, stream, index, version));
                    ++count;
                }
            }
        }
        CHECK(seeds.size() == count);
    }

    // The arguments are not interchangeable.
    CHECK(deriveSeed(5U, RandomStream::Terrain, 1U, 2U) !=
          deriveSeed(5U, RandomStream::Terrain, 2U, 1U));
    CHECK(deriveSeed(3U, RandomStream::IslandLayout, 0U, 1U) !=
          deriveSeed(1U, RandomStream::Terrain, 0U, 1U));
    CHECK(deriveSeed(7U, RandomStream::Clouds, 0U, 0U) !=
          deriveSeed(0U, RandomStream::Clouds, 7U, 0U));
}

TEST_CASE("Pcg32 reproduces the reference implementation", "[worldgen][random]")
{
    Pcg32 rng = referenceGenerator();
    for (const std::uint32_t expected :
         {0xA15C02B7U, 0x7B47F409U, 0xBA1D3330U, 0x83D2F293U, 0xBFA4784BU, 0xCBED606EU}) {
        CHECK(rng.next() == expected);
    }
}

TEST_CASE("Pcg32 with the default sequence matches its golden values", "[worldgen][random]")
{
    Pcg32 zero{0U};
    for (const std::uint32_t expected : {0xE823A24EU, 0x7A7ECBD9U, 0x89FD6C06U, 0xAE646AA8U}) {
        CHECK(zero.next() == expected);
    }

    const std::uint64_t seed = deriveSeed(42U, RandomStream::IslandLayout, 0U, 1U);
    CHECK(seed == 0x71818AD83CBA8F05ULL);
    Pcg32 derived{seed};
    for (const std::uint32_t expected : {0x30D70B14U, 0x298D1D89U, 0xE6D0AAE2U, 0x7E23CEC3U}) {
        CHECK(derived.next() == expected);
    }

    CHECK(Pcg32{7U} == Pcg32{7U, Pcg32::kDefaultSequence});
}

TEST_CASE("Pcg32 is a value type", "[worldgen][random]")
{
    Pcg32 original{123U};
    for (int i = 0; i < 10; ++i) {
        static_cast<void>(original.next());
    }
    Pcg32 copy = original;
    CHECK(copy == original);

    std::vector<std::uint32_t> fromOriginal;
    std::vector<std::uint32_t> fromCopy;
    for (int i = 0; i < 100; ++i) {
        fromOriginal.push_back(original.next());
    }
    for (int i = 0; i < 100; ++i) {
        fromCopy.push_back(copy.next());
    }
    CHECK(fromOriginal == fromCopy);
    CHECK(copy == original);
    CHECK_FALSE(Pcg32{123U} == Pcg32{124U});
}

TEST_CASE("Pcg32 sequences select independent streams", "[worldgen][random]")
{
    Pcg32 first{5U, 1U};
    Pcg32 second{5U, 2U};
    int equal = 0;
    for (int i = 0; i < 100; ++i) {
        if (first.next() == second.next()) {
            ++equal;
        }
    }
    CHECK(equal == 0);

    // Only the low 63 bits of the sequence are used.
    CHECK(Pcg32{5U, 1U} == Pcg32{5U, 1U | (std::uint64_t{1} << 63U)});
    CHECK_FALSE(Pcg32{5U, 1U} == Pcg32{5U, 3U});
}

TEST_CASE("Different seeds give different sequences", "[worldgen][random]")
{
    std::set<std::uint64_t> firstOutputs;
    for (std::uint64_t seed = 0; seed < 1000; ++seed) {
        Pcg32 rng{seed};
        const std::uint64_t high = rng.next();
        const std::uint64_t low = rng.next();
        firstOutputs.insert((high << 32U) | low);
    }
    CHECK(firstOutputs.size() == 1000U);
}

TEST_CASE("nextFloat01 is the exact top 24 bits of next()", "[worldgen][random]")
{
    Pcg32 rng = referenceGenerator();
    CHECK(rng.nextFloat01() == 0xA15C02p-24F);
    CHECK(rng.nextFloat01() == 0x7B47F4p-24F);
    CHECK(rng.nextFloat01() == 0xBA1D33p-24F);
    CHECK(rng.nextFloat01() == 0x83D2F2p-24F);
    CHECK(rng.nextFloat01() == 0xBFA478p-24F);
    CHECK(rng.nextFloat01() == 0xCBED60p-24F);

    // The largest possible result is exact and below 1.
    CHECK(0xFFFFFFp-24F < 1.0F);
}

TEST_CASE("nextFloat01 stays in [0, 1) on a large sample", "[worldgen][random]")
{
    Pcg32 rng{2026U};
    constexpr int samples = 1000000;
    int outside = 0;
    int notOnGrid = 0;
    double sum = 0.0;
    for (int i = 0; i < samples; ++i) {
        const float value = rng.nextFloat01();
        if (!(value >= 0.0F && value < 1.0F)) {
            ++outside;
        }
        const float scaled = value * 16777216.0F; // 2^24, exact
        if (std::floor(scaled) != scaled) {
            ++notOnGrid;
        }
        sum += static_cast<double>(value);
    }
    CHECK(outside == 0);
    CHECK(notOnGrid == 0);
    // The mean of a million uniform values has a standard deviation of 0.0003.
    const double mean = sum / samples;
    CHECK(mean > 0.498);
    CHECK(mean < 0.502);
}

TEST_CASE("nextBounded matches its golden values", "[worldgen][random]")
{
    Pcg32 rng = referenceGenerator();
    for (const std::uint32_t expected : {3U, 2U, 4U, 3U, 4U, 4U, 4U, 3U, 5U, 5U}) {
        CHECK(rng.nextBounded(6U) == expected);
    }

    // 2^31 + 1 rejects almost half of all draws: 8 results take 14 outputs here.
    const Pcg32 start = referenceGenerator();
    rng = start;
    for (const std::uint32_t expected : {0x3DA3FA04U, 0x5D0E9998U, 0x65F6B037U, 0x730F982DU,
                                         0x7C9C25C8U, 0x0EE01AFCU, 0x76BC3413U, 0x1C11220EU}) {
        CHECK(rng.nextBounded(0x80000001U) == expected);
    }
    CHECK(advancedBy(start, rng, 14));
    CHECK(rng.next() == 0x2BA113D7U);
}

TEST_CASE("nextBounded handles the ends of its range", "[worldgen][random]")
{
    Pcg32 rng{77U};

    Pcg32 before = rng;
    int nonZero = 0;
    for (int i = 0; i < 1000; ++i) {
        if (rng.nextBounded(1U) != 0U) {
            ++nonZero;
        }
    }
    CHECK(nonZero == 0);
    CHECK(advancedBy(before, rng, 1000));

    // bound 0 is an empty range: 0, one output consumed.
    before = rng;
    CHECK(rng.nextBounded(0U) == 0U);
    CHECK(advancedBy(before, rng, 1));

    // A power of two never rejects.
    before = rng;
    int outside = 0;
    for (int i = 0; i < 1000; ++i) {
        if (rng.nextBounded(0x80000000U) >= 0x80000000U) {
            ++outside;
        }
    }
    CHECK(outside == 0);
    CHECK(advancedBy(before, rng, 1000));

    outside = 0;
    for (int i = 0; i < 10000; ++i) {
        if (rng.nextBounded(kMax32) >= kMax32) {
            ++outside;
        }
    }
    CHECK(outside == 0);
}

TEST_CASE("nextBounded covers its range without bias", "[worldgen][random]")
{
    Pcg32 rng{31415U};

    // Chi-square test of a die. The sample is fixed by the seed, so the result
    // is the same in every run; 20.52 is the 0.1 % critical value for 5 degrees
    // of freedom.
    constexpr int rolls = 600000;
    std::array<int, 6> faces{};
    for (int i = 0; i < rolls; ++i) {
        ++faces[rng.nextBounded(6U)];
    }
    const double expected = rolls / 6.0;
    double chiSquare = 0.0;
    for (const int count : faces) {
        const double difference = static_cast<double>(count) - expected;
        chiSquare += difference * difference / expected;
    }
    CHECK(chiSquare < 20.52);

    std::array<int, 10> digits{};
    for (int i = 0; i < 1000; ++i) {
        ++digits[rng.nextBounded(10U)];
    }
    for (const int count : digits) {
        CHECK(count > 0);
    }

    // bound = 3 * 2^30. A plain next() % bound would put half of all results
    // into the lowest third; an unbiased draw puts a third into each.
    constexpr int draws = 300000;
    std::array<int, 3> thirds{};
    for (int i = 0; i < draws; ++i) {
        ++thirds[rng.nextBounded(0xC0000000U) / 0x40000000U];
    }
    for (const int count : thirds) {
        const double fraction = static_cast<double>(count) / draws;
        CHECK(fraction > 1.0 / 3.0 - 0.005);
        CHECK(fraction < 1.0 / 3.0 + 0.005);
    }
}

TEST_CASE("nextFloat matches its golden values", "[worldgen][random]")
{
    Pcg32 rng = referenceGenerator();
    for (const std::uint32_t expected : {0x4026CC0AU, 0xBEBCC1E0U, 0x40914900U, 0x3E98F5D0U}) {
        CHECK(bitsOf(rng.nextFloat(-10.0F, 10.0F)) == expected);
    }

    rng = referenceGenerator();
    for (const std::uint32_t expected : {0x441DA9C4U, 0x43F0F920U, 0x4435D4E2U, 0x4400D3C3U}) {
        CHECK(bitsOf(rng.nextFloat(0.5F, 1000.25F)) == expected);
    }
}

TEST_CASE("nextFloat stays in [min, max)", "[worldgen][random]")
{
    Pcg32 rng{8U};
    int outside = 0;
    for (int i = 0; i < 100000; ++i) {
        const float value = rng.nextFloat(-3.5F, 7.25F);
        if (!(value >= -3.5F && value < 7.25F)) {
            ++outside;
        }
    }
    CHECK(outside == 0);

    // max - min overflows in float but not in double.
    const float largest = std::numeric_limits<float>::max();
    outside = 0;
    for (int i = 0; i < 10000; ++i) {
        const float value = rng.nextFloat(-largest, largest);
        if (!(std::isfinite(value) && value >= -largest && value < largest)) {
            ++outside;
        }
    }
    CHECK(outside == 0);

    // The only float in [1, 1 + ulp) is 1: rounding that reaches max is moved
    // below it.
    const float justAboveOne = std::nextafter(1.0F, 2.0F);
    int notOne = 0;
    for (int i = 0; i < 1000; ++i) {
        if (rng.nextFloat(1.0F, justAboveOne) != 1.0F) {
            ++notOne;
        }
    }
    CHECK(notOne == 0);
}

TEST_CASE("nextFloat consumes one output even for empty or invalid ranges", "[worldgen][random]")
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    struct Range {
        float min;
        float max;
    };
    Pcg32 rng{99U};
    for (const Range range : {Range{2.0F, 2.0F}, Range{3.0F, 1.0F}, Range{nan, 1.0F},
                              Range{0.0F, nan}, Range{0.0F, infinity}, Range{-infinity, 0.0F}}) {
        const Pcg32 before = rng;
        CHECK(bitsOf(rng.nextFloat(range.min, range.max)) == bitsOf(range.min));
        CHECK(advancedBy(before, rng, 1));
    }

    const Pcg32 before = rng;
    static_cast<void>(rng.nextFloat(0.0F, 1.0F));
    CHECK(advancedBy(before, rng, 1));
}

TEST_CASE("nextInt matches its golden values", "[worldgen][random]")
{
    Pcg32 rng = referenceGenerator();
    for (const std::int32_t expected : {1, 0, 2, 0, 2, 2, 2, 0}) {
        CHECK(rng.nextInt(-3, 3) == expected);
    }

    // The whole int32 range is min plus one full output.
    const Pcg32 start = referenceGenerator();
    rng = start;
    CHECK(rng.nextInt(kMinInt, kMaxInt) == 559678135);
    CHECK(rng.nextInt(kMinInt, kMaxInt) == -79170551);
    CHECK(advancedBy(start, rng, 2));
}

TEST_CASE("nextInt covers [min, max] and handles collapsed ranges", "[worldgen][random]")
{
    Pcg32 rng{4242U};
    std::array<int, 7> seen{};
    int outside = 0;
    for (int i = 0; i < 10000; ++i) {
        const std::int32_t value = rng.nextInt(-3, 3);
        if (value < -3 || value > 3) {
            ++outside;
        } else {
            ++seen[static_cast<std::size_t>(value + 3)];
        }
    }
    CHECK(outside == 0);
    for (const int count : seen) {
        CHECK(count > 0);
    }

    Pcg32 before = rng;
    CHECK(rng.nextInt(5, 5) == 5);
    CHECK(advancedBy(before, rng, 1));

    before = rng;
    CHECK(rng.nextInt(5, -5) == 5);
    CHECK(advancedBy(before, rng, 1));

    int negative = 0;
    int positive = 0;
    for (int i = 0; i < 1000; ++i) {
        const std::int32_t value = rng.nextInt(kMinInt, kMaxInt);
        if (value < 0) {
            ++negative;
        } else {
            ++positive;
        }
    }
    CHECK(negative > 400);
    CHECK(positive > 400);
}

TEST_CASE("nextBool matches its golden values", "[worldgen][random]")
{
    Pcg32 rng = referenceGenerator();
    for (const bool expected : {false, true, false, true, false, false}) {
        CHECK(rng.nextBool(0.6F) == expected);
    }

    Pcg32 withDefault{17U};
    Pcg32 withHalf{17U};
    int different = 0;
    for (int i = 0; i < 100; ++i) {
        if (withDefault.nextBool() != withHalf.nextBool(0.5F)) {
            ++different;
        }
    }
    CHECK(different == 0);
}

TEST_CASE("nextBool follows its probability and consumes one output", "[worldgen][random]")
{
    Pcg32 rng{555U};
    const float nan = std::numeric_limits<float>::quiet_NaN();
    for (const float never : {0.0F, -1.0F, nan}) {
        const Pcg32 before = rng;
        int trueCount = 0;
        for (int i = 0; i < 1000; ++i) {
            if (rng.nextBool(never)) {
                ++trueCount;
            }
        }
        CHECK(trueCount == 0);
        CHECK(advancedBy(before, rng, 1000));
    }
    for (const float always : {1.0F, 2.0F}) {
        int falseCount = 0;
        for (int i = 0; i < 1000; ++i) {
            if (!rng.nextBool(always)) {
                ++falseCount;
            }
        }
        CHECK(falseCount == 0);
    }

    // Standard deviation of the fraction over 100000 draws: about 0.0014.
    constexpr int draws = 100000;
    int quarter = 0;
    for (int i = 0; i < draws; ++i) {
        if (rng.nextBool(0.25F)) {
            ++quarter;
        }
    }
    const double fraction = static_cast<double>(quarter) / draws;
    CHECK(fraction > 0.24);
    CHECK(fraction < 0.26);
}
