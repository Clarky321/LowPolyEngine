#pragma once

// Internal to the engine (not a public header): what the project's JSON
// documents share (ADR 0004 and its Amendment 1) — the world settings file,
// the asset manifest and scene documents. nlohmann_json is included here and
// only by the sources that write or read documents.

#include "engine/core/Result.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <format>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

namespace engine::core::json {

using Json = nlohmann::json;
using OrderedJson = nlohmann::ordered_json;

// The JSON number for a float: the double nearest to the float's shortest
// decimal form, which the library prints with those few digits (0.045, not
// 0.04500000178813934), if it reads back to the same float; the float's exact
// value otherwise. Reading the number back as a float gives every bit again.
[[nodiscard]] double floatNumber(float value);

// At most 40 bytes of text from a document, cut at a character boundary and
// ending in "..." when shortened.
[[nodiscard]] std::string shortened(std::string_view text);

// A JSON value in a message: its type, and the value itself if it is short,
// e.g. `string "many"` or `array`.
[[nodiscard]] std::string describe(const Json& value);

// Nothing if the text is well-formed JSON, otherwise the library's message
// with line and column, e.g. "parse error at line 2, column 5: ...". Found
// without exceptions.
[[nodiscard]] std::optional<std::string> syntaxError(std::string_view text);

// The envelope of a document (ADR 0004 rule 1): "format" equal to `formatId` and
// an integer "formatVersion" from 1 to currentVersion. Returns the version; a
// newer one is an error that says a newer engine wrote the file.
[[nodiscard]] Result<int> readEnvelope(const Json& root, std::string_view formatId,
                                       int currentVersion);

// Field readers: the error starts with the field path, e.g.
// "islands.count: expected an integer, found string \"many\"".
[[nodiscard]] std::optional<Error> readFloat(const Json& value, std::string_view path, float& out);
[[nodiscard]] std::optional<Error> readBool(const Json& value, std::string_view path, bool& out);
[[nodiscard]] std::optional<Error> readString(const Json& value, std::string_view path,
                                              std::string& out);

// An integer field of type T; a JSON float such as 12.5, or a value outside
// T, is an error.
template <typename T>
[[nodiscard]] std::optional<Error> readInteger(const Json& value, std::string_view path, T& out)
{
    if (!value.is_number_integer()) {
        return Error{std::format("{}: expected an integer, found {}", path, describe(value))};
    }
    const auto outside = [&] {
        return Error{std::format("{}: {} is outside the range of the field's type ({} to {})", path,
                                 value.dump(), std::numeric_limits<T>::min(),
                                 std::numeric_limits<T>::max())};
    };
    if (value.is_number_unsigned()) {
        const auto number = value.get<std::uint64_t>();
        if (number > static_cast<std::uint64_t>(std::numeric_limits<T>::max())) {
            return outside();
        }
        out = static_cast<T>(number);
    } else {
        const auto number = value.get<std::int64_t>();
        if (number < static_cast<std::int64_t>(std::numeric_limits<T>::min()) ||
            number > static_cast<std::int64_t>(std::numeric_limits<T>::max())) {
            return outside();
        }
        out = static_cast<T>(number);
    }
    return std::nullopt;
}

} // namespace engine::core::json
