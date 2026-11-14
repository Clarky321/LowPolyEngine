#include "engine/assets/AssetPath.hpp"

#include "engine/core/Utf8.hpp"

#include <array>
#include <cstdint>
#include <format>
#include <vector>

namespace engine::assets {

namespace {

constexpr std::string_view kRelative = "; asset paths are relative to the project root";

char lowerAscii(char c) noexcept
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool equalsIgnoringCase(std::string_view a, std::string_view b) noexcept
{
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (lowerAscii(a[i]) != lowerAscii(b[i])) {
            return false;
        }
    }
    return true;
}

bool isLetter(char c) noexcept
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

// CON, PRN, AUX, NUL, COM1-COM9, LPT1-LPT9: Windows opens a device instead of
// a file for these names, with any extension.
bool isDeviceName(std::string_view segment) noexcept
{
    const std::string_view stem = segment.substr(0, segment.find('.'));
    static constexpr std::array<std::string_view, 4> kDevices{"con", "prn", "aux", "nul"};
    for (const std::string_view device : kDevices) {
        if (equalsIgnoringCase(stem, device)) {
            return true;
        }
    }
    if (stem.size() == 4 && stem[3] >= '1' && stem[3] <= '9') {
        const std::string_view prefix = stem.substr(0, 3);
        return equalsIgnoringCase(prefix, "com") || equalsIgnoringCase(prefix, "lpt");
    }
    return false;
}

core::Error pathError(std::string_view text, std::string_view problem)
{
    return core::Error{std::format("asset path '{}': {}", text, problem)};
}

} // namespace

core::Result<AssetPath> AssetPath::parse(std::string_view text)
{
    if (text.empty()) {
        return core::Error{"asset path: must not be empty"};
    }
    if (text.size() > kMaxAssetPathBytes) {
        return core::Error{std::format("asset path: {} bytes, at most {} allowed", text.size(),
                                       kMaxAssetPathBytes)};
    }
    // Characters first, without quoting text that may not be printable.
    if (const auto invalid = core::findInvalidUtf8(text)) {
        return core::Error{std::format("asset path: invalid UTF-8 at byte {}", *invalid)};
    }
    for (std::size_t offset = 0; offset < text.size();) {
        const std::size_t start = offset;
        const auto codePoint = core::decodeUtf8At(text, offset);
        if (!codePoint) {
            return core::Error{std::format("asset path: invalid UTF-8 at byte {}", start)};
        }
        const auto value = static_cast<std::uint32_t>(*codePoint);
        if (value < 0x20U || (value >= 0x7FU && value <= 0x9FU)) {
            return core::Error{
                std::format("asset path: control character U+{:04X} at byte {}", value, start)};
        }
        if (value > 0x7FU) {
            return core::Error{std::format("asset path: character U+{:04X} at byte {} is not "
                                           "allowed; asset paths are printable ASCII",
                                           value, start)};
        }
    }

    // Machine-specific forms, named for what they are.
    if (text.starts_with("\\\\") || text.starts_with("//")) {
        return pathError(text, std::string("a UNC path is not allowed") + std::string(kRelative));
    }
    if (text.size() >= 2 && isLetter(text[0]) && text[1] == ':') {
        return pathError(text,
                         std::string("a drive letter is not allowed") + std::string(kRelative));
    }
    if (text.front() == '/') {
        return pathError(text,
                         std::string("an absolute path is not allowed") + std::string(kRelative));
    }
    if (text.front() == '~') {
        return pathError(text, "a leading '~' is not allowed: it names a home folder");
    }
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '\\') {
            return pathError(text, std::format("'\\' at byte {}: use '/' between folders", i));
        }
        if (std::string_view("<>:\"|?*").find(c) != std::string_view::npos) {
            return pathError(text, std::format("'{}' at byte {} is not allowed (Windows forbids "
                                               "< > : \" | ? * in file names)",
                                               c, i));
        }
    }

    // Segments: drop empty and "." ones, resolve "..".
    std::vector<std::string_view> segments;
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t end = text.find('/', start);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        const std::string_view segment = text.substr(start, end - start);
        start = end + 1;
        if (segment.empty() || segment == ".") {
            continue;
        }
        if (segment == "..") {
            if (segments.empty()) {
                return pathError(text, "'..' leaves the project root");
            }
            segments.pop_back();
            continue;
        }
        if (segment.back() == '.' || segment.back() == ' ') {
            return pathError(text, std::format("'{}' ends with a dot or a space, which Windows "
                                               "drops from file names",
                                               segment));
        }
        if (isDeviceName(segment)) {
            return pathError(text, std::format("'{}' is a device name on Windows", segment));
        }
        segments.push_back(segment);
    }
    if (segments.empty()) {
        return pathError(text, "names no file");
    }

    std::string normalized;
    for (const std::string_view segment : segments) {
        if (!normalized.empty()) {
            normalized += '/';
        }
        normalized += segment;
    }
    return AssetPath{std::move(normalized)};
}

std::string_view AssetPath::fileName() const noexcept
{
    const std::string_view text = text_;
    const std::size_t slash = text.rfind('/');
    return slash == std::string_view::npos ? text : text.substr(slash + 1);
}

std::string_view AssetPath::extension() const noexcept
{
    const std::string_view name = fileName();
    const std::size_t dot = name.rfind('.');
    if (dot == std::string_view::npos || dot == 0) {
        return {};
    }
    return name.substr(dot);
}

std::string foldCase(std::string_view text)
{
    std::string result(text);
    for (char& c : result) {
        c = lowerAscii(c);
    }
    return result;
}

std::string AssetPath::folded() const
{
    return foldCase(text_);
}

std::filesystem::path AssetPath::resolve(const std::filesystem::path& projectRoot) const
{
    // Printable ASCII means the same in every code page, so the narrow
    // constructor is exact on Windows too.
    return projectRoot / std::filesystem::path(text_);
}

} // namespace engine::assets
