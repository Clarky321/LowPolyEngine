#include "TestScene.hpp"

#include "engine/geometry/Primitives.hpp"

#include <array>
#include <utility>

namespace island_preview {

namespace {

using engine::geometry::makeBox;
using engine::geometry::makeCheckerPlane;
using engine::geometry::makePyramid;
using engine::math::Vec3;
using engine::math::Vec4;
using engine::render::Color;
using engine::render::MaterialDesc;

Vec4 gray(float brightness)
{
    return Vec4(Vec3(brightness), 1.0F);
}

// Until the renderer has lighting (M2.8), faces get fixed brightness factors as
// if lit from above and in front: top brightest, bottom darkest. They are gray
// vertex colors; the material's base color multiplies in the hue.
std::array<Vec4, 6> boxShades()
{
    // BoxFace order: +X, -X, +Y, -Y, +Z, -Z.
    return {gray(0.80F), gray(0.62F), gray(1.0F), gray(0.45F), gray(0.90F), gray(0.70F)};
}

std::array<Vec4, 5> pyramidShades()
{
    // Sides +X, -X, +Z, -Z, then the base.
    return {gray(0.80F), gray(0.62F), gray(0.95F), gray(0.70F), gray(0.45F)};
}

MaterialDesc material(const Color& baseColor)
{
    return MaterialDesc{.baseColor = baseColor};
}

const Vec4 kGrassLight{0.47F, 0.70F, 0.38F, 1.0F};
const Vec4 kGrassDark{0.42F, 0.64F, 0.34F, 1.0F};
constexpr Color kWhite{1.0F, 1.0F, 1.0F, 1.0F};
constexpr Color kSand{0.93F, 0.77F, 0.52F, 1.0F};
constexpr Color kCoral{0.89F, 0.40F, 0.34F, 1.0F};
constexpr Color kTeal{0.26F, 0.62F, 0.64F, 1.0F};
constexpr Color kChalk{0.94F, 0.92F, 0.87F, 1.0F};
constexpr Color kMustard{0.95F, 0.72F, 0.26F, 1.0F};
constexpr Color kViolet{0.56F, 0.46F, 0.76F, 1.0F};
constexpr Color kStone{0.62F, 0.60F, 0.58F, 1.0F};
constexpr Color kRose{0.90F, 0.52F, 0.70F, 1.0F};
constexpr Color kSky{0.36F, 0.56F, 0.86F, 1.0F};

} // namespace

TestScene buildTestScene()
{
    TestScene scene;
    const auto addMesh = [&scene](engine::geometry::MeshData mesh) {
        scene.meshes.push_back(std::move(mesh));
        return scene.meshes.size() - 1;
    };
    const auto add = [&scene](std::size_t mesh, const Color& color, const Vec3& position,
                              float spin = 0.0F) {
        scene.objects.push_back({mesh, material(color), position, spin});
    };

    // 80 x 80 m ground of 2 m cells: big enough to see motion and perspective.
    const auto ground = addMesh(makeCheckerPlane(40, 2.0F, kGrassLight, kGrassDark));
    const auto pyramid = addMesh(makePyramid(4.5F, 6.0F, pyramidShades()));
    const auto cube = addMesh(makeBox(Vec3{1.0F}, boxShades()));
    const auto tower = addMesh(makeBox({1.0F, 2.5F, 1.0F}, boxShades()));
    const auto smallCube = addMesh(makeBox(Vec3{0.75F}, boxShades()));
    const auto bar = addMesh(makeBox({3.0F, 0.6F, 0.6F}, boxShades()));
    const auto pillar = addMesh(makeBox({1.0F, 5.0F, 1.0F}, boxShades()));
    const auto swatch = addMesh(makeBox(Vec3{0.6F}, boxShades()));

    add(ground, kWhite, Vec3{0.0F});
    add(pyramid, kSand, Vec3{0.0F});
    add(cube, kCoral, {9.0F, 1.0F, -5.0F});
    add(tower, kTeal, {-10.0F, 2.5F, -7.0F});
    add(smallCube, kChalk, {6.5F, 0.75F, 8.0F});
    add(bar, kViolet, {13.0F, 0.6F, 3.0F});
    // Floating and spinning: shows that update() and frame time drive the scene.
    // Shares its mesh with the coral cube.
    add(cube, kMustard, {-7.0F, 3.0F, 5.0F}, 0.8F);

    // Pillars at the ground corners help with orientation while flying; one mesh.
    for (const float x : {-36.0F, 36.0F}) {
        for (const float z : {-36.0F, 36.0F}) {
            add(pillar, kStone, {x, 5.0F, z});
        }
    }

    // Material swatches: one mesh, five materials.
    const std::array<Color, 5> swatchColors{kCoral, kMustard, kTeal, kSky, kRose};
    for (std::size_t i = 0; i < swatchColors.size(); ++i) {
        add(swatch, swatchColors[i], {-13.0F + 2.5F * static_cast<float>(i), 0.6F, 10.0F});
    }
    return scene;
}

} // namespace island_preview
