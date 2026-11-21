#pragma once

// Internal to the engine (not a public header): the save journal of a
// project (M5.4, ADR 0028, point 5). A save writes every part to a temporary
// file next to it, then this journal; its appearance is the save's commit.
// Then the temporary files are renamed over the parts and the journal is
// removed. While it is there, a part whose temporary file still exists is
// read from that file.
//
//   {
//     "format": "lpe.project.save",
//     "formatVersion": 1,
//     "files": [
//       {"target": "assets.json", "temporary": "assets.json.saving-1a2b3c4d"}
//     ]
//   }

#include "engine/assets/AssetPath.hpp"
#include "engine/core/Result.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace engine::project::detail {

inline constexpr std::string_view kSaveJournalFormat = "lpe.project.save";
inline constexpr int kSaveJournalFormatVersion = 1;
// A temporary file is named "<file>.saving-<id>", the id being kSaveIdDigits
// lower-case hex digits, the same for every file of one save.
inline constexpr std::string_view kTemporaryMarker = ".saving-";
inline constexpr std::size_t kSaveIdDigits = 8;
inline constexpr std::size_t kMaxJournalEntries = 16;
inline constexpr std::uintmax_t kMaxJournalBytes = 64U * 1024U;

struct JournalEntry {
    assets::AssetPath target;
    assets::AssetPath temporary;
};

// Whether `saveId` has the form of a save's id.
[[nodiscard]] bool isSaveId(std::string_view saveId) noexcept;

// "<target>.saving-<saveId>". Fails for an invalid id or a name too long for
// an asset path.
[[nodiscard]] core::Result<assets::AssetPath> temporaryPath(const assets::AssetPath& target,
                                                            std::string_view saveId);

// Whether `fileName` names a temporary file of the file `targetFileName` (both
// without folders): "<targetFileName>.saving-" and a save's id.
[[nodiscard]] bool isTemporaryOf(std::string_view fileName,
                                 std::string_view targetFileName) noexcept;

// The journal's text, indented by two spaces, ending with a newline.
[[nodiscard]] std::string journalToJson(std::span<const JournalEntry> entries);

// Reads a journal. Fails for text that is not JSON or not an object, another
// format, a missing or newer version, a missing, empty, too long or wrongly
// typed "files", an entry without a valid "target" or "temporary", a
// temporary path that is not temporaryPath(target, <a save's id>), and a
// target named twice.
[[nodiscard]] core::Result<std::vector<JournalEntry>> journalFromJson(std::string_view text);

} // namespace engine::project::detail
