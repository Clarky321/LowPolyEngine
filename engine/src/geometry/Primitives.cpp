#include "engine/geometry/Primitives.hpp"

#include <glm/geometric.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace engine::geometry {

namespace {

bool isPositive(float value) noexcept
{
    return std::isfinite(value) && value > 0.0F;
}

// UVs of a quad's corners in addQuad order, relative to its (0, 0) corner.
constexpr std::array<math::Vec2, 4> kQuadUvs{{
    {0.0F, 0.0F},
    {1.0F, 0.0F},
    {1.0F, 1.0F},
    {0.0F, 1.0F},
}};

// UVs of a triangle's corners in addTriangle order: the base edge from left
// to right (seen from the front), then the apex.
constexpr std::array<math::Vec2, 3> kTriangleUvs{{
    {0.0F, 0.0F},
    {1.0F, 0.0F},
    {0.5F, 1.0F},
}};

glm::dvec3 toDouble(const math::Vec3& v) noexcept
{
    return {static_cast<double>(v.x), static_cast<double>(v.y), static_cast<double>(v.z)};
}

// Unit normal of the triangle a, b, c (counter-clockwise seen from its front).
// Computed in double, so very small or very large but finite sizes neither
// underflow to zero nor overflow to infinity.
math::Vec3 faceNormal(const math::Vec3& a, const math::Vec3& b, const math::Vec3& c)
{
    const glm::dvec3 origin = toDouble(a);
    const glm::dvec3 normal =
        glm::normalize(glm::cross(toDouble(b) - origin, toDouble(c) - origin));
    return {static_cast<float>(normal.x), static_cast<float>(normal.y),
            static_cast<float>(normal.z)};
}

// Adds a quad given counter-clockwise (seen from its front) as two triangles.
// All four vertices get `normal`; their UVs are the unit square (kQuadUvs)
// shifted by uvOrigin, so the corners a, b, c, d run (0, 0), (1, 0), (1, 1), (0, 1).
void addQuad(MeshData& mesh, const math::Vec3& a, const math::Vec3& b, const math::Vec3& c,
             const math::Vec3& d, const math::Vec4& color, const math::Vec3& normal,
             const math::Vec2& uvOrigin)
{
    const auto first = static_cast<std::uint32_t>(mesh.positions.size());
    mesh.positions.insert(mesh.positions.end(), {a, b, c, d});
    mesh.colors.insert(mesh.colors.end(), 4, color);
    mesh.normals.insert(mesh.normals.end(), 4, normal);
    for (const math::Vec2& uv : kQuadUvs) {
        mesh.uvs.push_back(uvOrigin + uv);
    }
    mesh.indices.insert(mesh.indices.end(),
                        {first, first + 1, first + 2, first, first + 2, first + 3});
}

// Adds a triangle given counter-clockwise (seen from its front) as base left,
// base right, apex. It gets its face normal and the UVs of kTriangleUvs.
void addTriangle(MeshData& mesh, const math::Vec3& a, const math::Vec3& b, const math::Vec3& c,
                 const math::Vec4& color)
{
    const auto first = static_cast<std::uint32_t>(mesh.positions.size());
    mesh.positions.insert(mesh.positions.end(), {a, b, c});
    mesh.colors.insert(mesh.colors.end(), 3, color);
    mesh.normals.insert(mesh.normals.end(), 3, faceNormal(a, b, c));
    mesh.uvs.insert(mesh.uvs.end(), kTriangleUvs.begin(), kTriangleUvs.end());
    mesh.indices.insert(mesh.indices.end(), {first, first + 1, first + 2});
}

// A box face as its outward normal n and two in-plane axes with u x v = n, so
// the corners -u-v, +u-v, +u+v, -u+v run counter-clockwise seen from outside.
// u and v are also the face's U and V texture directions: seen from outside,
// U points right and V up (world +Y on the side faces).
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
    mesh.normals.reserve(24);
    mesh.uvs.reserve(24);
    mesh.indices.reserve(36);
    for (std::size_t i = 0; i < kBoxFaces.size(); ++i) {
        const FaceFrame& face = kBoxFaces[i];
        // The frame is made of unit axes, so scaling each corner per component
        // by the half extents keeps the winding.
        const auto corner = [&](float su, float sv) {
            return (face.normal + face.u * su + face.v * sv) * halfExtents;
        };
        addQuad(mesh, corner(-1.0F, -1.0F), corner(1.0F, -1.0F), corner(1.0F, 1.0F),
                corner(-1.0F, 1.0F), faceColors[i], face.normal, math::Vec2{0.0F});
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

    // Each side lists its base corners from left to right seen from outside.
    addTriangle(mesh, pxpz, pxnz, apex, faceColors[0]); // +X
    addTriangle(mesh, nxnz, nxpz, apex, faceColors[1]); // -X
    addTriangle(mesh, nxpz, pxpz, apex, faceColors[2]); // +Z
    addTriangle(mesh, pxnz, nxnz, apex, faceColors[3]); // -Z
    // Base, facing -Y: seen from below, U = +X and V = +Z.
    addQuad(mesh, nxnz, pxnz, pxpz, nxpz, faceColors[4], math::Vec3{0.0F, -1.0F, 0.0F},
            math::Vec2{0.0F});
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
    mesh.normals.reserve(cells * 4);
    mesh.uvs.reserve(cells * 4);
    mesh.indices.reserve(cells * 6);

    const math::Vec3 up{0.0F, 1.0F, 0.0F};
    const float origin = -0.5F * static_cast<float>(cellsPerSide) * cellSize;
    for (int row = 0; row < cellsPerSide; ++row) {
        const float z0 = origin + static_cast<float>(row) * cellSize;
        const float z1 = z0 + cellSize;
        // V grows towards -Z: the cell's +Z edge (z1) has the smaller V.
        const auto v = static_cast<float>(cellsPerSide - row - 1);
        for (int column = 0; column < cellsPerSide; ++column) {
            const float x0 = origin + static_cast<float>(column) * cellSize;
            const float x1 = x0 + cellSize;
            const math::Vec4& color = ((row + column) % 2 == 0) ? colorA : colorB;
            // Counter-clockwise seen from above (+Y), starting at the cell's
            // (-X, +Z) corner, which gets the cell's smallest UV.
            addQuad(mesh, {x0, 0.0F, z1}, {x1, 0.0F, z1}, {x1, 0.0F, z0}, {x0, 0.0F, z0}, color, up,
                    math::Vec2{static_cast<float>(column), v});
        }
    }
    return mesh;
}

} // namespace engine::geometry
