#include "engine/worldgen/Random.hpp"

#include <cmath>
#include <cstdint>
#include <limits>

// ADR 0003: generation code is never compiled with fast-math or with
// floating-point contraction into fused multiply-add, which would change float
// results between compilers and CPUs. GCC and Clang get -ffp-contract=off from
// engine/CMakeLists.txt; MSVC's default /fp:precise does not contract.
#if defined(__FAST_MATH__) || defined(_M_FP_FAST) || defined(_M_FP_CONTRACT)
#error "worldgen must be compiled without fast-math and floating-point contraction (ADR 0003)"
#endif

namespace engine::worldgen {

namespace {

// 2^64 divided by the golden ratio, odd: SplitMix64's increment.
constexpr std::uint64_t kGoldenGamma = 0x9E3779B97F4A7C15ULL;

// PCG's 64-bit LCG multiplier (reference implementation).
constexpr std::uint64_t kPcgMultiplier = 6364136223846793005ULL;

std::uint64_t fold(std::uint64_t hash, std::uint64_t value)
{
    return mix64((hash ^ value) + kGoldenGamma);
}

} // namespace

std::uint64_t mix64(std::uint64_t value)
{
    value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    value = (value ^ (value >> 27U)) * 0x94D049BB133111EBULL;
    return value ^ (value >> 31U);
}

std::uint64_t deriveSeed(std::uint64_t masterSeed, RandomStream stream, std::uint64_t index,
                         std::uint32_t generatorVersion)
{
    std::uint64_t hash = fold(0U, masterSeed);
    hash = fold(hash, static_cast<std::uint64_t>(stream));
    hash = fold(hash, index);
    return fold(hash, generatorVersion);
}

Pcg32::Pcg32(std::uint64_t seed, std::uint64_t sequence)
    : state_{0U}, increment_{(sequence << 1U) | 1U}
{
    static_cast<void>(next());
    state_ += seed;
    static_cast<void>(next());
}

std::uint32_t Pcg32::next()
{
    const std::uint64_t old = state_;
    state_ = old * kPcgMultiplier + increment_;
    const auto xorShifted = static_cast<std::uint32_t>(((old >> 18U) ^ old) >> 27U);
    const auto rotation = static_cast<std::uint32_t>(old >> 59U);
    return (xorShifted >> rotation) | (xorShifted << ((32U - rotation) & 31U));
}

float Pcg32::nextFloat01()
{
    // A 24-bit integer is exact in a float, and so is its product with 2^-24.
    return static_cast<float>(next() >> 8U) * 0x1.0p-24F;
}

std::uint32_t Pcg32::nextBounded(std::uint32_t bound)
{
    if (bound == 0U) {
        static_cast<void>(next());
        return 0U;
    }
    // The high 32 bits of next() * bound are uniform in [0, bound) once the
    // 2^32 mod bound low values that would bias them are rejected. The modulo
    // is only computed when a rejection is possible at all.
    std::uint64_t product = static_cast<std::uint64_t>(next()) * bound;
    auto low = static_cast<std::uint32_t>(product);
    if (low < bound) {
        const std::uint32_t threshold =
            (std::numeric_limits<std::uint32_t>::max() - bound + 1U) % bound;
        while (low < threshold) {
            product = static_cast<std::uint64_t>(next()) * bound;
            low = static_cast<std::uint32_t>(product);
        }
    }
    return static_cast<std::uint32_t>(product >> 32U);
}

float Pcg32::nextFloat(float min, float max)
{
    const float unit = nextFloat01();
    if (!(std::isfinite(min) && std::isfinite(max) && min < max)) {
        return min;
    }
    // In double, max - min cannot overflow for finite floats. Rounding never
    // goes below min, but can reach max; the largest float below max is still
    // at least min, because min < max.
    const double minimum = static_cast<double>(min);
    const double span = static_cast<double>(max) - minimum;
    const auto value = static_cast<float>(minimum + span * static_cast<double>(unit));
    return value < max ? value : std::nextafter(max, min);
}

std::int32_t Pcg32::nextInt(std::int32_t min, std::int32_t max)
{
    if (max < min) {
        max = min;
    }
    // Up to 2^32 values, so the count needs 64 bits.
    const auto count = static_cast<std::uint64_t>(static_cast<std::int64_t>(max) -
                                                  static_cast<std::int64_t>(min)) +
                       1U;
    const std::uint32_t offset = count > std::numeric_limits<std::uint32_t>::max()
                                     ? next()
                                     : nextBounded(static_cast<std::uint32_t>(count));
    return static_cast<std::int32_t>(static_cast<std::int64_t>(min) +
                                     static_cast<std::int64_t>(offset));
}

bool Pcg32::nextBool(float probability)
{
    // nextFloat01() is in [0, 1): never below 0 and always below 1.
    return nextFloat01() < probability;
}

} // namespace engine::worldgen
