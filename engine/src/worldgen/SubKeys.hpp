#pragma once

// Keys for the separate uses of one seed (ADR 0014): key i is the i-th output
// of the seed's SplitMix64 sequence. Each use has a fixed index, so a new use
// never changes the keys of existing ones.

#include "engine/worldgen/Random.hpp"

#include <cstdint>

namespace engine::worldgen::detail {

inline std::uint64_t subKey(std::uint64_t seed, std::uint64_t index)
{
    return mix64(seed + (index + 1U) * kSplitMix64Gamma);
}

} // namespace engine::worldgen::detail
