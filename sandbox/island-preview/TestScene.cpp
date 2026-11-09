#include "TestScene.hpp"

#include "engine/geometry/Primitives.hpp"

#include <array>

namespace island_preview {

namespace {

using engine::geometry::makeBox;
using engine::geometry::makeCheckerPlane;
using engine::geometry::makePyramid;
using engine::math::Vec3;
using engine::math::Vec4;

Vec4 shade(const Vec4& color, float factor)
{
    return Vec4(Vec3(color) * factor, color.a);
}

// Until the renderer has lighting (M2), faces get fixed brightness factors as
// if lit from above and in front: top brightest, bottom darkest. This is
// scene content in vertex colors, not a renderer feature.
std::array<Vec4, 6> boxShades(const Vec4& color)
{
    // BoxFace order: +X, -X, +Y, -Y, +Z, -Z.
    return {shade(color, 0.80F), shade(color, 0.62F), color,
            shade(color, 0.45F), shade(color, 0.90F), shade(color, 0.70F)};
}

std::array<Vec4, 5> pyramidShades(const Vec4& color)
{
    // Sides +X, -X, +Z, -Z, then the base.
    return {shade(color, 0.80F), shade(color, 0.62F), shade(color, 0.95F), shade(color, 0.70F),
            shade(color, 0.45F)};
}

const Vec4 kGrassLight{0.47F, 0.70F, 0.38F, 1.0F};
const Vec4 kGrassDark{0.42F, 0.64F, 0.34F, 1.0F};
const Vec4 kSand{0.93F, 0.77F, 0.52F, 1.0F};
const Vec4 kCoral{0.89F, 0.40F, 0.34F, 1.0F};
const Vec4 kTeal{0.26F, 0.62F, 0.64F, 1.0F};
const Vec4 kChalk{0.94F, 0.92F, 0.87F, 1.0F};
const Vec4 kMustard{0.95F, 0.72F, 0.26F, 1.0F};
const Vec4 kViolet{0.56F, 0.46F, 0.76F, 1.0F};
const Vec4 kStone{0.62F, 0.60F, 0.58F, 1.0F};

} // namespace

std::vector<SceneObject> buildTestScene()
{
    std::vector<SceneObject> scene;
    // 80 x 80 m ground of 2 m cells: big enough to see motion and perspective.
    scene.push_back({makeCheckerPlane(40, 2.0F, kGrassLight, kGrassDark), Vec3{0.0F}});
    scene.push_back({makePyramid(4.5F, 6.0F, pyramidShades(kSand)), Vec3{0.0F}});

    scene.push_back({makeBox(Vec3{1.0F}, boxShades(kCoral)), {9.0F, 1.0F, -5.0F}});
    scene.push_back({makeBox({1.0F, 2.5F, 1.0F}, boxShades(kTeal)), {-10.0F, 2.5F, -7.0F}});
    scene.push_back({makeBox(Vec3{0.75F}, boxShades(kChalk)), {6.5F, 0.75F, 8.0F}});
    scene.push_back({makeBox({3.0F, 0.6F, 0.6F}, boxShades(kViolet)), {13.0F, 0.6F, 3.0F}});
    // Floating and spinning: shows that update() and frame time drive the scene.
    scene.push_back({makeBox(Vec3{1.0F}, boxShades(kMustard)), {-7.0F, 3.0F, 5.0F}, 0.8F});

    // Pillars at the ground corners help with orientation while flying.
    for (const float x : {-36.0F, 36.0F}) {
        for (const float z : {-36.0F, 36.0F}) {
            scene.push_back({makeBox({1.0F, 5.0F, 1.0F}, boxShades(kStone)), {x, 5.0F, z}});
        }
    }
    return scene;
}

} // namespace island_preview
