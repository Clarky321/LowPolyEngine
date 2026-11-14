#pragma once

// Persistent 128-bit identifiers (ADR 0020): the identity of entities and
// scenes (and, from M4.3, assets) in documents. They are random, never
// derived from memory addresses, GPU handles, ECS handles, world seeds or
// std::hash, and are stored in files in their canonical text form.

#include "engine/core/Result.hpp"

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace engine::core {

// 128 bits, compared and ordered as one big-endian number. All zero is the
// null ID, never a valid identity.
struct Uuid {
    std::uint64_t high = 0;
    std::uint64_t low = 0;

    [[nodiscard]] constexpr bool isNull() const noexcept { return high == 0 && low == 0; }
    friend constexpr auto operator<=>(const Uuid&, const Uuid&) = default;
};

// Length of the canonical text form xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx.
inline constexpr std::size_t kUuidTextLength = 36;

// The canonical text form in lower case.
[[nodiscard]] std::string toString(const Uuid& id);

// Reads the canonical text form: exactly 36 characters, hyphens at positions
// 8, 13, 18 and 23, hex digits (either case) elsewhere. Any version and
// variant are accepted, and the null ID too. Errors start with `kind` and
// quote at most 40 characters of the text, e.g.
// "entity ID 'abc': expected 36 characters ...".
[[nodiscard]] Result<Uuid> parseUuid(std::string_view text, std::string_view kind = "ID");

// Makes random IDs in the RFC 9562 version 4 layout (122 random bits; never
// null). A value type owned by its user: no global or shared state.
// xoshiro256** (Blackman and Vigna) with a 256-bit state, two outputs per ID.
class UuidGenerator {
public:
    // A reproducible sequence: the state is expanded from the seed with
    // SplitMix64, the same on every platform (tests).
    explicit UuidGenerator(std::uint64_t seed) noexcept;

    // A state from std::random_device; IDs need no reproducibility. Falls
    // back to the clocks if the device is not available.
    [[nodiscard]] static UuidGenerator fromEntropy() noexcept;

    [[nodiscard]] Uuid next() noexcept;

private:
    explicit UuidGenerator(const std::array<std::uint64_t, 4>& state) noexcept;
    [[nodiscard]] std::uint64_t nextWord() noexcept;

    std::array<std::uint64_t, 4> state_{};
};

// A Uuid for one kind of object. Different kinds are different types with no
// conversion between them, so an entity ID cannot be passed as a scene ID.
// Tag provides `static constexpr std::string_view kName`, e.g. "entity ID",
// which starts the messages of parse().
template <typename Tag>
class TypedUuid {
public:
    // The null ID.
    constexpr TypedUuid() noexcept = default;
    constexpr explicit TypedUuid(const Uuid& raw) noexcept : value_(raw) {}

    [[nodiscard]] constexpr const Uuid& value() const noexcept { return value_; }
    [[nodiscard]] constexpr bool isNull() const noexcept { return value_.isNull(); }
    [[nodiscard]] std::string toString() const { return core::toString(value_); }

    // Reads the canonical text form (parseUuid) and rejects the null ID.
    [[nodiscard]] static Result<TypedUuid> parse(std::string_view text)
    {
        auto parsed = parseUuid(text, Tag::kName);
        if (!parsed) {
            return parsed.error();
        }
        if (parsed.value().isNull()) {
            return Error{std::string(Tag::kName) + " '" + std::string(text) +
                         "': the null ID is not an identity"};
        }
        return TypedUuid{parsed.value()};
    }

    friend constexpr auto operator<=>(const TypedUuid&, const TypedUuid&) = default;

private:
    Uuid value_{};
};

} // namespace engine::core
