#pragma once

// Import of a documented subset of Wavefront OBJ into a MeshData (ADR 0012).
// OBJ is the M2 proof format for models made in external tools; glTF 2.0
// remains the candidate for the M6 content pipeline.
//
// Supported statements, one per line:
//   v x y z [w]       position; w is ignored
//   vt u [v [w]]      texture coordinate, V up as in MeshData; v defaults to 0,
//                     w is ignored
//   vn x y z          normal, any non-zero length; normalized on import
//   f c1 c2 c3 ...    face with 3 or more corners, counter-clockwise seen from
//                     its front. Polygons are split into a triangle fan from
//                     the first corner, which is right for convex polygons
//                     (convexity is not checked). Each corner is v, v/vt,
//                     v//vn or v/vt/vn, the same form for all corners of a face.
// Indices start at 1; negative indices count back from the latest element
// (-1 is the last v, vt or vn so far). An index must refer to an element
// defined earlier in the file. Either all faces have texture coordinates or
// none.
//
// The mesh is flat-shaded: every face gets vertices of its own. A face without
// normals gets its flat normal (faceNormal); a degenerate face without normals
// is an error. Vertex colors are white.
//
// '#' starts a comment. Blank lines, spaces, tabs and LF or CRLF line endings
// are fine. Any other statement (o, g, s, usemtl, mtllib, l, ...) is skipped
// and counted. Numbers must be finite; a leading '+' is accepted. Input
// without faces is an error. Errors name the line: "line 12: ...".

#include "engine/core/Result.hpp"
#include "engine/geometry/MeshData.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace engine::geometry {

// Limits for OBJ input, which is untrusted. The defaults are far above what a
// low-poly model needs and keep memory use below about 100 MB: at most 32 MB
// for the elements and about 60 MB for the resulting mesh.
struct ObjLimits {
    // Size of the text; readObj checks the file size before reading it.
    std::size_t maxBytes = 64 * 1024 * 1024;
    // Statements of each kind: v, vt and vn.
    std::size_t maxElements = 1'000'000;
    // Corners of all faces together; each becomes one vertex of the mesh.
    std::size_t maxCorners = 1'000'000;
};

// ObjMesh::ignoredKeywords lists at most this many keywords, each at most
// kMaxListedObjKeywordLength characters long.
inline constexpr std::size_t kMaxListedObjKeywords = 16;
inline constexpr std::size_t kMaxListedObjKeywordLength = 32;

struct ObjMesh {
    // Valid (geometry::validate()); normals always present, UVs when the
    // faces have them.
    MeshData mesh;
    // Statements outside the subset, skipped.
    std::size_t ignoredStatements = 0;
    // Their keywords, sorted, each once. Keywords beyond kMaxListedObjKeywords
    // are counted but not listed.
    std::vector<std::string> ignoredKeywords;
};

// Parses OBJ text (see above). Leaves reporting the ignored statements to the
// caller.
[[nodiscard]] core::Result<ObjMesh> parseObj(std::string_view text, const ObjLimits& limits = {});

// Reads and parses an OBJ file. Errors name the file. Logs a warning with the
// ignored keywords when statements were skipped.
[[nodiscard]] core::Result<ObjMesh> readObj(const std::filesystem::path& path,
                                            const ObjLimits& limits = {});

} // namespace engine::geometry
