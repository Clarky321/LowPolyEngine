#pragma once

#include "engine/core/Result.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>

namespace engine::core {

// Reads a whole file as bytes (no newline translation). The error names the file.
[[nodiscard]] Result<std::string> readTextFile(const std::filesystem::path& path);

// Writes bytes to a file, replacing it, and creates missing parent folders.
// The error names the file.
[[nodiscard]] std::optional<Error> writeBinaryFile(const std::filesystem::path& path,
                                                   std::span<const std::uint8_t> bytes);

// Replaces a file in one step, as far as the file system allows (M4.4): the
// bytes go to a temporary file in the target's folder, which is then renamed
// over the target (an atomic replace on Linux; MoveFileEx with
// MOVEFILE_REPLACE_EXISTING on Windows). On failure the target is left as it
// was and the temporary file is removed. The folder must exist; it is not
// created. The data is not forced to the disk, so a power loss right after
// may still lose it. The error names the file.
[[nodiscard]] std::optional<Error> writeFileAtomically(const std::filesystem::path& path,
                                                       std::span<const std::uint8_t> bytes);

// UTF-8 text of a path for messages. path::string() can throw on Windows when
// the path contains characters outside the active code page.
[[nodiscard]] std::string toUtf8(const std::filesystem::path& path);

} // namespace engine::core
