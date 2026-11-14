#include "engine/core/Utf8.hpp"

#include <cstdint>

namespace engine::core {

std::optional<char32_t> decodeUtf8At(std::string_view text, std::size_t& offset) noexcept
{
    if (offset >= text.size()) {
        return std::nullopt;
    }
    const auto lead = static_cast<std::uint32_t>(static_cast<unsigned char>(text[offset]));
    std::size_t length = 0;
    std::uint32_t value = 0;
    std::uint32_t minimum = 0;
    if (lead < 0x80U) {
        ++offset;
        return static_cast<char32_t>(lead);
    }
    if ((lead & 0xE0U) == 0xC0U) {
        length = 2;
        value = lead & 0x1FU;
        minimum = 0x80U;
    } else if ((lead & 0xF0U) == 0xE0U) {
        length = 3;
        value = lead & 0x0FU;
        minimum = 0x800U;
    } else if ((lead & 0xF8U) == 0xF0U) {
        length = 4;
        value = lead & 0x07U;
        minimum = 0x10000U;
    } else {
        return std::nullopt; // a continuation byte or 0xF8..0xFF
    }
    if (length > text.size() - offset) {
        return std::nullopt;
    }
    for (std::size_t k = 1; k < length; ++k) {
        const auto next = static_cast<std::uint32_t>(static_cast<unsigned char>(text[offset + k]));
        if ((next & 0xC0U) != 0x80U) {
            return std::nullopt;
        }
        value = (value << 6U) | (next & 0x3FU);
    }
    // Overlong forms, surrogates and values beyond Unicode.
    if (value < minimum || (value >= 0xD800U && value <= 0xDFFFU) || value > 0x10FFFFU) {
        return std::nullopt;
    }
    offset += length;
    return static_cast<char32_t>(value);
}

std::optional<std::size_t> findInvalidUtf8(std::string_view text) noexcept
{
    std::size_t offset = 0;
    while (offset < text.size()) {
        const std::size_t start = offset;
        if (!decodeUtf8At(text, offset)) {
            return start;
        }
    }
    return std::nullopt;
}

} // namespace engine::core
