#pragma once

#include "engine/core/Result.hpp"

#include <filesystem>
#include <string>

namespace engine::core {

// Reads a whole file as bytes (no newline translation). The error names the file.
[[nodiscard]] Result<std::string> readTextFile(const std::filesystem::path& path);

// UTF-8 text of a path for messages. path::string() can throw on Windows when
// the path contains characters outside the active code page.
[[nodiscard]] std::string toUtf8(const std::filesystem::path& path);

} // namespace engine::core
