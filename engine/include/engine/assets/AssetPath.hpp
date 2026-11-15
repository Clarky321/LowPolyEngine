#pragma once

// A portable, project-relative asset path (M4.3, ADR 0022).
//
// An AssetPath is always valid and normalized: segments separated by '/',
// without empty, "." or ".." segments, relative to a project root that is
// given at runtime and never stored. The rules make a path mean the same file
// on Windows and Linux and keep everything machine-specific out of documents:
// - printable ASCII only (Unicode names compare and normalize differently
//   between file systems), at most kMaxAssetPathBytes bytes;
// - no absolute path, drive letter, UNC path or leading '~' (home folder);
// - '/' separates folders; '\' and the characters Windows forbids
//   (< > : " | ? *) are rejected;
// - ".." may not leave the project root;
// - no segment ends with '.' or ' ' (Windows drops them), and none is a
//   Windows device name (CON, PRN, AUX, NUL, COM1-COM9, LPT1-LPT9, with or
//   without an extension).
// Letter case is kept and matters for equality; folded() gives the form the
// asset registry compares to find paths that differ only in case.

#include "engine/core/Result.hpp"

#include <compare>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

namespace engine::assets {

inline constexpr std::size_t kMaxAssetPathBytes = 256;

// The text in ASCII lower case: the form in which asset paths and file names
// are compared to find names that differ only in letter case.
[[nodiscard]] std::string foldCase(std::string_view text);

class AssetPath {
public:
    // Normalizes and checks a path: "./models//tree.obj" becomes
    // "models/tree.obj", "models/../textures/a.png" becomes "textures/a.png".
    // The error says which rule the text breaks, e.g.
    // "asset path 'C:/tree.obj': a drive letter is not allowed; asset paths
    // are relative to the project root".
    [[nodiscard]] static core::Result<AssetPath> parse(std::string_view text);

    [[nodiscard]] const std::string& text() const noexcept { return text_; }
    // The last segment, e.g. "tree.obj".
    [[nodiscard]] std::string_view fileName() const noexcept;
    // The file name's extension with the dot (".obj"), or empty if it has none
    // or only a leading dot (".gitignore").
    [[nodiscard]] std::string_view extension() const noexcept;
    // The path in ASCII lower case.
    [[nodiscard]] std::string folded() const;

    // The file under a project root.
    [[nodiscard]] std::filesystem::path resolve(const std::filesystem::path& projectRoot) const;

    friend bool operator==(const AssetPath&, const AssetPath&) = default;
    friend auto operator<=>(const AssetPath&, const AssetPath&) = default;

private:
    explicit AssetPath(std::string text) noexcept : text_(std::move(text)) {}

    std::string text_;
};

} // namespace engine::assets
