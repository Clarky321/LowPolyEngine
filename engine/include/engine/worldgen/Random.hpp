#pragma once

// Deterministic randomness for world generation (ADR 0003 and its Amendment 1).
//
// Everything here is project code with fixed formulas, so the same inputs give
// the same results with every compiler and standard library: integer results
// bit for bit, float results too, because each one comes from a few correctly
// rounded IEEE operations compiled without contraction or fast-math. The
// formulas, the RandomStream values and the number of generator outputs each
// call consumes are part of the save-compatibility contract: changing any of
// them changes generated worlds and requires a generator version bump.

#include <cstdint>

namespace engine::worldgen {

// Independent random streams. Each use of randomness in the generator derives
// its seeds from its own stream, so adding a feature never reshuffles the
// random sequences of existing ones. Values are fixed forever and never reused;
// a new use gets the next free value in the step that introduces it.
enum class RandomStream : std::uint64_t {
    IslandLayout = 1,
    IslandShape = 2,
    Terrain = 3,
    Vegetation = 4,
    Clouds = 5,
};

// SplitMix64's increment: 2^64 divided by the golden ratio, odd. Adding it to a
// state again and again and mixing each state with mix64 gives the SplitMix64
// sequence of that state.
inline constexpr std::uint64_t kSplitMix64Gamma = 0x9E3779B97F4A7C15ULL;

// SplitMix64 finalizer: a bijective 64-bit mix in which every input bit
// affects every output bit. mix64(0) is 0.
[[nodiscard]] std::uint64_t mix64(std::uint64_t value);

// Seed of one random sequence, derived from the world's master seed, a stream,
// an index within the stream (an island number, for example) and the generator
// version. With fold(h, x) = mix64((h ^ x) + kSplitMix64Gamma):
//
//   deriveSeed = fold(fold(fold(fold(0, masterSeed), stream), index), generatorVersion)
//
// Every fold is a bijection of either argument, so changing any single input
// always changes the seed. fold(0, m) equals the first output of SplitMix64
// seeded with m.
[[nodiscard]] std::uint64_t deriveSeed(std::uint64_t masterSeed, RandomStream stream,
                                       std::uint64_t index, std::uint32_t generatorVersion);

// PCG32 (XSH-RR variant: 64-bit state, 32-bit output) by M. E. O'Neill, as in
// the reference implementation pcg32_random_r. A value type: copies continue
// the same sequence independently, and there is no global or thread-local
// state. Use one instance per derived seed.
//
// Number of generator outputs each call consumes (part of the contract):
// next, nextFloat01, nextFloat and nextBool exactly one; nextBounded and
// nextInt one, plus one per rejected draw (probability below bound / 2^32).
class Pcg32 {
public:
    // Default stream: increment 1442695040888963407, the default of pcg32 in the
    // reference C++ library (pcg-cpp).
    static constexpr std::uint64_t kDefaultSequence = 0x0A02BDBF7BB3C0A7ULL;

    // Seeds like the reference pcg32_srandom_r(seed, sequence). The sequence
    // selects one of 2^63 distinct streams; only its low 63 bits are used.
    explicit Pcg32(std::uint64_t seed, std::uint64_t sequence = kDefaultSequence);

    // Next 32-bit output.
    [[nodiscard]] std::uint32_t next();

    // Uniform float in [0, 1): the top 24 bits of next() times 2^-24, exact.
    [[nodiscard]] float nextFloat01();

    // Uniform integer in [0, bound), without modulo bias (Lemire's multiply-shift
    // method with rejection; integer-only). bound 0 is an empty range: the
    // result is 0.
    [[nodiscard]] std::uint32_t nextBounded(std::uint32_t bound);

    // Uniform float in [min, max), computed in double from nextFloat01() and
    // rounded down to below max where float rounding would reach it. Returns
    // min unchanged when min and max are not finite or min >= max.
    [[nodiscard]] float nextFloat(float min, float max);

    // Uniform integer in [min, max], both ends included; the whole int32 range
    // works. max below min counts as max == min, so the result is min.
    [[nodiscard]] std::int32_t nextInt(std::int32_t min, std::int32_t max);

    // True with the given probability: nextFloat01() < probability. Always
    // false for probability <= 0 or NaN, always true for probability >= 1.
    [[nodiscard]] bool nextBool(float probability = 0.5F);

    friend bool operator==(const Pcg32&, const Pcg32&) = default;

private:
    std::uint64_t state_;
    std::uint64_t increment_;
};

} // namespace engine::worldgen
