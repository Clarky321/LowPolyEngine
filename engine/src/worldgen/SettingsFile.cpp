#include "engine/worldgen/SettingsFile.hpp"

#include "FloatingPointRules.hpp"
#include "SettingsFields.hpp"

#include "engine/core/FileSystem.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <limits>
#include <set>
#include <span>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>

namespace engine::worldgen {

namespace {

using Json = nlohmann::json;
using OrderedJson = nlohmann::ordered_json;

constexpr std::string_view kWaterEnabled = "water.enabled";

// Calls visit(path, value) for every field of the settings below the envelope
// in the order of declaration: the numeric fields, and water.enabled before
// the first other water field.
template <typename Settings, typename Visit>
void forEachField(Settings& settings, Visit&& visit)
{
    const WorldGenSettings defaults;
    detail::forEachNumericField(
        settings, defaults,
        [&](std::string_view path, auto& value, const auto& /*limits*/, const auto& /*fallback*/) {
            if (path == "water.lakeChance") {
                visit(kWaterEnabled, settings.water.enabled);
            }
            visit(path, value);
        });
}

// What a file may contain: the envelope and field paths, and the objects that
// lead to them.
struct Schema {
    std::set<std::string, std::less<>> fields;
    std::set<std::string, std::less<>> objects;
};

const Schema& schema()
{
    static const Schema known = [] {
        Schema result;
        result.fields = {"format", "formatVersion", "generatorVersion", "seed"};
        WorldGenSettings settings;
        forEachField(settings, [&](std::string_view path, const auto&) {
            result.fields.emplace(path);
            for (std::size_t dot = path.find('.'); dot != std::string_view::npos;
                 dot = path.find('.', dot + 1)) {
                result.objects.emplace(path.substr(0, dot));
            }
        });
        return result;
    }();
    return known;
}

// The value at a dotted path of a JSON object, created as needed.
OrderedJson& slot(OrderedJson& root, std::string_view path)
{
    OrderedJson* node = &root;
    std::size_t start = 0;
    for (;;) {
        const std::size_t dot = path.find('.', start);
        const std::string key{path.substr(
            start, dot == std::string_view::npos ? std::string_view::npos : dot - start)};
        node = &(*node)[key];
        if (dot == std::string_view::npos) {
            return *node;
        }
        start = dot + 1;
    }
}

// The JSON number for a float: the double nearest to the float's shortest
// decimal form, which the writer prints with those few digits, if it reads
// back to the same float; the float's exact value otherwise.
double jsonFloat(float value)
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

// A JSON value in a message: its type, and the value itself if it is short.
std::string describe(const Json& value)
{
    if (value.is_object() || value.is_array() || value.is_null()) {
        return value.type_name();
    }
    std::string text = value.dump(-1, ' ', false, Json::error_handler_t::replace);
    if (text.size() > 40U) {
        text = text.substr(0, 37U) + "...";
    }
    return std::format("{} {}", value.type_name(), text);
}

template <typename T>
std::optional<core::Error> readInteger(const Json& value, std::string_view path, T& out)
{
    if (!value.is_number_integer()) {
        return core::Error{std::format("{}: expected an integer, found {}", path, describe(value))};
    }
    const auto outside = [&] {
        return core::Error{std::format("{}: {} is outside the range of the field's type ({} to {})",
                                       path, value.dump(), std::numeric_limits<T>::min(),
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

std::optional<core::Error> readFloat(const Json& value, std::string_view path, float& out)
{
    if (!value.is_number()) {
        return core::Error{std::format("{}: expected a number, found {}", path, describe(value))};
    }
    const auto number = value.get<double>();
    if (!(std::abs(number) <= static_cast<double>(std::numeric_limits<float>::max()))) {
        return core::Error{
            std::format("{}: {} is outside the range of a float", path, value.dump())};
    }
    out = static_cast<float>(number);
    return std::nullopt;
}

std::optional<core::Error> readBool(const Json& value, std::string_view path, bool& out)
{
    if (!value.is_boolean()) {
        return core::Error{
            std::format("{}: expected true or false, found {}", path, describe(value))};
    }
    out = value.get<bool>();
    return std::nullopt;
}

// The value at a dotted path, or nullptr if the file leaves it out; an error
// if something on the way is not an object.
core::Result<const Json*> lookUp(const Json& root, std::string_view path)
{
    const Json* node = &root;
    std::size_t start = 0;
    for (;;) {
        const std::size_t dot = path.find('.', start);
        const std::string_view key = path.substr(
            start, dot == std::string_view::npos ? std::string_view::npos : dot - start);
        const auto found = node->find(std::string{key});
        if (found == node->end()) {
            return static_cast<const Json*>(nullptr);
        }
        node = &*found;
        if (dot == std::string_view::npos) {
            return node;
        }
        if (!node->is_object()) {
            return core::Error{std::format("{}: expected an object, found {}", path.substr(0, dot),
                                           describe(*node))};
        }
        start = dot + 1;
    }
}

// The seed: "0x" followed by 1 to 16 hex digits.
std::optional<std::uint64_t> parseSeed(const std::string& text)
{
    if (text.size() < 3U || text.size() > 18U || text[0] != '0' ||
        (text[1] != 'x' && text[1] != 'X')) {
        return std::nullopt;
    }
    std::uint64_t value = 0;
    const char* first = text.data() + 2;
    const char* last = text.data() + text.size();
    const auto [end, error] = std::from_chars(first, last, value, 16);
    if (error != std::errc{} || end != last) {
        return std::nullopt;
    }
    return value;
}

// Keys of objects the reader does not know, as warnings; objects it knows are
// searched too.
void collectUnknown(const Json& node, const std::string& prefix, std::vector<std::string>& warnings)
{
    const Schema& known = schema();
    for (auto entry = node.begin(); entry != node.end(); ++entry) {
        const std::string path = prefix.empty() ? entry.key() : prefix + "." + entry.key();
        if (known.objects.contains(path)) {
            if (entry->is_object()) {
                collectUnknown(*entry, path, warnings);
            }
        } else if (!known.fields.contains(path)) {
            warnings.push_back(std::format("unknown field '{}' ignored", path));
        }
    }
}

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

std::string settingsToJson(const WorldGenSettings& settings)
{
    OrderedJson root = OrderedJson::object();
    root["format"] = std::string{kSettingsFormat};
    root["formatVersion"] = kSettingsFormatVersion;
    root["generatorVersion"] = settings.generatorVersion;
    root["seed"] = std::format("0x{:016X}", settings.seed);
    forEachField(settings, [&](std::string_view path, const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, float>) {
            slot(root, path) = jsonFloat(value);
        } else {
            slot(root, path) = value;
        }
    });
    return root.dump(2, ' ', false, OrderedJson::error_handler_t::replace) + "\n";
}

core::Result<LoadedSettings> settingsFromJson(std::string_view text)
{
    SyntaxErrorFinder finder;
    if (!Json::sax_parse(text, &finder)) {
        return core::Error{"malformed JSON: " + finder.message};
    }
    const Json root = Json::parse(text, nullptr, false);
    if (root.is_discarded()) {
        return core::Error{"malformed JSON"};
    }
    if (!root.is_object()) {
        return core::Error{std::format("expected a JSON object, found {}", describe(root))};
    }

    // The envelope.
    const auto formatField = root.find("format");
    if (formatField == root.end()) {
        return core::Error{std::format("format: missing; a settings file has \"format\": \"{}\"",
                                       kSettingsFormat)};
    }
    if (!formatField->is_string() || formatField->get<std::string>() != kSettingsFormat) {
        return core::Error{std::format("format: expected \"{}\", found {}", kSettingsFormat,
                                       describe(*formatField))};
    }
    const auto versionField = root.find("formatVersion");
    if (versionField == root.end()) {
        return core::Error{"formatVersion: missing"};
    }
    std::int64_t formatVersion = 0;
    if (auto error = readInteger(*versionField, "formatVersion", formatVersion)) {
        return std::move(*error);
    }
    if (formatVersion > kSettingsFormatVersion) {
        return core::Error{std::format(
            "formatVersion {} is newer than this build reads (up to {}); update the program",
            formatVersion, kSettingsFormatVersion)};
    }
    if (formatVersion < 1) {
        return core::Error{
            std::format("formatVersion: {} is not a version (versions start at 1)", formatVersion)};
    }
    // Version 1 is the first, so there is nothing to migrate yet; later
    // versions add a migration from each older one here (ADR 0004).

    LoadedSettings loaded;
    WorldGenSettings& settings = loaded.settings;
    if (const auto version = root.find("generatorVersion"); version != root.end()) {
        if (auto error = readInteger(*version, "generatorVersion", settings.generatorVersion)) {
            return std::move(*error);
        }
    }
    if (const auto seed = root.find("seed"); seed != root.end()) {
        const std::optional<std::uint64_t> value =
            seed->is_string() ? parseSeed(seed->get<std::string>()) : std::nullopt;
        if (!value) {
            return core::Error{std::format(
                "seed: expected a hex string such as \"0x0000000000000001\", found {}{}",
                describe(*seed),
                seed->is_number() ? "; seeds are strings so that no tool rounds them" : "")};
        }
        settings.seed = *value;
    }

    // The fields; those the file leaves out keep their defaults.
    std::optional<core::Error> failure;
    forEachField(settings, [&](std::string_view path, auto& value) {
        if (failure) {
            return;
        }
        auto found = lookUp(root, path);
        if (!found) {
            failure = found.error();
            return;
        }
        const Json* json = found.value();
        if (json == nullptr) {
            return;
        }
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, float>) {
            failure = readFloat(*json, path, value);
        } else if constexpr (std::is_same_v<T, bool>) {
            failure = readBool(*json, path, value);
        } else {
            failure = readInteger(*json, path, value);
        }
    });
    if (failure) {
        return std::move(*failure);
    }

    collectUnknown(root, "", loaded.warnings);
    if (auto error = validate(settings)) {
        return std::move(*error);
    }
    return loaded;
}

std::optional<core::Error> saveSettings(const std::filesystem::path& path,
                                        const WorldGenSettings& settings)
{
    if (auto error = validate(settings)) {
        return core::Error{
            std::format("settings not saved to '{}': {}", core::toUtf8(path), error->message)};
    }
    const std::string text = settingsToJson(settings);
    return core::writeBinaryFile(
        path, std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(text.data()),
                                            text.size()));
}

core::Result<LoadedSettings> loadSettings(const std::filesystem::path& path)
{
    auto text = core::readTextFile(path);
    if (!text) {
        return text.error();
    }
    auto loaded = settingsFromJson(text.value());
    if (!loaded) {
        return core::Error{std::format("'{}': {}", core::toUtf8(path), loaded.error().message)};
    }
    return loaded;
}

} // namespace engine::worldgen
