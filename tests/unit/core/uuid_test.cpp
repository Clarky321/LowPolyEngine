#include "engine/core/Utf8.hpp"
#include "engine/core/Uuid.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>

using engine::core::parseUuid;
using engine::core::TypedUuid;
using engine::core::Uuid;
using engine::core::UuidGenerator;

namespace {

struct WidgetTag {
    static constexpr std::string_view kName = "widget ID";
};
struct GadgetTag {
    static constexpr std::string_view kName = "gadget ID";
};
using WidgetId = TypedUuid<WidgetTag>;
using GadgetId = TypedUuid<GadgetTag>;

struct Golden {
    std::uint64_t high;
    std::uint64_t low;
    std::string_view text;
};

// From an independent Python implementation of xoshiro256** seeded through
// SplitMix64 with the RFC 9562 version 4 bits (ADR 0020).
constexpr Golden kSeed0[] = {
    {0x99EC5F36CB7542B4ULL, 0xBF6E1F784956452AULL, "99ec5f36-cb75-42b4-bf6e-1f784956452a"},
    {0x1A5F849D493346E0ULL, 0xAAA594F1262D2D2CULL, "1a5f849d-4933-46e0-aaa5-94f1262d2d2c"},
    {0xBBA5AD4A1F844E59ULL, 0xBFEF8375D9EBCACAULL, "bba5ad4a-1f84-4e59-bfef-8375d9ebcaca"},
};
constexpr Golden kSeed1[] = {
    {0xB3F2AF6D0FC740C5ULL, 0x853B559647364CEAULL, "b3f2af6d-0fc7-40c5-853b-559647364cea"},
    {0x92F89756082A4514ULL, 0xA42E1C7BC266A3A7ULL, "92f89756-082a-4514-a42e-1c7bc266a3a7"},
    {0xB27A48E29A234673ULL, 0xA4C123126FFDA722ULL, "b27a48e2-9a23-4673-a4c1-23126ffda722"},
};
constexpr Golden kSeedDeadBeef[] = {
    {0xC5555444A74D4E83ULL, 0xA5C30D37B4B16E38ULL, "c5555444-a74d-4e83-a5c3-0d37b4b16e38"},
    {0x54F773200A4E4A23ULL, 0x829AED75FB958AF7ULL, "54f77320-0a4e-4a23-829a-ed75fb958af7"},
    {0xFB0E1DD69C254B2EULL, 0x9D6D02EC58814A27ULL, "fb0e1dd6-9c25-4b2e-9d6d-02ec58814a27"},
};

template <std::size_t N>
void checkGolden(std::uint64_t seed, const Golden (&expected)[N])
{
    UuidGenerator generator(seed);
    for (const Golden& golden : expected) {
        const Uuid id = generator.next();
        CHECK(id.high == golden.high);
        CHECK(id.low == golden.low);
        CHECK(engine::core::toString(id) == golden.text);
    }
}

bool hasVersion4Layout(const Uuid& id)
{
    return (id.high & 0xF000ULL) == 0x4000ULL && (id.low >> 62U) == 2U;
}

} // namespace

TEST_CASE("The seeded generator matches the reference implementation", "[core][uuid]")
{
    checkGolden(0, kSeed0);
    checkGolden(1, kSeed1);
    checkGolden(0xDEADBEEFULL, kSeedDeadBeef);
}

TEST_CASE("Generated IDs have the version 4 layout and are never null", "[core][uuid]")
{
    UuidGenerator generator(42);
    for (int i = 0; i < 10000; ++i) {
        const Uuid id = generator.next();
        REQUIRE_FALSE(id.isNull());
        REQUIRE(hasVersion4Layout(id));
    }
}

TEST_CASE("Generated IDs are unique", "[core][uuid]")
{
    UuidGenerator generator(7);
    std::set<Uuid> seen;
    constexpr int kCount = 200000;
    for (int i = 0; i < kCount; ++i) {
        seen.insert(generator.next());
    }
    CHECK(seen.size() == static_cast<std::size_t>(kCount));

    // The first IDs of many seeds differ too.
    std::set<Uuid> first;
    for (std::uint64_t seed = 0; seed < 1000; ++seed) {
        first.insert(UuidGenerator(seed).next());
    }
    CHECK(first.size() == 1000);
}

TEST_CASE("A generator is a value: a copy repeats the sequence", "[core][uuid]")
{
    UuidGenerator original(99);
    (void)original.next();
    UuidGenerator copy = original;
    for (int i = 0; i < 10; ++i) {
        CHECK(original.next() == copy.next());
    }
}

TEST_CASE("Generators from entropy give different, well-formed IDs", "[core][uuid]")
{
    UuidGenerator a = UuidGenerator::fromEntropy();
    UuidGenerator b = UuidGenerator::fromEntropy();
    const Uuid first = a.next();
    const Uuid second = b.next();
    CHECK(first != second);
    CHECK(hasVersion4Layout(first));
    CHECK(hasVersion4Layout(second));
}

TEST_CASE("The text form round-trips", "[core][uuid]")
{
    for (const Golden& golden : kSeed1) {
        const auto parsed = parseUuid(golden.text);
        REQUIRE(parsed);
        CHECK(parsed.value() == Uuid{golden.high, golden.low});
    }
    const auto upper = parseUuid("B3F2AF6D-0FC7-40C5-853B-559647364CEA");
    REQUIRE(upper);
    CHECK(upper.value() == Uuid{kSeed1[0].high, kSeed1[0].low});
    CHECK(engine::core::toString(upper.value()) == kSeed1[0].text);

    const auto null = parseUuid("00000000-0000-0000-0000-000000000000");
    REQUIRE(null);
    CHECK(null.value().isNull());
    CHECK(engine::core::toString(Uuid{}) == "00000000-0000-0000-0000-000000000000");

    const Uuid all{~0ULL, ~0ULL};
    CHECK(engine::core::toString(all) == "ffffffff-ffff-ffff-ffff-ffffffffffff");
    const auto parsedAll = parseUuid("ffffffff-ffff-ffff-ffff-ffffffffffff");
    REQUIRE(parsedAll);
    CHECK(parsedAll.value() == all);

    // Any version and variant can be read (IDs made by other tools).
    const auto version1 = parseUuid("6ba7b810-9dad-11d1-80b4-00c04fd430c8");
    REQUIRE(version1);
    CHECK(engine::core::toString(version1.value()) == "6ba7b810-9dad-11d1-80b4-00c04fd430c8");
}

TEST_CASE("IDs compare as big-endian numbers", "[core][uuid]")
{
    CHECK(Uuid{0, 5} < Uuid{1, 0});
    CHECK(Uuid{1, 0} < Uuid{1, 1});
    CHECK(Uuid{2, 3} == Uuid{2, 3});
    CHECK(Uuid{2, 3} != Uuid{3, 2});
    CHECK(Uuid{0, ~0ULL} < Uuid{1, 0});
}

TEST_CASE("Malformed text is rejected with the position and the kind", "[core][uuid]")
{
    const auto message = [](std::string_view text, std::string_view kind = "ID") {
        const auto parsed = parseUuid(text, kind);
        REQUIRE_FALSE(parsed);
        return parsed.error().message;
    };
    CHECK(message("") == "ID '': expected 36 characters in the form "
                         "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx, found 0");
    CHECK(message("b3f2af6d-0fc7-40c5-853b-559647364ce", "entity ID") ==
          "entity ID 'b3f2af6d-0fc7-40c5-853b-559647364ce': expected 36 characters in the form "
          "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx, found 35");
    CHECK(message("b3f2af6d-0fc7-40c5-853b-559647364ceaa") ==
          "ID 'b3f2af6d-0fc7-40c5-853b-559647364ceaa': expected 36 characters in the form "
          "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx, found 37");
    CHECK(message("b3f2af6dx0fc7-40c5-853b-559647364cea") ==
          "ID 'b3f2af6dx0fc7-40c5-853b-559647364cea': expected '-' at position 8, found 'x'");
    CHECK(message("b3f2af6d-0fc7-40c5-853b0559647364cea") ==
          "ID 'b3f2af6d-0fc7-40c5-853b0559647364cea': expected '-' at position 23, found '0'");
    CHECK(message("b3g2af6d-0fc7-40c5-853b-559647364cea") ==
          "ID 'b3g2af6d-0fc7-40c5-853b-559647364cea': 'g' at position 2 is not a hex digit");
    CHECK(message("b3f2af6d-0fc7-40c5-853b-55964 364cea") ==
          "ID 'b3f2af6d-0fc7-40c5-853b-55964 364cea': ' ' at position 29 is not a hex digit");
    const std::string withControl =
        std::string("b3f2af6d-0fc7-40c5-853b-5596473") + '\x01' + "4cea";
    CHECK(message(withControl).ends_with("byte 0x01 at position 31 is not a hex digit"));
    CHECK(message("{b3f2af6d-0fc7-40c5-853b-559647364c}") ==
          "ID '{b3f2af6d-0fc7-40c5-853b-559647364c}': '{' at position 0 is not a hex digit");
}

TEST_CASE("Long text is quoted shortly and stays valid UTF-8", "[core][uuid]")
{
    const std::string longText(1000, 'a');
    const auto parsed = parseUuid(longText);
    REQUIRE_FALSE(parsed);
    CHECK(parsed.error().message == "ID '" + std::string(40, 'a') +
                                        "...': expected 36 characters in the form "
                                        "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx, found 1000");

    // 20 euro signs (3 bytes each): byte 40 is inside the 14th, so the quote
    // stops after 13 of them.
    std::string euros;
    for (int i = 0; i < 20; ++i) {
        euros += "\xE2\x82\xAC";
    }
    const auto failed = parseUuid(euros);
    REQUIRE_FALSE(failed);
    std::string thirteen;
    for (int i = 0; i < 13; ++i) {
        thirteen += "\xE2\x82\xAC";
    }
    CHECK(failed.error().message.starts_with("ID '" + thirteen + "...': "));
    CHECK_FALSE(engine::core::findInvalidUtf8(failed.error().message).has_value());
}

TEST_CASE("Typed IDs keep their kind and reject the null ID", "[core][uuid]")
{
    static_assert(!std::is_convertible_v<WidgetId, GadgetId>);
    static_assert(!std::is_constructible_v<GadgetId, WidgetId>);
    static_assert(!std::is_convertible_v<Uuid, WidgetId>);
    static_assert(!std::is_constructible_v<WidgetId, std::uint64_t>);
    static_assert(!std::is_constructible_v<WidgetId, const void*>);
    static_assert(std::is_trivially_copyable_v<WidgetId>);

    const WidgetId none;
    CHECK(none.isNull());

    const auto parsed = WidgetId::parse(kSeed0[0].text);
    REQUIRE(parsed);
    CHECK(parsed.value().value() == Uuid{kSeed0[0].high, kSeed0[0].low});
    CHECK(parsed.value().toString() == kSeed0[0].text);
    CHECK(parsed.value() == WidgetId{Uuid{kSeed0[0].high, kSeed0[0].low}});

    const auto null = WidgetId::parse("00000000-0000-0000-0000-000000000000");
    REQUIRE_FALSE(null);
    CHECK(null.error().message ==
          "widget ID '00000000-0000-0000-0000-000000000000': the null ID is not an identity");

    const auto wrong = GadgetId::parse("nope");
    REQUIRE_FALSE(wrong);
    CHECK(wrong.error().message.starts_with("gadget ID 'nope': expected 36 characters"));
}
