#pragma once

// UTF-8 checks for names and other text from documents (RFC 3629: no overlong
// forms, no surrogates, nothing above U+10FFFF).

#include <cstddef>
#include <optional>
#include <string_view>

namespace engine::core {

// Decodes the code point that starts at byte `offset` and moves `offset` past
// it. Returns nothing, and leaves `offset` unchanged, if the bytes there are
// not a valid UTF-8 sequence or `offset` is at the end.
[[nodiscard]] std::optional<char32_t> decodeUtf8At(std::string_view text,
                                                   std::size_t& offset) noexcept;

// Byte offset of the first invalid UTF-8 sequence, or nothing if the whole
// text is valid.
[[nodiscard]] std::optional<std::size_t> findInvalidUtf8(std::string_view text) noexcept;

} // namespace engine::core
