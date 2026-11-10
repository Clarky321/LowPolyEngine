#include "engine/geometry/Primitives.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace engine::geometry {

namespace {

bool isPositive(float value) noexcept
{
    return std::isfinite(value) && value > 0.0F;
}

// Adds a quad given counter-clockwise (seen from its front) as two triangles.
void addQuad(MeshData& mesh, const math::Vec3& a, const math::Vec3& b, const math::Vec3& c,
             const math::Vec3& d, const math::Vec4& color)
{
    const auto first = static_cast<std::uint32_t>(mesh.positions.size());
    mesh.positions.insert(mesh.positions.end(), {a, b, c, d});
    mesh.colors.insert(mesh.colors.end(), 4, color);
    mesh.indices.insert(mesh.indices.end(),
                        {first, first + 1, first + 2, first, first + 2, first + 3});
}

void addTriangle(MeshData& mesh, const math::Vec3& a, const math::Vec3& b, const math::Vec3& c,
                 const math::Vec4& color)
{
    const auto first = static_cast<std::uint32_t>(mesh.positions.size());
    mesh.positions.insert(mesh.positions.end(), {a, b, c});
    mesh.colors.insert(mesh.colors.end(), 3, color);
    mesh.indices.insert(mesh.indices.end(), {first, first + 1, first + 2});
}

// A box face as its outward normal n and two in-plane axes with u x v = n, so
// the corners -u-v, +u-v, +u+v, -u+v run counter-clockwise seen from outside.
struct FaceFrame {
    math::Vec3 normal;
    math::Vec3 u;
    math::Vec3 v;
};

// Same order as BoxFace.
constexpr std::array<FaceFrame, 6> kBoxFaces{{
    {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}},
    {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},
    {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}},
    {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},
    {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},
    {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}},
}};

} // namespace

MeshData makeBox(const math::Vec3& halfExtents, const std::array<math::Vec4, 6>& faceColors)
{
    MeshData mesh;
    if (!isPositive(halfExtents.x) || !isPositive(halfExtents.y) || !isPositive(halfExtents.z)) {
        return mesh;
    }
    mesh.positions.reserve(24);
    mesh.colors.reserve(24);
    mesh.indices.reserve(36);
    for (std::size_t i = 0; i < kBoxFaces.size(); ++i) {
        const FaceFrame& face = kBoxFaces[i];
        // The frame is made of unit axes, so scaling each corner per component
        // by the half extents keeps the winding.
        const auto corner = [&](float su, float sv) {
            return (face.normal + face.u * su + face.v * sv) * halfExtents;
        };
        addQuad(mesh, corner(-1.0F, -1.0F), corner(1.0F, -1.0F), corner(1.0F, 1.0F),
                corner(-1.0F, 1.0F), faceColors[i]);
    }
    return mesh;
}

MeshData makePyramid(float halfBase, float height, const std::array<math::Vec4, 5>& faceColors)
{
    MeshData mesh;
    if (!isPositive(halfBase) || !isPositive(height)) {
        return mesh;
    }
    const float b = halfBase;
    const math::Vec3 apex{0.0F, height, 0.0F};
    const math::Vec3 pxpz{b, 0.0F, b};
    const math::Vec3 pxnz{b, 0.0F, -b};
    const math::Vec3 nxpz{-b, 0.0F, b};
    const math::Vec3 nxnz{-b, 0.0F, -b};

    addTriangle(mesh, pxpz, pxnz, apex, faceColors[0]);   // +X
    addTriangle(mesh, nxnz, nxpz, apex, faceColors[1]);   // -X
    addTriangle(mesh, nxpz, pxpz, apex, faceColors[2]);   // +Z
    addTriangle(mesh, pxnz, nxnz, apex, faceColors[3]);   // -Z
    addQuad(mesh, nxnz, pxnz, pxpz, nxpz, faceColors[4]); // base, facing -Y
    return mesh;
}

MeshData makeCheckerPlane(int cellsPerSide, float cellSize, const math::Vec4& colorA,
                          const math::Vec4& colorB)
{
    MeshData mesh;
    if (cellsPerSide <= 0 || cellsPerSide > kMaxCheckerCells || !isPositive(cellSize)) {
        return mesh;
    }
    const auto cells =
        static_cast<std::size_t>(cellsPerSide) * static_cast<std::size_t>(cellsPerSide);
    mesh.positions.reserve(cells * 4);
    mesh.colors.reserve(cells * 4);
    mesh.indices.reserve(cells * 6);

    const float origin = -0.5F * static_cast<float>(cellsPerSide) * cellSize;
    for (int row = 0; row < cellsPerSide; ++row) {
        const float z0 = origin + static_cast<float>(row) * cellSize;
        const float z1 = z0 + cellSize;
        for (int column = 0; column < cellsPerSide; ++column) {
            const float x0 = origin + static_cast<float>(column) * cellSize;
            const float x1 = x0 + cellSize;
            const math::Vec4& color = ((row + column) % 2 == 0) ? colorA : colorB;
            // Counter-clockwise seen from above (+Y).
            addQuad(mesh, {x0, 0.0F, z1}, {x1, 0.0F, z1}, {x1, 0.0F, z0}, {x0, 0.0F, z0}, color);
        }
    }
    return mesh;
}

} // namespace engine::geometry
