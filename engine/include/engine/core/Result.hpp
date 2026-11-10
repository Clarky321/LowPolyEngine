#pragma once

#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace engine::core {

// Error returned across subsystem boundaries instead of an exception (AGENTS.md §4).
struct Error {
    std::string message;
};

// Holds either a value or an Error. A minimal stand-in for C++23 std::expected,
// which is not available in the project's C++20 baseline (ADR 0004).
//
// Accessing value() on an error (or error() on a value) is a programming error;
// it throws std::bad_variant_access rather than invoking undefined behavior.
template <typename T>
class [[nodiscard]] Result {
    static_assert(!std::is_same_v<T, Error>, "Result<Error> would be ambiguous");

public:
    // Implicit on purpose so functions can `return value;` or `return Error{...};`.
    Result(T value) : storage_(std::in_place_index<0>, std::move(value)) {}
    Result(Error error) : storage_(std::in_place_index<1>, std::move(error)) {}

    [[nodiscard]] bool hasValue() const noexcept { return storage_.index() == 0; }
    explicit operator bool() const noexcept { return hasValue(); }

    [[nodiscard]] T& value() & { return std::get<0>(storage_); }
    [[nodiscard]] const T& value() const& { return std::get<0>(storage_); }
    [[nodiscard]] T&& value() && { return std::get<0>(std::move(storage_)); }

    [[nodiscard]] const Error& error() const& { return std::get<1>(storage_); }

private:
    std::variant<T, Error> storage_;
};

} // namespace engine::core
