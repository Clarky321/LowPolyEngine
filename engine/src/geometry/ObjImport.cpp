#include "engine/geometry/ObjImport.hpp"

#include "MeshLimits.hpp"

#include "engine/core/FileSystem.hpp"
#include "engine/core/Log.hpp"
#include "engine/geometry/FaceNormal.hpp"

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <exception>
#include <format>
#include <optional>
#include <system_error>
#include <utility>

namespace engine::geometry {

namespace {

// Tokens are quoted in messages with at most this many characters.
constexpr std::size_t kMaxQuotedLength = 40;

constexpr math::Vec4 kWhite{1.0F};

core::Error lineError(std::size_t line, const std::string& message)
{
    return core::Error{std::format("line {}: {}", line, message)};
}

// A token for a message: shortened, and bytes that are not printable ASCII
// replaced, so binary garbage cannot flood or garble the log.
std::string quoteToken(std::string_view token)
{
    std::string text = "'";
    for (const char c : token.substr(0, kMaxQuotedLength)) {
        text += (c >= ' ' && c <= '~') ? c : '?';
    }
    if (token.size() > kMaxQuotedLength) {
        text += "...";
    }
    return text + "'";
}

// Splits a line at spaces and tabs.
void tokenize(std::string_view line, std::vector<std::string_view>& tokens)
{
    tokens.clear();
    std::size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
            ++i;
        }
        const std::size_t start = i;
        while (i < line.size() && line[i] != ' ' && line[i] != '\t') {
            ++i;
        }
        if (i > start) {
            tokens.push_back(line.substr(start, i - start));
        }
    }
}

// from_chars does not accept a leading '+'.
std::string_view withoutPlus(std::string_view token)
{
    if (token.size() > 1 && token[0] == '+' && token[1] != '+' && token[1] != '-') {
        token.remove_prefix(1);
    }
    return token;
}

std::optional<core::Error> parseFloat(std::string_view token, std::size_t line, float& value)
{
    const std::string_view digits = withoutPlus(token);
    const char* end = digits.data() + digits.size();
    const auto [stop, error] = std::from_chars(digits.data(), end, value);
    // Checked first: on a range error from_chars has not looked past the
    // digits, so "1e999x" must still count as no number at all.
    if (error == std::errc::invalid_argument || stop != end) {
        return lineError(line, quoteToken(token) + " is not a number");
    }
    if (error == std::errc::result_out_of_range) {
        return lineError(line, quoteToken(token) + " is outside the float range");
    }
    if (!std::isfinite(value)) {
        return lineError(line, quoteToken(token) + " is not a finite number");
    }
    return std::nullopt;
}

// Parses the numbers after the keyword: at least `minimum`, at most `maximum`;
// unused trailing slots of `values` are left as they are.
std::optional<core::Error> parseNumbers(const std::vector<std::string_view>& tokens,
                                        std::size_t minimum, std::size_t maximum, std::size_t line,
                                        std::array<float, 4>& values)
{
    const std::size_t count = tokens.size() - 1;
    if (count < minimum || count > maximum) {
        std::string expected = std::to_string(minimum);
        if (maximum == minimum + 1) {
            expected += std::format(" or {}", maximum);
        } else if (maximum > minimum) {
            expected += std::format(" to {}", maximum);
        }
        return lineError(
            line, std::format("'{}' expects {} numbers, got {}", tokens[0], expected, count));
    }
    for (std::size_t i = 0; i < count; ++i) {
        if (auto error = parseFloat(tokens[i + 1], line, values[i])) {
            return error;
        }
    }
    return std::nullopt;
}

// An index into `count` elements of one kind defined so far: 1-based, or
// negative counting back from the latest one.
std::optional<core::Error> resolveIndex(std::string_view token, const char* kind, std::size_t count,
                                        std::size_t line, std::size_t& index)
{
    const std::string_view digits = withoutPlus(token);
    const char* end = digits.data() + digits.size();
    std::int64_t value = 0;
    const auto [stop, error] = std::from_chars(digits.data(), end, value);
    if (error != std::errc{} || stop != end) {
        return lineError(line, quoteToken(token) + " is not an index");
    }
    if (value == 0) {
        return lineError(line, std::format("{} index 0 is invalid: indices start at 1, negative "
                                           "ones count back from the latest element",
                                           kind));
    }
    // count is at most ObjLimits::maxElements, far below the int64 range.
    const auto defined = static_cast<std::int64_t>(count);
    const std::int64_t resolved = value > 0 ? value - 1 : defined + value;
    if (resolved < 0 || resolved >= defined) {
        return lineError(line, std::format("{} index {} is out of range ({} defined so far)", kind,
                                           value, count));
    }
    index = static_cast<std::size_t>(resolved);
    return std::nullopt;
}

// Indices into Elements. Plain flags instead of std::optional keep GCC's
// maybe-uninitialized analysis quiet.
struct Corner {
    std::size_t position = 0;
    std::size_t uv = 0;
    std::size_t normal = 0;
    bool hasUv = false;
    bool hasNormal = false;
};

// The elements read so far.
struct Elements {
    std::vector<math::Vec3> positions;
    std::vector<math::Vec2> uvs;
    std::vector<math::Vec3> normals;
};

// One face corner: v, v/vt, v//vn or v/vt/vn.
std::optional<core::Error> parseCorner(std::string_view token, const Elements& elements,
                                       std::size_t line, Corner& corner)
{
    std::array<std::string_view, 3> parts{};
    std::size_t partCount = 0;
    std::size_t start = 0;
    while (true) {
        const std::size_t slash = token.find('/', start);
        if (partCount == parts.size()) {
            partCount = parts.size() + 1; // too many parts
            break;
        }
        parts[partCount++] = token.substr(
            start, slash == std::string_view::npos ? std::string_view::npos : slash - start);
        if (slash == std::string_view::npos) {
            break;
        }
        start = slash + 1;
    }
    const bool valid = partCount <= parts.size() && !parts[0].empty() &&
                       (partCount != 2 || !parts[1].empty()) &&
                       (partCount != 3 || !parts[2].empty());
    if (!valid) {
        return lineError(line,
                         quoteToken(token) + " is not a face corner (v, v/vt, v//vn or v/vt/vn)");
    }

    corner = Corner{};
    if (auto error =
            resolveIndex(parts[0], "position", elements.positions.size(), line, corner.position)) {
        return error;
    }
    if (partCount >= 2 && !parts[1].empty()) {
        if (auto error = resolveIndex(parts[1], "texture coordinate", elements.uvs.size(), line,
                                      corner.uv)) {
            return error;
        }
        corner.hasUv = true;
    }
    if (partCount == 3) {
        if (auto error =
                resolveIndex(parts[2], "normal", elements.normals.size(), line, corner.normal)) {
            return error;
        }
        corner.hasNormal = true;
    }
    return std::nullopt;
}

class ObjParser {
public:
    explicit ObjParser(const ObjLimits& limits)
        : limits_(limits), maxCorners_(std::min(limits.maxCorners, detail::kMaxVertexCount))
    {
    }

    std::optional<core::Error> parseLine(std::string_view text, std::size_t line)
    {
        tokenize(text, tokens_);
        if (tokens_.empty()) {
            return std::nullopt;
        }
        const std::string_view keyword = tokens_[0];
        if (keyword == "v") {
            return parsePosition(line);
        }
        if (keyword == "vt") {
            return parseUv(line);
        }
        if (keyword == "vn") {
            return parseNormal(line);
        }
        if (keyword == "f") {
            return parseFace(line);
        }
        ignore(keyword);
        return std::nullopt;
    }

    [[nodiscard]] bool hasFaces() const noexcept { return faceCount_ > 0; }

    ObjMesh take() { return std::move(result_); }

private:
    std::optional<core::Error> checkElementLimit(std::size_t count, const char* what,
                                                 std::size_t line) const
    {
        if (count >= limits_.maxElements) {
            return lineError(line, std::format("more than {} {} ('{}' statements)",
                                               limits_.maxElements, what, tokens_[0]));
        }
        return std::nullopt;
    }

    std::optional<core::Error> parsePosition(std::size_t line)
    {
        if (auto error = checkElementLimit(elements_.positions.size(), "positions", line)) {
            return error;
        }
        std::array<float, 4> values{};
        if (auto error = parseNumbers(tokens_, 3, 4, line, values)) {
            return error;
        }
        elements_.positions.emplace_back(values[0], values[1], values[2]);
        return std::nullopt;
    }

    std::optional<core::Error> parseUv(std::size_t line)
    {
        if (auto error = checkElementLimit(elements_.uvs.size(), "texture coordinates", line)) {
            return error;
        }
        std::array<float, 4> values{};
        if (auto error = parseNumbers(tokens_, 1, 3, line, values)) {
            return error;
        }
        elements_.uvs.emplace_back(values[0], values[1]);
        return std::nullopt;
    }

    std::optional<core::Error> parseNormal(std::size_t line)
    {
        if (auto error = checkElementLimit(elements_.normals.size(), "normals", line)) {
            return error;
        }
        std::array<float, 4> values{};
        if (auto error = parseNumbers(tokens_, 3, 3, line, values)) {
            return error;
        }
        // Normalized in double: finite floats can neither overflow nor
        // underflow there.
        const glm::dvec3 normal{static_cast<double>(values[0]), static_cast<double>(values[1]),
                                static_cast<double>(values[2])};
        const double length = glm::length(normal);
        if (!(length > 0.0)) {
            return lineError(line, "normal (0, 0, 0) has no direction");
        }
        const glm::dvec3 unit = normal / length;
        elements_.normals.emplace_back(static_cast<float>(unit.x), static_cast<float>(unit.y),
                                       static_cast<float>(unit.z));
        return std::nullopt;
    }

    std::optional<core::Error> parseFace(std::size_t line)
    {
        const std::size_t cornerCount = tokens_.size() - 1;
        if (cornerCount < 3) {
            return lineError(line,
                             std::format("a face needs at least 3 corners, got {}", cornerCount));
        }
        MeshData& mesh = result_.mesh;
        if (cornerCount > maxCorners_ - mesh.positions.size()) {
            return lineError(line, std::format("more than {} face corners in total", maxCorners_));
        }

        corners_.resize(cornerCount);
        for (std::size_t i = 0; i < cornerCount; ++i) {
            if (auto error = parseCorner(tokens_[i + 1], elements_, line, corners_[i])) {
                return error;
            }
        }
        const bool hasUvs = corners_[0].hasUv;
        const bool hasNormals = corners_[0].hasNormal;
        for (const Corner& corner : corners_) {
            if (corner.hasUv != hasUvs || corner.hasNormal != hasNormals) {
                return lineError(line, "all corners of a face must have the same form (v, "
                                       "v/vt, v//vn or v/vt/vn)");
            }
        }
        if (faceCount_ > 0 && hasUvs != meshHasUvs_) {
            return lineError(line, hasUvs ? "this face has texture coordinates and earlier faces "
                                            "do not; all faces must have them or none"
                                          : "this face has no texture coordinates and earlier "
                                            "faces do; all faces must have them or none");
        }

        math::Vec3 flatNormal{0.0F};
        if (!hasNormals) {
            facePositions_.clear();
            for (const Corner& corner : corners_) {
                facePositions_.push_back(elements_.positions[corner.position]);
            }
            const auto normal = faceNormal(facePositions_);
            if (!normal) {
                return lineError(line, "the face has no normals and is degenerate (its corners "
                                       "lie on one line or point), so it has no flat normal");
            }
            flatNormal = *normal;
        }

        // Fits: the corner limit is at most kMaxVertexCount.
        const auto first = static_cast<std::uint32_t>(mesh.positions.size());
        for (const Corner& corner : corners_) {
            mesh.positions.push_back(elements_.positions[corner.position]);
            mesh.colors.push_back(kWhite);
            mesh.normals.push_back(hasNormals ? elements_.normals[corner.normal] : flatNormal);
            if (hasUvs) {
                mesh.uvs.push_back(elements_.uvs[corner.uv]);
            }
        }
        for (std::uint32_t i = 1; i + 1 < cornerCount; ++i) {
            mesh.indices.insert(mesh.indices.end(), {first, first + i, first + i + 1});
        }
        meshHasUvs_ = hasUvs;
        ++faceCount_;
        return std::nullopt;
    }

    void ignore(std::string_view keyword)
    {
        ++result_.ignoredStatements;
        std::vector<std::string>& listed = result_.ignoredKeywords;
        const std::string name{keyword.substr(0, kMaxListedObjKeywordLength)};
        const auto position = std::lower_bound(listed.begin(), listed.end(), name);
        if ((position == listed.end() || *position != name) &&
            listed.size() < kMaxListedObjKeywords) {
            listed.insert(position, name);
        }
    }

    ObjLimits limits_;
    std::size_t maxCorners_ = 0;
    Elements elements_;
    ObjMesh result_;
    std::size_t faceCount_ = 0;
    bool meshHasUvs_ = false;
    // Reused for every line.
    std::vector<std::string_view> tokens_;
    std::vector<Corner> corners_;
    std::vector<math::Vec3> facePositions_;
};

core::Result<ObjMesh> parse(std::string_view text, const ObjLimits& limits)
{
    if (text.empty()) {
        return core::Error{"OBJ data is empty"};
    }
    if (text.size() > limits.maxBytes) {
        return core::Error{std::format("OBJ data has {} bytes; at most {} are accepted",
                                       text.size(), limits.maxBytes)};
    }

    ObjParser parser(limits);
    std::size_t line = 0;
    std::size_t start = 0;
    while (start < text.size()) {
        ++line;
        const std::size_t newline = text.find('\n', start);
        const std::size_t end = newline == std::string_view::npos ? text.size() : newline;
        std::string_view content = text.substr(start, end - start);
        if (!content.empty() && content.back() == '\r') {
            content.remove_suffix(1);
        }
        content = content.substr(0, content.find('#'));
        if (auto error = parser.parseLine(content, line)) {
            return std::move(*error);
        }
        start = end + 1;
    }
    if (!parser.hasFaces()) {
        return core::Error{"OBJ data has no faces ('f' statements)"};
    }

    ObjMesh result = parser.take();
    // The parser only builds valid meshes; checked anyway, since the renderer
    // relies on it.
    if (auto error = validate(result.mesh)) {
        return core::Error{"the imported mesh is invalid: " + error->message};
    }
    return result;
}

} // namespace

core::Result<ObjMesh> parseObj(std::string_view text, const ObjLimits& limits)
{
    try {
        return parse(text, limits);
    } catch (const std::exception&) {
        // bad_alloc must not escape: callers expect a Result.
        return core::Error{"not enough memory to import the OBJ data"};
    }
}

core::Result<ObjMesh> readObj(const std::filesystem::path& path, const ObjLimits& limits)
{
    const auto failure = [&path](const std::string& reason) {
        return core::Error{"cannot read OBJ '" + core::toUtf8(path) + "': " + reason};
    };
    // Check the size before reading: the whole file is loaded into memory.
    std::error_code error;
    const auto fileSize = std::filesystem::file_size(path, error);
    if (!error && fileSize > limits.maxBytes) {
        return failure(std::format("the file has {} bytes; at most {} are accepted", fileSize,
                                   limits.maxBytes));
    }
    // A missing file or a folder is reported by readTextFile, with the path.
    auto contents = core::readTextFile(path);
    if (!contents) {
        return contents.error();
    }
    auto result = parseObj(contents.value(), limits);
    if (!result) {
        return failure(result.error().message);
    }

    const ObjMesh& mesh = result.value();
    if (mesh.ignoredStatements > 0) {
        std::string keywords;
        for (const std::string& keyword : mesh.ignoredKeywords) {
            keywords += (keywords.empty() ? "" : ", ") + quoteToken(keyword);
        }
        if (mesh.ignoredKeywords.size() == kMaxListedObjKeywords) {
            keywords += ", ...";
        }
        core::logWarning("geometry",
                         "OBJ '{}': skipped {} statement(s) outside the supported subset: {}",
                         core::toUtf8(path), mesh.ignoredStatements, keywords);
    }
    return result;
}

} // namespace engine::geometry
