#include "engine/core/Uuid.hpp"

#include <chrono>
#include <exception>
#include <format>
#include <random>

namespace engine::core {

namespace {

constexpr std::uint64_t kSplitMix64Gamma = 0x9E3779B97F4A7C15ULL;
// RFC 9562: version 4 in bits 12-15 of the high word, variant 10 in the top
// two bits of the low word.
constexpr std::uint64_t kVersionMask = 0xF000ULL;
constexpr std::uint64_t kVersion4 = 0x4000ULL;
constexpr std::uint64_t kVariantMask = 0xC000000000000000ULL;
constexpr std::uint64_t kVariantRfc = 0x8000000000000000ULL;
// Messages quote at most this many bytes of a text that failed to parse.
constexpr std::size_t kMaxQuotedBytes = 40;

std::uint64_t splitMix64(std::uint64_t& state) noexcept
{
    state += kSplitMix64Gamma;
    std::uint64_t z = state;
    z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31U);
}

constexpr std::uint64_t rotateLeft(std::uint64_t value, unsigned bits) noexcept
{
    return (value << bits) | (value >> (64U - bits));
}

constexpr bool isHyphenPosition(std::size_t position) noexcept
{
    return position == 8 || position == 13 || position == 18 || position == 23;
}

int hexValue(char c) noexcept
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

// The text in quotes, cut at a character boundary after kMaxQuotedBytes.
std::string quoted(std::string_view text)
{
    if (text.size() <= kMaxQuotedBytes) {
        return "'" + std::string(text) + "'";
    }
    std::size_t cut = kMaxQuotedBytes;
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0U) == 0x80U) {
        --cut;
    }
    return "'" + std::string(text.substr(0, cut)) + "...'";
}

std::string describe(char c)
{
    const auto byte = static_cast<unsigned char>(c);
    if (byte < 0x20U || byte >= 0x7FU) {
        return std::format("byte 0x{:02X}", static_cast<unsigned>(byte));
    }
    return std::format("'{}'", c);
}

} // namespace

std::string toString(const Uuid& id)
{
    static constexpr std::string_view kDigits = "0123456789abcdef";
    std::string text(kUuidTextLength, '-');
    std::size_t nibble = 0;
    for (std::size_t position = 0; position < kUuidTextLength; ++position) {
        if (isHyphenPosition(position)) {
            continue;
        }
        const std::uint64_t word = nibble < 16 ? id.high : id.low;
        const auto shift = static_cast<unsigned>(60U - 4U * static_cast<unsigned>(nibble % 16));
        text[position] = kDigits[static_cast<std::size_t>((word >> shift) & 0xFU)];
        ++nibble;
    }
    return text;
}

Result<Uuid> parseUuid(std::string_view text, std::string_view kind)
{
    const std::string prefix = std::string(kind) + " " + quoted(text) + ": ";
    if (text.size() != kUuidTextLength) {
        return Error{prefix + std::format("expected {} characters in the form "
                                          "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx, found {}",
                                          kUuidTextLength, text.size())};
    }
    Uuid id;
    std::size_t nibble = 0;
    for (std::size_t position = 0; position < kUuidTextLength; ++position) {
        const char c = text[position];
        if (isHyphenPosition(position)) {
            if (c != '-') {
                return Error{prefix + std::format("expected '-' at position {}, found {}", position,
                                                  describe(c))};
            }
            continue;
        }
        const int value = hexValue(c);
        if (value < 0) {
            return Error{prefix + std::format("{} at position {} is not a hex digit", describe(c),
                                              position)};
        }
        std::uint64_t& word = nibble < 16 ? id.high : id.low;
        word = (word << 4U) | static_cast<std::uint64_t>(value);
        ++nibble;
    }
    return id;
}

UuidGenerator::UuidGenerator(std::uint64_t seed) noexcept
{
    std::uint64_t state = seed;
    for (std::uint64_t& word : state_) {
        word = splitMix64(state);
    }
}

UuidGenerator::UuidGenerator(const std::array<std::uint64_t, 4>& state) noexcept : state_(state) {}

UuidGenerator UuidGenerator::fromEntropy() noexcept
{
    std::array<std::uint64_t, 4> state{};
    bool filled = false;
    try {
        std::random_device device;
        for (std::uint64_t& word : state) {
            const auto upper = static_cast<std::uint64_t>(device());
            const auto lower = static_cast<std::uint64_t>(device());
            word = (upper << 32U) | lower;
        }
        filled = state[0] != 0 || state[1] != 0 || state[2] != 0 || state[3] != 0;
    } catch (const std::exception&) {
        filled = false;
    }
    if (!filled) {
        // No entropy device: both clocks, spread over the state.
        const auto steady =
            static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
        const auto system =
            static_cast<std::uint64_t>(std::chrono::system_clock::now().time_since_epoch().count());
        std::uint64_t seed = steady ^ rotateLeft(system, 32U);
        for (std::uint64_t& word : state) {
            word = splitMix64(seed);
        }
    }
    return UuidGenerator{state};
}

std::uint64_t UuidGenerator::nextWord() noexcept
{
    // xoshiro256** 1.0 (reference implementation by Blackman and Vigna).
    const std::uint64_t result = rotateLeft(state_[1] * 5U, 7U) * 9U;
    const std::uint64_t t = state_[1] << 17U;
    state_[2] ^= state_[0];
    state_[3] ^= state_[1];
    state_[1] ^= state_[2];
    state_[0] ^= state_[3];
    state_[2] ^= t;
    state_[3] = rotateLeft(state_[3], 45U);
    return result;
}

Uuid UuidGenerator::next() noexcept
{
    Uuid id;
    id.high = (nextWord() & ~kVersionMask) | kVersion4;
    id.low = (nextWord() & ~kVariantMask) | kVariantRfc;
    return id;
}

} // namespace engine::core
