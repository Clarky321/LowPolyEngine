#include "engine/core/Utf8.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

using engine::core::decodeUtf8At;
using engine::core::findInvalidUtf8;

TEST_CASE("Valid UTF-8 is accepted", "[core][utf8]")
{
    CHECK_FALSE(findInvalidUtf8("").has_value());
    CHECK_FALSE(findInvalidUtf8("Island 7").has_value());
    CHECK_FALSE(findInvalidUtf8("Остров α").has_value());
    CHECK_FALSE(findInvalidUtf8("日本").has_value());
    CHECK_FALSE(findInvalidUtf8("\xF0\x9F\x98\x80").has_value()); // U+1F600
    CHECK_FALSE(findInvalidUtf8("\xF4\x8F\xBF\xBF").has_value()); // U+10FFFF
    CHECK_FALSE(findInvalidUtf8("\xEF\xBF\xBD").has_value());     // U+FFFD
}

TEST_CASE("Invalid UTF-8 is found at its byte offset", "[core][utf8]")
{
    struct Case {
        std::string_view text;
        std::size_t offset;
    };
    const Case cases[] = {
        {"\x80", 0},                 // a continuation byte without a lead
        {"ab\xBF", 2},               // the same later in the text
        {"a\xC3", 1},                // truncated two-byte sequence
        {"\xE2\x82", 0},             // truncated three-byte sequence
        {"\xE2\x82(", 0},            // a non-continuation byte inside a sequence
        {"\xC0\xAF", 0},             // overlong '/'
        {"\xC1\xBF", 0},             // overlong
        {"\xE0\x80\xAF", 0},         // overlong three-byte form
        {"\xF0\x80\x80\xAF", 0},     // overlong four-byte form
        {"\xED\xA0\x80", 0},         // UTF-16 surrogate U+D800
        {"\xED\xBF\xBF", 0},         // surrogate U+DFFF
        {"\xF4\x90\x80\x80", 0},     // U+110000, beyond Unicode
        {"\xF8\x88\x80\x80\x80", 0}, // five-byte lead
        {"ok\xFF", 2},               // never valid
        {"\xC3\xA9\xC3", 2},         // valid 'é', then a truncated sequence
    };
    for (const Case& c : cases) {
        INFO("offset " << c.offset);
        const auto found = findInvalidUtf8(c.text);
        REQUIRE(found.has_value());
        CHECK(*found == c.offset);
    }
}

TEST_CASE("decodeUtf8At reads one code point and moves past it", "[core][utf8]")
{
    const std::string_view text = "a\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80";
    std::size_t offset = 0;
    CHECK(decodeUtf8At(text, offset) == std::optional<char32_t>(U'a'));
    CHECK(offset == 1);
    CHECK(decodeUtf8At(text, offset) == std::optional<char32_t>(U'é'));
    CHECK(offset == 3);
    CHECK(decodeUtf8At(text, offset) == std::optional<char32_t>(U'€'));
    CHECK(offset == 6);
    CHECK(decodeUtf8At(text, offset) == std::optional<char32_t>(U'\U0001F600'));
    CHECK(offset == 10);
    CHECK_FALSE(decodeUtf8At(text, offset).has_value());
    CHECK(offset == 10);

    std::size_t bad = 1;
    CHECK_FALSE(decodeUtf8At("a\x80", bad).has_value());
    CHECK(bad == 1);
}
