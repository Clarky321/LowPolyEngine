#pragma once

// World generation settings as a versioned JSON file (ADR 0004 and its
// Amendment 1): an envelope with the format id and version, the generator
// version and the seed as a hex string, then every field of WorldGenSettings
// in nested objects along its path ("islands.altitude.min" is
// "islands": {"altitude": {"min": ...}}). The JSON library stays inside the
// implementation; nothing here throws.

#include "engine/core/Result.hpp"
#include "engine/worldgen/WorldGenSettings.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace engine::worldgen {

// The "format" of a settings file.
inline constexpr std::string_view kSettingsFormat = "lpe.worldgen.settings";

// The "formatVersion" this build writes and the newest it reads.
inline constexpr int kSettingsFormatVersion = 1;

struct LoadedSettings {
    // Valid settings (validate() passed); fields the file leaves out keep
    // their defaults.
    WorldGenSettings settings;
    // One per field of the file the reader does not know, which is ignored,
    // e.g. "unknown field 'islands.colour' ignored".
    std::vector<std::string> warnings;
};

// The file's text: every field, in the order of declaration, indented by two
// spaces, ending with a newline. Floats are written with the fewest digits
// that read back to the same float; the seed as "0x" and 16 hex digits.
[[nodiscard]] std::string settingsToJson(const WorldGenSettings& settings);

// Reads a file's text. Fails with a message that names the field path (or the
// line and column of malformed JSON) if the text is not JSON, the format is
// another one, the format version is newer than kSettingsFormatVersion, a
// field has the wrong type, or the settings do not pass validate().
[[nodiscard]] core::Result<LoadedSettings> settingsFromJson(std::string_view text);

// Writes settingsToJson() to a file, creating missing folders. Settings that
// do not pass validate() are not written; the error says why.
[[nodiscard]] std::optional<core::Error> saveSettings(const std::filesystem::path& path,
                                                      const WorldGenSettings& settings);

// Reads a settings file; errors start with the file's name.
[[nodiscard]] core::Result<LoadedSettings> loadSettings(const std::filesystem::path& path);

} // namespace engine::worldgen
