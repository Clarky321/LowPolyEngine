#include "engine/worldgen/IslandWater.hpp"

#include "FloatingPointRules.hpp"
#include "SubKeys.hpp"

#include "engine/geometry/FaceNormal.hpp"
#include "engine/worldgen/Random.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <limits>
#include <optional>
#include <queue>
#include <utility>
#include <vector>

namespace engine::worldgen {

namespace {

using detail::subKey;

// Constants of ADR 0016. Changing any of them changes worlds.
constexpr int kSide = kWaterGridCells + 1; // samples per row and per column
constexpr std::size_t kSamples = static_cast<std::size_t>(kSide) * static_cast<std::size_t>(kSide);
constexpr float kMinLakeDepth = 0.3F; // spill level above the bottom, metres
constexpr std::size_t kMinBasinSamples = 4;
constexpr std::size_t kMinLakeSamples = 3;
constexpr float kMinFill = 0.55F; // share of a basin's depth a lake fills
constexpr float kMaxFill = 0.95F;
constexpr int kSpringAttempts = 16;
constexpr float kMinSpringHeight = 1.0F; // above the rim, metres
constexpr std::size_t kMinStreamSamples = 6;
constexpr float kMinStreamWidth = 0.035F; // times the island's radius
constexpr float kMaxStreamWidth = 0.06F;
constexpr float kFallWidthFactor = 2.5F; // waterfall width over stream width
constexpr float kMinFallLength = 0.8F;   // times the island's undersideDepth
constexpr float kMaxFallLength = 1.4F;
constexpr float kMinFallSpeed = 0.8F; // horizontal speed over the rim, m/s
constexpr float kMaxFallSpeed = 1.6F;
constexpr double kGravity = 9.81;
constexpr int kFallSegments = 6;
constexpr double kFallWidening = 1.5; // extra width at the bottom, share of the top width
constexpr int kRimBisections = 24;
constexpr double kDeepWater = 2.5; // depth at which a lake shows its deep colour, metres
constexpr double kDiagonal = 1.4142135623730951;

// Keys of the island's water seed: one random sequence per kind of water body.
constexpr std::uint64_t kLakeKey = 0U;
constexpr std::uint64_t kStreamKey = 1U;
constexpr std::uint64_t kWaterfallKey = 2U;

// Display-space colours; alpha below 1 makes them translucent.
const math::Vec4 kShallowLake{0.47F, 0.82F, 0.83F, 0.62F};
const math::Vec4 kDeepLake{0.13F, 0.43F, 0.62F, 0.86F};
const math::Vec4 kStreamColor{0.52F, 0.84F, 0.88F, 0.82F};
const math::Vec4 kFallColor{0.80F, 0.93F, 0.98F, 0.92F};

const math::Vec3 kUp{0.0F, 1.0F, 0.0F};

constexpr std::size_t kNoLake = std::numeric_limits<std::size_t>::max();

// The 8 neighbours of a sample (column, row offsets), in a fixed order.
constexpr std::array<std::array<int, 2>, 8> kNeighbours{
    {{-1, -1}, {0, -1}, {1, -1}, {-1, 0}, {1, 0}, {-1, 1}, {0, 1}, {1, 1}}};

std::size_t sampleIndex(int i, int j)
{
    return static_cast<std::size_t>(j) * static_cast<std::size_t>(kSide) +
           static_cast<std::size_t>(i);
}

int column(std::size_t index)
{
    return static_cast<int>(index % static_cast<std::size_t>(kSide));
}

int row(std::size_t index)
{
    return static_cast<int>(index / static_cast<std::size_t>(kSide));
}

// Calls visit(neighbour, distance in cells) for each neighbour on the grid.
template <typename Visit>
void forEachNeighbour(std::size_t index, Visit&& visit)
{
    const int i = column(index);
    const int j = row(index);
    for (const auto& offset : kNeighbours) {
        const int ni = i + offset[0];
        const int nj = j + offset[1];
        if (ni < 0 || nj < 0 || ni >= kSide || nj >= kSide) {
            continue;
        }
        const bool diagonal = offset[0] != 0 && offset[1] != 0;
        visit(sampleIndex(ni, nj), diagonal ? kDiagonal : 1.0);
    }
}

// The top surface sampled on the grid over the island's bounding square.
struct Grid {
    double radius = 0.0;
    double step = 0.0;
    // Per sample, row by row: whether it lies inside the outline, its
    // normalized distance (inside samples only) and the top height (0 outside).
    std::vector<std::uint8_t> inside;
    std::vector<float> distance;
    std::vector<float> height;

    [[nodiscard]] math::Vec2 position(int i, int j) const
    {
        return math::Vec2{static_cast<float>(-radius + step * static_cast<double>(i)),
                          static_cast<float>(-radius + step * static_cast<double>(j))};
    }

    [[nodiscard]] math::Vec2 position(std::size_t index) const
    {
        return position(column(index), row(index));
    }
};

Grid sampleTop(const IslandShape& shape)
{
    Grid grid;
    grid.radius = static_cast<double>(shape.island.radius);
    grid.step = 2.0 * grid.radius / static_cast<double>(kWaterGridCells);
    grid.inside.assign(kSamples, 0U);
    grid.distance.assign(kSamples, std::numeric_limits<float>::infinity());
    grid.height.assign(kSamples, 0.0F);
    for (int j = 0; j < kSide; ++j) {
        for (int i = 0; i < kSide; ++i) {
            const math::Vec2 local = grid.position(i, j);
            const auto x = static_cast<double>(local.x);
            const auto z = static_cast<double>(local.y);
            // The outline never leaves the bounding circle.
            if (x * x + z * z >= grid.radius * grid.radius) {
                continue;
            }
            const float distance = outlineDistance(shape, local);
            if (!(distance < 1.0F)) {
                continue;
            }
            const std::size_t k = sampleIndex(i, j);
            grid.inside[k] = 1U;
            grid.distance[k] = distance;
            grid.height[k] = topHeightAt(shape, local);
        }
    }
    return grid;
}

// The level water would fill each sample to before it drains over the rim,
// and where it drains to (priority flood from the samples outside the
// outline): `filled` is the lowest level of any path from the sample to the
// outside, at least the sample's own height; `from` is the neighbour the flood
// reached the sample from, whose filled level is not higher.
struct Flood {
    std::vector<float> filled;
    std::vector<std::size_t> from;
};

Flood floodFromRim(const Grid& grid)
{
    Flood flood{grid.height, std::vector<std::size_t>(kSamples, kSamples)};
    std::vector<std::uint8_t> reached(kSamples, 0U);
    using Entry = std::pair<float, std::size_t>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<>> open;
    for (std::size_t k = 0; k < kSamples; ++k) {
        if (grid.inside[k] == 0U) {
            reached[k] = 1U;
            open.emplace(flood.filled[k], k);
        }
    }
    while (!open.empty()) {
        const float level = open.top().first;
        const std::size_t k = open.top().second;
        open.pop();
        forEachNeighbour(k, [&](std::size_t n, double) {
            if (reached[n] == 0U) {
                reached[n] = 1U;
                flood.filled[n] = std::max(grid.height[n], level);
                flood.from[n] = k;
                open.emplace(flood.filled[n], n);
            }
        });
    }
    return flood;
}

// Whether water would stand on the sample: it lies in a depression.
bool inDepression(const Grid& grid, const Flood& flood, std::size_t k)
{
    return grid.inside[k] != 0U && flood.filled[k] > grid.height[k];
}

// A depression of the top: 8-connected samples whose filled level is above
// their height. The filled level is the same for all of them: the spill level.
struct Basin {
    std::size_t deepest = 0;
    float bottom = 0.0F;
    float spill = 0.0F;
    float farthest = 0.0F;
    std::size_t samples = 0;
};

constexpr std::size_t kNoBasin = std::numeric_limits<std::size_t>::max();

struct Basins {
    // In the order of their first sample.
    std::vector<Basin> list;
    // Per sample: the index of its basin, or kNoBasin.
    std::vector<std::size_t> of;
};

Basins findBasins(const Grid& grid, const Flood& flood)
{
    Basins basins;
    basins.of.assign(kSamples, kNoBasin);
    std::vector<std::size_t> open;
    for (std::size_t k = 0; k < kSamples; ++k) {
        if (basins.of[k] != kNoBasin || !inDepression(grid, flood, k)) {
            continue;
        }
        const std::size_t index = basins.list.size();
        Basin basin{.deepest = k,
                    .bottom = grid.height[k],
                    .spill = flood.filled[k],
                    .farthest = grid.distance[k],
                    .samples = 0};
        basins.of[k] = index;
        open.assign(1U, k);
        while (!open.empty()) {
            const std::size_t current = open.back();
            open.pop_back();
            ++basin.samples;
            const float height = grid.height[current];
            if (height < basin.bottom || (height == basin.bottom && current < basin.deepest)) {
                basin.deepest = current;
                basin.bottom = height;
            }
            basin.farthest = std::max(basin.farthest, grid.distance[current]);
            forEachNeighbour(current, [&](std::size_t n, double) {
                if (basins.of[n] == kNoBasin && inDepression(grid, flood, n)) {
                    basins.of[n] = index;
                    open.push_back(n);
                }
            });
        }
        basins.list.push_back(basin);
    }
    return basins;
}

// A pond the stream fills must lie away from the steep edge of the top.
bool canHoldPond(const Basin& basin)
{
    return basin.farthest <= kLakeMaxDistance;
}

// A lake also needs some depth and size.
bool canHoldLake(const Basin& basin)
{
    return canHoldPond(basin) && basin.spill - basin.bottom >= kMinLakeDepth &&
           basin.samples >= kMinBasinSamples;
}

// The 8-connected samples below `level` around `start`, in index order.
std::vector<std::size_t> samplesBelow(const Grid& grid, std::size_t start, float level)
{
    std::vector<std::uint8_t> seen(kSamples, 0U);
    std::vector<std::size_t> below;
    std::vector<std::size_t> open{start};
    seen[start] = 1U;
    while (!open.empty()) {
        const std::size_t current = open.back();
        open.pop_back();
        below.push_back(current);
        forEachNeighbour(current, [&](std::size_t n, double) {
            if (seen[n] == 0U && grid.inside[n] != 0U && grid.height[n] < level) {
                seen[n] = 1U;
                open.push_back(n);
            }
        });
    }
    std::sort(below.begin(), below.end());
    return below;
}

// The grid cells with at least one corner among the samples, in index order
// (cell index cj * kWaterGridCells + ci for the cell whose lowest corner is
// sample (ci, cj)).
std::vector<std::size_t> cellsAround(const std::vector<std::size_t>& samples)
{
    std::vector<std::size_t> cells;
    for (const std::size_t sample : samples) {
        const int i = column(sample);
        const int j = row(sample);
        for (int cj = j - 1; cj <= j; ++cj) {
            for (int ci = i - 1; ci <= i; ++ci) {
                if (ci >= 0 && cj >= 0 && ci < kWaterGridCells && cj < kWaterGridCells) {
                    cells.push_back(static_cast<std::size_t>(cj) *
                                        static_cast<std::size_t>(kWaterGridCells) +
                                    static_cast<std::size_t>(ci));
                }
            }
        }
    }
    std::sort(cells.begin(), cells.end());
    cells.erase(std::unique(cells.begin(), cells.end()), cells.end());
    return cells;
}

// Appends a triangle with its own three vertices, the face normal and one
// colour per corner. Its corners are swapped if needed so that it faces along
// `facing`; triangles smaller than kMinWaterTriangleArea are left out.
void addTriangle(geometry::MeshData& mesh, math::Vec3 a, math::Vec3 b, math::Vec3 c,
                 math::Vec4 colorA, math::Vec4 colorB, math::Vec4 colorC, const math::Vec3& facing)
{
    const double abX = static_cast<double>(b.x) - static_cast<double>(a.x);
    const double abY = static_cast<double>(b.y) - static_cast<double>(a.y);
    const double abZ = static_cast<double>(b.z) - static_cast<double>(a.z);
    const double acX = static_cast<double>(c.x) - static_cast<double>(a.x);
    const double acY = static_cast<double>(c.y) - static_cast<double>(a.y);
    const double acZ = static_cast<double>(c.z) - static_cast<double>(a.z);
    const double nX = abY * acZ - abZ * acY;
    const double nY = abZ * acX - abX * acZ;
    const double nZ = abX * acY - abY * acX;
    if (!(0.5 * std::sqrt(nX * nX + nY * nY + nZ * nZ) >= kMinWaterTriangleArea)) {
        return;
    }
    if (nX * static_cast<double>(facing.x) + nY * static_cast<double>(facing.y) +
            nZ * static_cast<double>(facing.z) <
        0.0) {
        std::swap(b, c);
        std::swap(colorB, colorC);
    }
    const std::array<math::Vec3, 3> corners{a, b, c};
    const auto normal = geometry::faceNormal(corners);
    if (!normal) {
        return;
    }
    const auto first = static_cast<std::uint32_t>(mesh.positions.size());
    const std::array<math::Vec4, 3> colors{colorA, colorB, colorC};
    for (std::size_t corner = 0; corner < 3U; ++corner) {
        mesh.positions.push_back(corners[corner]);
        mesh.normals.push_back(*normal);
        mesh.colors.push_back(colors[corner]);
    }
    mesh.indices.push_back(first);
    mesh.indices.push_back(first + 1U);
    mesh.indices.push_back(first + 2U);
}

// Lake colour by the water's depth: lighter and clearer where it is shallow.
math::Vec4 lakeColor(double depth)
{
    const auto t = static_cast<float>(std::clamp(depth / kDeepWater, 0.0, 1.0));
    return kShallowLake + (kDeepLake - kShallowLake) * t;
}

// A flat surface at `level` over the cells, two triangles per cell with
// alternating diagonals, each coloured by the depth at its centre.
void addLakeSurface(geometry::MeshData& mesh, const Grid& grid, const IslandShape& shape,
                    const std::vector<std::size_t>& cells, float level)
{
    const auto corner = [&](int i, int j) {
        const math::Vec2 p = grid.position(i, j);
        return math::Vec3{p.x, level, p.y};
    };
    const auto addFace = [&](const math::Vec3& a, const math::Vec3& b, const math::Vec3& c) {
        const math::Vec3 centre = (a + b + c) / 3.0F;
        const double depth = static_cast<double>(level) -
                             static_cast<double>(topHeightAt(shape, {centre.x, centre.z}));
        const math::Vec4 color = lakeColor(depth);
        addTriangle(mesh, a, b, c, color, color, color, kUp);
    };
    for (const std::size_t cell : cells) {
        const int ci = static_cast<int>(cell % static_cast<std::size_t>(kWaterGridCells));
        const int cj = static_cast<int>(cell / static_cast<std::size_t>(kWaterGridCells));
        const math::Vec3 p00 = corner(ci, cj);
        const math::Vec3 p10 = corner(ci + 1, cj);
        const math::Vec3 p01 = corner(ci, cj + 1);
        const math::Vec3 p11 = corner(ci + 1, cj + 1);
        if ((ci + cj) % 2 == 0) {
            addFace(p00, p10, p11);
            addFace(p00, p11, p01);
        } else {
            addFace(p00, p10, p01);
            addFace(p10, p11, p01);
        }
    }
}

// The grid sample nearest to the point u (in [-1, 1]) along one axis.
int nearestSample(float u)
{
    const double scaled =
        (static_cast<double>(u) + 1.0) * 0.5 * static_cast<double>(kWaterGridCells);
    return std::clamp(static_cast<int>(std::floor(scaled + 0.5)), 0, kWaterGridCells);
}

// The way down from a sample to the first sample outside the outline: to the
// neighbour where the filled level drops most steeply (per distance); where it
// does not drop (in a depression, which water fills, or on a flat), to the
// sample the flood reached it from. The last sample lies outside the outline.
std::vector<std::size_t> walkDown(const Grid& grid, const Flood& flood, std::size_t start)
{
    std::vector<std::size_t> path{start};
    std::size_t current = start;
    // Each step lowers the filled level or moves closer to the outside along
    // the flood's paths, so the walk ends; the bound is only a safeguard.
    while (grid.inside[current] != 0U && path.size() <= kSamples) {
        std::size_t best = kSamples;
        double bestDrop = 0.0;
        forEachNeighbour(current, [&](std::size_t n, double distance) {
            const double drop = (static_cast<double>(flood.filled[current]) -
                                 static_cast<double>(flood.filled[n])) /
                                distance;
            if (drop > bestDrop) {
                bestDrop = drop;
                best = n;
            }
        });
        current = best != kSamples ? best : flood.from[current];
        path.push_back(current);
    }
    return path;
}

math::Vec2 pointBetween(const math::Vec2& a, const math::Vec2& b, double t)
{
    return a + (b - a) * static_cast<float>(t);
}

// A ribbon of the given half width along the path, facing up.
void addRibbon(geometry::MeshData& mesh, const std::vector<math::Vec3>& path, float halfWidth,
               const math::Vec4& color)
{
    const std::size_t count = path.size();
    std::vector<math::Vec3> left(count);
    std::vector<math::Vec3> right(count);
    math::Vec3 side{0.0F};
    for (std::size_t i = 0; i < count; ++i) {
        const math::Vec3& before = path[i == 0 ? 0 : i - 1U];
        const math::Vec3& after = path[std::min(i + 1U, count - 1U)];
        const double dx = static_cast<double>(after.x) - static_cast<double>(before.x);
        const double dz = static_cast<double>(after.z) - static_cast<double>(before.z);
        const double length = std::sqrt(dx * dx + dz * dz);
        if (length > 0.0) {
            const double scale = static_cast<double>(halfWidth) / length;
            side =
                math::Vec3{static_cast<float>(-dz * scale), 0.0F, static_cast<float>(dx * scale)};
        }
        left[i] = path[i] + side;
        right[i] = path[i] - side;
    }
    for (std::size_t i = 0; i + 1U < count; ++i) {
        addTriangle(mesh, left[i], right[i], right[i + 1U], color, color, color, kUp);
        addTriangle(mesh, left[i], right[i + 1U], left[i + 1U], color, color, color, kUp);
    }
}

// One cross-section of a waterfall: k = 0 is the top edge, kFallSegments the
// bottom. The centre follows a parabola (the water's flight), the width grows
// and the colour fades out towards the bottom.
struct FallSection {
    math::Vec3 centre{0.0F};
    float halfWidth = 0.0F;
    float alpha = 0.0F;
};

FallSection fallSection(const Waterfall& fall, int k)
{
    const double f = static_cast<double>(k) / static_cast<double>(kFallSegments);
    const double out = static_cast<double>(fall.drift) * f;
    FallSection section;
    section.centre = math::Vec3{static_cast<float>(static_cast<double>(fall.top.x) +
                                                   static_cast<double>(fall.outward.x) * out),
                                static_cast<float>(static_cast<double>(fall.top.y) -
                                                   static_cast<double>(fall.length) * f * f),
                                static_cast<float>(static_cast<double>(fall.top.z) +
                                                   static_cast<double>(fall.outward.y) * out)};
    section.halfWidth =
        static_cast<float>(0.5 * static_cast<double>(fall.width) * (1.0 + kFallWidening * f));
    section.alpha = static_cast<float>(static_cast<double>(kFallColor.a) * (1.0 - f * f));
    return section;
}

// Distance in the XZ plane from p to the segment ab.
double distanceToSegment(const math::Vec3& p, const math::Vec3& a, const math::Vec3& b)
{
    const double abX = static_cast<double>(b.x) - static_cast<double>(a.x);
    const double abZ = static_cast<double>(b.z) - static_cast<double>(a.z);
    const double apX = static_cast<double>(p.x) - static_cast<double>(a.x);
    const double apZ = static_cast<double>(p.z) - static_cast<double>(a.z);
    const double lengthSquared = abX * abX + abZ * abZ;
    const double t =
        lengthSquared > 0.0 ? std::clamp((apX * abX + apZ * abZ) / lengthSquared, 0.0, 1.0) : 0.0;
    const double dx = apX - t * abX;
    const double dz = apZ - t * abZ;
    return std::sqrt(dx * dx + dz * dz);
}

// Whether the fall passes through the volume of another island.
bool fallBlocked(const Waterfall& fall, const PlacedIsland& island,
                 std::span<const PlacedIsland> islands)
{
    for (const PlacedIsland& other : islands) {
        if (other.index == island.index) {
            continue;
        }
        const double low =
            static_cast<double>(other.anchor.y) - static_cast<double>(other.undersideDepth);
        const double high =
            static_cast<double>(other.anchor.y) + static_cast<double>(other.topHeight);
        for (int k = 0; k < kFallSegments; ++k) {
            const FallSection upper = fallSection(fall, k);
            const FallSection lower = fallSection(fall, k + 1);
            const math::Vec3 a = island.anchor + upper.centre;
            const math::Vec3 b = island.anchor + lower.centre;
            if (static_cast<double>(a.y) < low || static_cast<double>(b.y) > high) {
                continue;
            }
            if (distanceToSegment(other.anchor, a, b) <
                static_cast<double>(other.radius) + static_cast<double>(lower.halfWidth)) {
                return true;
            }
        }
    }
    return false;
}

// Both sides of the waterfall: it can be seen from outside and from below the
// island.
void addWaterfall(geometry::MeshData& mesh, const Waterfall& fall)
{
    const math::Vec3 outward{fall.outward.x, 0.0F, fall.outward.y};
    const math::Vec3 across{-fall.outward.y, 0.0F, fall.outward.x};
    for (const float facing : {1.0F, -1.0F}) {
        for (int k = 0; k < kFallSegments; ++k) {
            const FallSection upper = fallSection(fall, k);
            const FallSection lower = fallSection(fall, k + 1);
            const math::Vec3 a = upper.centre + across * upper.halfWidth;
            const math::Vec3 b = upper.centre - across * upper.halfWidth;
            const math::Vec3 c = lower.centre - across * lower.halfWidth;
            const math::Vec3 d = lower.centre + across * lower.halfWidth;
            const math::Vec4 top{kFallColor.r, kFallColor.g, kFallColor.b, upper.alpha};
            const math::Vec4 bottom{kFallColor.r, kFallColor.g, kFallColor.b, lower.alpha};
            addTriangle(mesh, a, b, c, top, top, bottom, outward * facing);
            addTriangle(mesh, a, c, d, top, bottom, bottom, outward * facing);
        }
    }
}

} // namespace

IslandWater generateIslandWater(const IslandShape& shape, const WorldGenSettings& settings,
                                std::span<const PlacedIsland> islands)
{
    IslandWater water;
    if (!settings.water.enabled) {
        return water;
    }
    const PlacedIsland& island = shape.island;
    const std::uint64_t waterSeed =
        deriveSeed(settings.seed, RandomStream::Water, island.index, settings.generatorVersion);
    const Grid grid = sampleTop(shape);
    const Flood flood = floodFromRim(grid);
    const Basins basins = findBasins(grid, flood);
    const auto maxLakes = static_cast<std::size_t>(settings.water.maxLakesPerIsland);

    // Lakes by basin, and the grid cells each lake's surface covers. Water
    // samples of different basins are never neighbours, so no cell is shared.
    std::vector<std::size_t> lakeOfBasin(basins.list.size(), kNoLake);
    std::vector<std::vector<std::size_t>> lakeCells;
    const auto addLake = [&](std::size_t basinIndex, float level, bool overflows,
                             std::size_t minSamples) {
        const Basin& basin = basins.list[basinIndex];
        const std::vector<std::size_t> below = samplesBelow(grid, basin.deepest, level);
        if (below.size() < minSamples) {
            return;
        }
        lakeOfBasin[basinIndex] = water.lakes.size();
        water.lakes.push_back(Lake{.deepest = grid.position(basin.deepest),
                                   .bottom = basin.bottom,
                                   .level = level,
                                   .spillLevel = basin.spill,
                                   .overflows = overflows,
                                   .samples = below.size()});
        lakeCells.push_back(cellsAround(below));
    };

    // The stream, with streamChance: from the highest of the drawn springs (on
    // higher ground, not in a hollow) whose way down reaches the rim through at
    // most maxLakesPerIsland hollows, each away from the edge of the top. It
    // fills them as ponds.
    Pcg32 streamRandom{subKey(waterSeed, kStreamKey)};
    std::vector<std::size_t> way;
    float width = 0.0F;
    if (streamRandom.nextBool(settings.water.streamChance)) {
        width = island.radius * streamRandom.nextFloat(kMinStreamWidth, kMaxStreamWidth);
        std::vector<std::size_t> springs;
        for (int attempt = 0; attempt < kSpringAttempts; ++attempt) {
            const float u = streamRandom.nextFloat(-1.0F, 1.0F);
            const float v = streamRandom.nextFloat(-1.0F, 1.0F);
            const std::size_t k = sampleIndex(nearestSample(u), nearestSample(v));
            if (grid.inside[k] != 0U && grid.distance[k] <= kStreamSourceMaxDistance &&
                grid.height[k] >= kMinSpringHeight && !inDepression(grid, flood, k)) {
                springs.push_back(k);
            }
        }
        std::sort(springs.begin(), springs.end(), [&](std::size_t a, std::size_t b) {
            if (grid.height[a] != grid.height[b]) {
                return grid.height[a] > grid.height[b];
            }
            return a < b;
        });
        springs.erase(std::unique(springs.begin(), springs.end()), springs.end());
        for (const std::size_t spring : springs) {
            std::vector<std::size_t> path = walkDown(grid, flood, spring);
            if (grid.inside[path.back()] != 0U) {
                continue;
            }
            std::size_t onLand = 0;
            std::vector<std::size_t> hollows;
            bool usable = true;
            for (std::size_t i = 0; i + 1U < path.size(); ++i) {
                const std::size_t basin = basins.of[path[i]];
                if (basin == kNoBasin) {
                    ++onLand;
                } else if (std::find(hollows.begin(), hollows.end(), basin) == hollows.end()) {
                    hollows.push_back(basin);
                    usable = usable && canHoldPond(basins.list[basin]);
                }
            }
            if (usable && onLand >= kMinStreamSamples && hollows.size() <= maxLakes) {
                for (const std::size_t basin : hollows) {
                    addLake(basin, basins.list[basin].spill, true, 1U);
                }
                way = std::move(path);
                break;
            }
        }
    }

    // Runs of the stream between its spring, its ponds and the rim. The last
    // sample of the way lies outside the outline; the one before it on land.
    if (!way.empty()) {
        Stream run;
        run.width = width;
        bool inPond = false;
        for (std::size_t i = 0; i + 1U < way.size(); ++i) {
            const std::size_t k = way[i];
            const math::Vec2 p = grid.position(k);
            const std::size_t basin = basins.of[k];
            if (basin == kNoBasin) {
                if (inPond) {
                    const std::size_t pond = run.lake;
                    run = Stream{};
                    run.width = width;
                    run.fromLake = true;
                    run.sourceLake = pond;
                    inPond = false;
                }
                run.path.push_back(math::Vec3{p.x, grid.height[k] + kStreamLift, p.y});
            } else if (!inPond) {
                run.mouth = StreamMouth::Lake;
                run.lake = lakeOfBasin[basin];
                const float level = water.lakes[run.lake].level;
                run.path.push_back(math::Vec3{p.x, std::min(run.path.back().y, level), p.y});
                water.streams.push_back(run);
                inPond = true;
            }
        }
        // Where the last step crosses the outline.
        const math::Vec2 last = grid.position(way[way.size() - 2U]);
        const math::Vec2 next = grid.position(way.back());
        double inside = 0.0;
        double outside = 1.0;
        for (int n = 0; n < kRimBisections; ++n) {
            const double middle = 0.5 * (inside + outside);
            if (outlineDistance(shape, pointBetween(last, next, middle)) < 1.0F) {
                inside = middle;
            } else {
                outside = middle;
            }
        }
        const math::Vec2 rim = pointBetween(last, next, inside);
        run.mouth = StreamMouth::Rim;
        run.lake = 0;
        run.path.push_back(math::Vec3{rim.x, std::min(run.path.back().y, kStreamLift), rim.y});
        water.streams.push_back(run);

        // A waterfall with waterfallChance where the stream leaves over the
        // rim, unless it would fall through another island.
        Pcg32 fallRandom{subKey(waterSeed, kWaterfallKey)};
        if (fallRandom.nextBool(settings.water.waterfallChance)) {
            const float length =
                island.undersideDepth * fallRandom.nextFloat(kMinFallLength, kMaxFallLength);
            const float speed = fallRandom.nextFloat(kMinFallSpeed, kMaxFallSpeed);
            const double rimX = static_cast<double>(rim.x);
            const double rimZ = static_cast<double>(rim.y);
            const double rimLength = std::sqrt(rimX * rimX + rimZ * rimZ);
            const math::Vec3 mouth = run.path.back();
            Waterfall fall;
            fall.outward = math::Vec2{static_cast<float>(rimX / rimLength),
                                      static_cast<float>(rimZ / rimLength)};
            fall.top = math::Vec3{mouth.x + fall.outward.x * kWaterfallMargin, mouth.y,
                                  mouth.z + fall.outward.y * kWaterfallMargin};
            fall.width = kFallWidthFactor * width;
            fall.length = length;
            // The water leaves the rim horizontally at `speed` and falls
            // freely: it has moved this far out when it has fallen `length`.
            fall.drift =
                static_cast<float>(static_cast<double>(speed) *
                                   std::sqrt(2.0 * static_cast<double>(length) / kGravity));
            if (!fallBlocked(fall, island, islands)) {
                water.waterfalls.push_back(fall);
            }
        }
    }

    // Lakes, with lakeChance per suitable basin, deepest first, while fewer
    // than maxLakesPerIsland lakes and ponds exist; each is filled to a drawn
    // share of its basin's depth.
    std::vector<std::size_t> candidates;
    for (std::size_t b = 0; b < basins.list.size(); ++b) {
        if (lakeOfBasin[b] == kNoLake && canHoldLake(basins.list[b])) {
            candidates.push_back(b);
        }
    }
    std::sort(candidates.begin(), candidates.end(), [&](std::size_t a, std::size_t b) {
        const Basin& basinA = basins.list[a];
        const Basin& basinB = basins.list[b];
        const float depthA = basinA.spill - basinA.bottom;
        const float depthB = basinB.spill - basinB.bottom;
        if (depthA != depthB) {
            return depthA > depthB;
        }
        return basinA.deepest < basinB.deepest;
    });
    Pcg32 lakeRandom{subKey(waterSeed, kLakeKey)};
    for (const std::size_t b : candidates) {
        if (water.lakes.size() >= maxLakes) {
            break;
        }
        if (!lakeRandom.nextBool(settings.water.lakeChance)) {
            continue;
        }
        const Basin& basin = basins.list[b];
        const float fill = lakeRandom.nextFloat(kMinFill, kMaxFill);
        const auto level =
            static_cast<float>(static_cast<double>(basin.bottom) +
                               static_cast<double>(fill) * (static_cast<double>(basin.spill) -
                                                            static_cast<double>(basin.bottom)));
        addLake(b, level, false, kMinLakeSamples);
    }

    // The mesh: lakes, then the stream's runs, then the waterfall.
    for (std::size_t l = 0; l < water.lakes.size(); ++l) {
        Lake& lake = water.lakes[l];
        lake.mesh.firstTriangle = water.mesh.triangleCount();
        addLakeSurface(water.mesh, grid, shape, lakeCells[l], lake.level);
        lake.mesh.triangleCount = water.mesh.triangleCount() - lake.mesh.firstTriangle;
    }
    for (Stream& stream : water.streams) {
        stream.mesh.firstTriangle = water.mesh.triangleCount();
        addRibbon(water.mesh, stream.path, 0.5F * stream.width, kStreamColor);
        stream.mesh.triangleCount = water.mesh.triangleCount() - stream.mesh.firstTriangle;
    }
    for (Waterfall& fall : water.waterfalls) {
        fall.mesh.firstTriangle = water.mesh.triangleCount();
        addWaterfall(water.mesh, fall);
        fall.mesh.triangleCount = water.mesh.triangleCount() - fall.mesh.firstTriangle;
    }
    return water;
}

} // namespace engine::worldgen
