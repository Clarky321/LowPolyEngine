#pragma once

// Size arithmetic of MeshData (internal). Kept apart from MeshData.cpp so the
// limits can be unit-tested with plain numbers instead of meshes with billions
// of vertices.

#include <cstddef>
#include <cstdint>
#include <limits>

namespace engine::geometry::detail {

// 32-bit indices address vertices 0 .. 4294967294, so one mesh holds at most
// 4294967295 vertices.
inline constexpr std::size_t kMaxVertexCount = std::numeric_limits<std::uint32_t>::max();

// True if a mesh with `existing` vertices can take `added` more without
// exceeding kMaxVertexCount. The sum itself is never computed, so it cannot
// overflow std::size_t.
[[nodiscard]] constexpr bool fitsVertexCount(std::size_t existing, std::size_t added) noexcept
{
    return existing <= kMaxVertexCount && added <= kMaxVertexCount - existing;
}

// True if `index + offset` fits in 32 bits (no unsigned wrap-around).
[[nodiscard]] constexpr bool fitsShiftedIndex(std::uint32_t index, std::uint32_t offset) noexcept
{
    return index <= std::numeric_limits<std::uint32_t>::max() - offset;
}

} // namespace engine::geometry::detail
