// OBJ subset import (engine/geometry/ObjImport.hpp, ADR 0012). The fixtures are
// inline OBJ texts, so each test shows its input next to its expectations.

#include "engine/geometry/ObjImport.hpp"

#include "engine/geometry/MeshData.hpp"

#include <glm/geometric.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

using engine::geometry::kMaxListedObjKeywordLength;
using engine::geometry::kMaxListedObjKeywords;
using engine::geometry::ObjLimits;
using engine::geometry::ObjMesh;
using engine::geometry::parseObj;
using engine::geometry::readObj;
using engine::math::Vec2;
using engine::math::Vec3;
using engine::math::Vec4;

namespace {

// Three positions and a triangle-friendly set of UVs and normals; the line
// after them is line 8.
const std::string kElements = "v 0 0 0\n"
                              "v 2 0 0\n"
                              "v 0 2 0\n"
                              "vt 0 0\n"
                              "vt 1 0\n"
                              "vt 0 1\n"
                              "vn 0 3 4\n";

ObjMesh parsed(const std::string& text, const ObjLimits& limits = {})
{
    auto result = parseObj(text, limits);
    if (!result) {
        FAIL("parseObj failed: " << result.error().message);
    }
    CHECK_FALSE(engine::geometry::validate(result.value().mesh).has_value());
    return std::move(result).value();
}

std::string failure(const std::string& text, const ObjLimits& limits = {})
{
    auto result = parseObj(text, limits);
    REQUIRE_FALSE(result.hasValue());
    return result.error().message;
}

bool near(const Vec3& actual, const Vec3& expected)
{
    return glm::length(actual - expected) < 1.0e-6F;
}

bool contains(const std::string& text, const std::string& part)
{
    return text.find(part) != std::string::npos;
}

// Creates a file in the system temp folder and removes it at scope exit.
class TempFile {
public:
    TempFile(const std::string& name, const std::string& contents)
        : path_(std::filesystem::temp_directory_path() / name)
    {
        std::ofstream file(path_, std::ios::out | std::ios::binary | std::ios::trunc);
        file << contents;
    }
    ~TempFile()
    {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }
    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

} // namespace

TEST_CASE("A triangle of positions gets white colors and its flat normal", "[geometry][obj]")
{
    const ObjMesh obj = parsed("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
    const auto& mesh = obj.mesh;
    REQUIRE(mesh.vertexCount() == 3);
    CHECK(mesh.positions[1] == Vec3{1, 0, 0});
    CHECK(mesh.colors == std::vector<Vec4>(3, Vec4{1.0F}));
    CHECK(mesh.normals == std::vector<Vec3>(3, Vec3{0, 0, 1}));
    CHECK_FALSE(mesh.hasUvs());
    CHECK(mesh.indices == std::vector<std::uint32_t>{0, 1, 2});
    CHECK(obj.ignoredStatements == 0);
    CHECK(obj.ignoredKeywords.empty());
}

TEST_CASE("All four corner forms are read", "[geometry][obj]")
{
    const Vec3 givenNormal{0.0F, 0.6F, 0.8F}; // vn 0 3 4, normalized
    const std::vector<Vec2> uvs{{0, 0}, {1, 0}, {0, 1}};

    SECTION("v")
    {
        const auto mesh = parsed(kElements + "f 1 2 3\n").mesh;
        CHECK_FALSE(mesh.hasUvs());
        CHECK(near(mesh.normals[0], Vec3{0, 0, 1}));
    }
    SECTION("v/vt")
    {
        const auto mesh = parsed(kElements + "f 1/1 2/2 3/3\n").mesh;
        CHECK(mesh.uvs == uvs);
        CHECK(near(mesh.normals[0], Vec3{0, 0, 1}));
    }
    SECTION("v//vn: given normals win over the flat one")
    {
        const auto mesh = parsed(kElements + "f 1//1 2//1 3//1\n").mesh;
        CHECK_FALSE(mesh.hasUvs());
        for (const Vec3& normal : mesh.normals) {
            CHECK(near(normal, givenNormal));
        }
    }
    SECTION("v/vt/vn")
    {
        const auto mesh = parsed(kElements + "f 1/1/1 2/2/1 3/3/1\n").mesh;
        CHECK(mesh.uvs == uvs);
        CHECK(near(mesh.normals[2], givenNormal));
    }
}

TEST_CASE("Negative indices count back from the latest element", "[geometry][obj]")
{
    const auto mesh = parsed(kElements + "f -3/-3/-1 -2/-2/-1 -1/-1/-1\n"
                                         "v 5 5 5\n"
                                         "f 1/1/1 2/2/1 -1/-1/-1\n")
                          .mesh;
    REQUIRE(mesh.vertexCount() == 6);
    CHECK(mesh.positions[0] == Vec3{0, 0, 0});
    CHECK(mesh.positions[2] == Vec3{0, 2, 0});
    // -1 now refers to the position added after the first face.
    CHECK(mesh.positions[5] == Vec3{5, 5, 5});
    CHECK(mesh.uvs[5] == Vec2{0, 1});
}

TEST_CASE("Quads and convex polygons become triangle fans", "[geometry][obj]")
{
    const auto mesh = parsed("v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nv -1 0.5 0\n"
                             "f 1 2 3 4\n"
                             "f 1 2 3 4 5\n")
                          .mesh;
    REQUIRE(mesh.vertexCount() == 9);
    CHECK(mesh.indices == std::vector<std::uint32_t>{0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7, 4, 7, 8});
    CHECK(mesh.triangleCount() == 5);
    CHECK(mesh.normals == std::vector<Vec3>(9, Vec3{0, 0, 1}));
}

TEST_CASE("Every face gets vertices of its own", "[geometry][obj]")
{
    // Two triangles folded along their shared edge 1-3.
    const auto mesh = parsed("v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 1\nf 1 2 3\nf 1 3 4\n").mesh;
    REQUIRE(mesh.vertexCount() == 6);
    CHECK(mesh.positions[0] == mesh.positions[3]);
    CHECK(near(mesh.normals[0], Vec3{0, 0, 1}));
    CHECK(near(mesh.normals[3], glm::normalize(Vec3{1, -1, 1})));
}

TEST_CASE("Comments, blank lines, tabs, CRLF and optional components are accepted",
          "[geometry][obj]")
{
    const ObjMesh obj = parsed("# exported by hand\r\n"
                               "\r\n"
                               "v\t0 0 0 1\r\n"
                               "   v 1 0 0   # trailing comment\r\n"
                               "v +0 1.0e0 0\r\n"
                               "vt 0.5\r\n"
                               "vt 0.25 0.75 0\r\n"
                               "vt 1 1\r\n"
                               "#f 9 9 9\r\n"
                               "f 1/1 2/2 3/3");
    const auto& mesh = obj.mesh;
    REQUIRE(mesh.vertexCount() == 3);
    CHECK(mesh.positions[0] == Vec3{0, 0, 0}); // w ignored
    CHECK(mesh.positions[2] == Vec3{0, 1, 0});
    CHECK(mesh.uvs[0] == Vec2{0.5F, 0.0F}); // v defaults to 0
    CHECK(mesh.uvs[1] == Vec2{0.25F, 0.75F});
    CHECK(obj.ignoredStatements == 0);
}

TEST_CASE("Statements outside the subset are skipped and counted", "[geometry][obj]")
{
    const ObjMesh obj = parsed("mtllib model.mtl\n"
                               "o Cube\n"
                               "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
                               "g side\n"
                               "usemtl Material\n"
                               "s off\n"
                               "l 1 2\n"
                               "f 1 2 3\n"
                               "s 1\n");
    CHECK(obj.mesh.triangleCount() == 1);
    CHECK(obj.ignoredStatements == 7);
    CHECK(obj.ignoredKeywords == std::vector<std::string>{"g", "l", "mtllib", "o", "s", "usemtl"});

    // The list is capped; the count is not.
    std::string many = "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
    for (int i = 0; i < 20; ++i) {
        many += "k" + std::to_string(10 + i) + "\n";
    }
    many += std::string(100, 'x') + "\n";
    const ObjMesh capped = parsed(many);
    CHECK(capped.ignoredStatements == 21);
    REQUIRE(capped.ignoredKeywords.size() == kMaxListedObjKeywords);
    CHECK(capped.ignoredKeywords.front() == "k10");
    for (const std::string& keyword : capped.ignoredKeywords) {
        CHECK(keyword.size() <= kMaxListedObjKeywordLength);
    }
}

TEST_CASE("Malformed lines are reported with their line number", "[geometry][obj]")
{
    struct Case {
        std::string line;
        std::string message;
    };
    // kElements fills lines 1 to 7, so the line under test is line 8.
    const std::vector<Case> cases{
        {"v 1 2", "line 8: 'v' expects 3 or 4 numbers, got 2"},
        {"v 1 2 3 4 5", "line 8: 'v' expects 3 or 4 numbers, got 5"},
        {"v 1 x 3", "line 8: 'x' is not a number"},
        {"v 1 2 3abc", "line 8: '3abc' is not a number"},
        {"v 1 2 \x01\x02", "line 8: '??' is not a number"},
        {"v 1 2 " + std::string(60, '7') + "z",
         "line 8: '" + std::string(40, '7') + "...' is not a number"},
        {"vt", "line 8: 'vt' expects 1 to 3 numbers, got 0"},
        {"vn 1 0", "line 8: 'vn' expects 3 numbers, got 2"},
        {"vn 0 0 0", "line 8: normal (0, 0, 0) has no direction"},
        {"f 1 2", "line 8: a face needs at least 3 corners, got 2"},
        {"f 1 2 3/", "line 8: '3/' is not a face corner (v, v/vt, v//vn or v/vt/vn)"},
        {"f 1/1/1/1 2 3", "line 8: '1/1/1/1' is not a face corner"},
        {"f 1// 2// 3//", "line 8: '1//' is not a face corner"},
        {"f /1 2 3", "line 8: '/1' is not a face corner"},
        {"f a 2 3", "line 8: 'a' is not an index"},
        {"f 1 2 3.5", "line 8: '3.5' is not an index"},
        {"f 1 2 99999999999999999999", "line 8: '99999999999999999999' is not an index"},
        {"f 0 1 2", "line 8: position index 0 is invalid"},
        {"f 1/1 2 3", "line 8: all corners of a face must have the same form"},
        {"f 1 2/2 3", "line 8: all corners of a face must have the same form"},
        {"f 1 1 2", "line 8: the face has no normals and is degenerate"},
    };
    for (const Case& test : cases) {
        INFO("line '" << test.line << "'");
        const std::string message = failure(kElements + test.line + "\n");
        INFO("message: " << message);
        CHECK(contains(message, test.message));
    }

    // All faces have texture coordinates or none.
    CHECK(contains(failure(kElements + "f 1/1 2/2 3/3\nf 1 2 3\n"),
                   "line 9: this face has no texture coordinates and earlier faces do"));
    CHECK(contains(failure(kElements + "f 1 2 3\nf 1/1 2/2 3/3\n"),
                   "line 9: this face has texture coordinates and earlier faces do not"));
}

TEST_CASE("Indices must refer to elements defined earlier", "[geometry][obj]")
{
    CHECK(contains(failure(kElements + "f 1 2 4\n"),
                   "line 8: position index 4 is out of range (3 defined so far)"));
    CHECK(contains(failure(kElements + "f -4 1 2\n"),
                   "line 8: position index -4 is out of range (3 defined so far)"));
    CHECK(contains(failure(kElements + "f 1/4 2/1 3/1\n"),
                   "line 8: texture coordinate index 4 is out of range (3 defined so far)"));
    CHECK(contains(failure(kElements + "f 1//2 2//1 3//1\n"),
                   "line 8: normal index 2 is out of range (1 defined so far)"));
    // A position defined after the face does not count.
    CHECK(contains(failure("v 0 0 0\nv 1 0 0\nf 1 2 3\nv 0 1 0\n"),
                   "line 3: position index 3 is out of range (2 defined so far)"));
}

TEST_CASE("Non-finite and out-of-range numbers are rejected", "[geometry][obj]")
{
    CHECK(contains(failure("v nan 0 0\n"), "line 1: 'nan' is not a finite number"));
    CHECK(contains(failure("v 0 inf 0\n"), "line 1: 'inf' is not a finite number"));
    CHECK(contains(failure("v 0 0 -infinity\n"), "line 1: '-infinity' is not a finite number"));
    CHECK(contains(failure("vt -inf\n"), "line 1: '-inf' is not a finite number"));
    CHECK(contains(failure("vn 0 0 1e999\n"), "line 1: '1e999' is outside the float range"));
}

TEST_CASE("Size, element and corner limits are enforced", "[geometry][obj]")
{
    const std::string triangle = "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
    CHECK(contains(failure(triangle, ObjLimits{.maxBytes = 10}),
                   "OBJ data has 32 bytes; at most 10 are accepted"));
    CHECK(contains(failure(triangle + "v 0 0 1\n", ObjLimits{.maxElements = 3}),
                   "line 5: more than 3 positions ('v' statements)"));
    CHECK(contains(failure("vt 0 0\nvt 0 0\n", ObjLimits{.maxElements = 1}),
                   "line 2: more than 1 texture coordinates ('vt' statements)"));
    CHECK(contains(failure("vn 0 0 1\nvn 0 0 1\n", ObjLimits{.maxElements = 1}),
                   "line 2: more than 1 normals ('vn' statements)"));
    CHECK(contains(failure(triangle + "f 1 2 3\n", ObjLimits{.maxCorners = 5}),
                   "line 5: more than 5 face corners in total"));
    // Exactly at the limits is fine.
    CHECK(parsed(triangle, ObjLimits{.maxBytes = 32, .maxElements = 3, .maxCorners = 3})
              .mesh.vertexCount() == 3);
}

TEST_CASE("Empty input and input without faces are errors", "[geometry][obj]")
{
    CHECK(failure("") == "OBJ data is empty");
    CHECK(failure("# nothing but a comment\n\n") == "OBJ data has no faces ('f' statements)");
    CHECK(failure("v 0 0 0\nv 1 0 0\nv 0 1 0\n") == "OBJ data has no faces ('f' statements)");
}

TEST_CASE("readObj reads a file and names it in errors", "[geometry][obj]")
{
    const TempFile good("lpe_obj_test_good.obj", "o Triangle\r\nv 0 0 0\r\nv 1 0 0\r\nv 0 1 0\r\n"
                                                 "f 1 2 3\r\n");
    const auto loaded = readObj(good.path());
    REQUIRE(loaded.hasValue());
    CHECK(loaded.value().mesh.vertexCount() == 3);
    CHECK(loaded.value().ignoredStatements == 1);

    const TempFile broken("lpe_obj_test_broken.obj", "v 0 0 0\nv 1 0\n");
    const auto bad = readObj(broken.path());
    REQUIRE_FALSE(bad.hasValue());
    CHECK(contains(bad.error().message, "cannot read OBJ '"));
    CHECK(contains(bad.error().message, "lpe_obj_test_broken.obj': line 2: 'v' expects"));

    // The size is checked before the file is read.
    const auto large = readObj(good.path(), ObjLimits{.maxBytes = 8});
    REQUIRE_FALSE(large.hasValue());
    CHECK(contains(large.error().message, "lpe_obj_test_good.obj': the file has"));
    CHECK(contains(large.error().message, "at most 8 are accepted"));

    const auto missing = readObj(std::filesystem::temp_directory_path() / "lpe_obj_missing.obj");
    REQUIRE_FALSE(missing.hasValue());
    CHECK(contains(missing.error().message, "lpe_obj_missing.obj"));
}
