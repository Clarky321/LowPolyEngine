#include "engine/geometry/FaceNormal.hpp"

#include <glm/geometric.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <optional>
#include <vector>

using engine::geometry::faceNormal;
using engine::math::Vec3;

namespace {

bool near(const std::optional<Vec3>& actual, const Vec3& expected)
{
    return actual.has_value() && glm::length(*actual - expected) < 1.0e-6F;
}

std::optional<Vec3> normalOf(const std::vector<Vec3>& corners)
{
    return faceNormal(corners);
}

} // namespace

TEST_CASE("A counter-clockwise triangle's normal points to the viewer", "[geometry][normal]")
{
    CHECK(near(normalOf({{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}), Vec3{0, 0, 1}));
    CHECK(near(normalOf({{0, 0, 0}, {0, 1, 0}, {1, 0, 0}}), Vec3{0, 0, -1})); // clockwise
    CHECK(near(normalOf({{3, 1, 2}, {3, 1, 7}, {9, 1, 2}}), Vec3{0, 1, 0}));
}

TEST_CASE("Polygons use their whole area", "[geometry][normal]")
{
    // A quad seen from above (+Y), counter-clockwise.
    CHECK(near(normalOf({{0, 0, 0}, {0, 0, 1}, {1, 0, 1}, {1, 0, 0}}), Vec3{0, 1, 0}));
    // The first three corners lie on one line, the polygon does not.
    CHECK(near(normalOf({{0, 0, 0}, {1, 0, 0}, {2, 0, 0}, {2, 1, 0}, {0, 1, 0}}), Vec3{0, 0, 1}));
    // A slightly bent quad still gets a unit normal close to its plane's.
    const auto bent = normalOf({{0, 0, 0}, {1, 0, 0.01F}, {1, 1, 0}, {0, 1, 0.01F}});
    REQUIRE(bent.has_value());
    CHECK(std::abs(glm::length(*bent) - 1.0F) < 1.0e-6F);
    CHECK(bent->z > 0.999F);
}

TEST_CASE("Degenerate polygons have no normal", "[geometry][normal]")
{
    CHECK_FALSE(normalOf({}).has_value());
    CHECK_FALSE(normalOf({{0, 0, 0}, {1, 0, 0}}).has_value());
    CHECK_FALSE(normalOf({{0, 0, 0}, {1, 1, 1}, {2, 2, 2}}).has_value());
    CHECK_FALSE(normalOf({{5, 5, 5}, {5, 5, 5}, {5, 5, 5}}).has_value());
    CHECK_FALSE(normalOf({{1, 2, 3}, {1, 2, 3}, {4, 5, 6}, {4, 5, 6}}).has_value());

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    CHECK_FALSE(normalOf({{0, 0, 0}, {1, 0, 0}, {0, nan, 0}}).has_value());
    CHECK_FALSE(normalOf({{0, 0, 0}, {infinity, 0, 0}, {0, 1, 0}}).has_value());
}

TEST_CASE("Tiny, huge and thin polygons are not degenerate", "[geometry][normal]")
{
    // The threshold is relative to the polygon's size.
    CHECK(near(normalOf({{0, 0, 0}, {1e-20F, 0, 0}, {0, 1e-20F, 0}}), Vec3{0, 0, 1}));
    CHECK(near(normalOf({{0, 0, 0}, {3e38F, 0, 0}, {0, 3e38F, 0}}), Vec3{0, 0, 1}));
    CHECK(near(normalOf({{0, 0, 0}, {1, 0, 0}, {0.5F, 1e-6F, 0}}), Vec3{0, 0, 1}));
}
