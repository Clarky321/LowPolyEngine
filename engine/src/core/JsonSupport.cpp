#include "JsonSupport.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <system_error>

namespace engine::core::json {

namespace {

// Finds where malformed JSON goes wrong, without exceptions: a parser that
// accepts every value and records the first syntax error.
class SyntaxErrorFinder final : public nlohmann::json_sax<Json> {
public:
    bool null() override { return true; }
    bool boolean(bool) override { return true; }
    bool number_integer(number_integer_t) override { return true; }
    bool number_unsigned(number_unsigned_t) override { return true; }
    bool number_float(number_float_t, const string_t&) override { return true; }
    bool string(string_t&) override { return true; }
    bool binary(binary_t&) override { return true; }
    bool start_object(std::size_t) override { return true; }
    bool key(string_t&) override { return true; }
    bool end_object() override { return true; }
    bool start_array(std::size_t) override { return true; }
    bool end_array() override { return true; }
    bool parse_error(std::size_t, const std::string&,
                     const nlohmann::detail::exception& error) override
    {
        // "[json.exception.parse_error.101] parse error at line 2, column 5: ..."
        message = error.what();
        if (const auto bracket = message.find("] "); bracket != std::string::npos) {
            message.erase(0, bracket + 2U);
        }
        return false;
    }

    std::string message;
};

} // namespace

double floatNumber(float value)
{
    std::array<char, 64> buffer{};
    const auto written = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    if (written.ec == std::errc{}) {
        double shortest = 0.0;
        const auto read = std::from_chars(buffer.data(), written.ptr, shortest);
        if (read.ec == std::errc{} && read.ptr == written.ptr &&
            static_cast<float>(shortest) == value) {
            return shortest;
        }
    }
    return static_cast<double>(value);
}

std::string shortened(std::string_view text)
{
    constexpr std::size_t kMaxBytes = 40;
    if (text.size() <= kMaxBytes) {
        return std::string(text);
    }
    std::size_t cut = kMaxBytes - 3;
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0U) == 0x80U) {
        --cut;
    }
    return std::string(text.substr(0, cut)) + "...";
}

std::string describe(const Json& value)
{
    if (value.is_object() || value.is_array() || value.is_null()) {
        return value.type_name();
    }
    return std::format("{} {}", value.type_name(),
                       shortened(value.dump(-1, ' ', false, Json::error_handler_t::replace)));
}

std::optional<std::string> syntaxError(std::string_view text)
{
    SyntaxErrorFinder finder;
    if (Json::sax_parse(text, &finder)) {
        return std::nullopt;
    }
    return finder.message;
}

Result<int> readEnvelope(const Json& root, std::string_view formatId, int currentVersion)
{
    const auto formatField = root.find("format");
    if (formatField == root.end()) {
        return Error{std::format("format: missing, expected \"{}\"", formatId)};
    }
    if (!formatField->is_string() || formatField->get<std::string>() != formatId) {
        return Error{
            std::format("format: expected \"{}\", found {}", formatId, describe(*formatField))};
    }
    const auto versionField = root.find("formatVersion");
    if (versionField == root.end()) {
        return Error{"formatVersion: missing"};
    }
    if (!versionField->is_number_integer()) {
        return Error{
            std::format("formatVersion: expected an integer, found {}", describe(*versionField))};
    }
    const auto version = versionField->get<std::int64_t>();
    if (version < 1) {
        return Error{std::format("formatVersion: {} is not a version (the first is 1)", version)};
    }
    if (version > currentVersion) {
        return Error{std::format("formatVersion: {} is newer than this program reads ({}); a "
                                 "newer version of the engine wrote the file",
                                 version, currentVersion)};
    }
    return static_cast<int>(version);
}

std::optional<Error> readFloat(const Json& value, std::string_view path, float& out)
{
    if (!value.is_number()) {
        return Error{std::format("{}: expected a number, found {}", path, describe(value))};
    }
    const auto number = value.get<double>();
    if (!(std::abs(number) <= static_cast<double>(std::numeric_limits<float>::max()))) {
        return Error{std::format("{}: {} is outside the range of a float", path, value.dump())};
    }
    out = static_cast<float>(number);
    return std::nullopt;
}

std::optional<Error> readBool(const Json& value, std::string_view path, bool& out)
{
    if (!value.is_boolean()) {
        return Error{std::format("{}: expected true or false, found {}", path, describe(value))};
    }
    out = value.get<bool>();
    return std::nullopt;
}

std::optional<Error> readString(const Json& value, std::string_view path, std::string& out)
{
    if (!value.is_string()) {
        return Error{std::format("{}: expected a string, found {}", path, describe(value))};
    }
    out = value.get<std::string>();
    return std::nullopt;
}

} // namespace engine::core::json
