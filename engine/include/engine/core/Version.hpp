#pragma once

#include <cstdint>
#include <string>

namespace engine::core {

struct Version {
    std::uint32_t major = 0;
    std::uint32_t minor = 0;
    std::uint32_t patch = 0;

    friend constexpr bool operator==(const Version&, const Version&) = default;
};

// Version of the engine library, taken from the CMake project() declaration.
[[nodiscard]] Version engineVersion() noexcept;

// Formats a version as "major.minor.patch".
[[nodiscard]] std::string toString(const Version& version);

} // namespace engine::core
