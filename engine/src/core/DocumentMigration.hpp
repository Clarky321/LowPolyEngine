#pragma once

// Internal to the engine: bringing a document of an older format version up
// to the current one before its fields are read (ADR 0004 rule 5). A
// migration turns version N into N + 1 in place; they run one after another,
// so each is written once and never needs to know about later versions.
// Generic over the document type, so it is tested without a JSON library.

#include "engine/core/Result.hpp"

#include <cstddef>
#include <format>
#include <optional>
#include <span>

namespace engine::core {

template <typename Document>
using DocumentMigration = std::optional<Error> (*)(Document& document);

// Applies migrations[version - 1], migrations[version], ... up to the
// current version: migrations[i] turns version i + 1 into i + 2. Fails for a
// version outside [1, currentVersion], a missing migration, or a migration's
// own error, which is prefixed with its versions.
template <typename Document>
[[nodiscard]] std::optional<Error>
migrateDocument(Document& document, int version, int currentVersion,
                std::span<const DocumentMigration<Document>> migrations)
{
    if (version < 1 || version > currentVersion) {
        return Error{std::format("format version {} cannot be brought to version {}", version,
                                 currentVersion)};
    }
    for (int from = version; from < currentVersion; ++from) {
        const auto index = static_cast<std::size_t>(from - 1);
        if (index >= migrations.size() || migrations[index] == nullptr) {
            return Error{std::format("no migration from format version {} to {}", from, from + 1)};
        }
        if (auto error = migrations[index](document)) {
            return Error{std::format("migration from format version {} to {}: {}", from, from + 1,
                                     error->message)};
        }
    }
    return std::nullopt;
}

} // namespace engine::core
