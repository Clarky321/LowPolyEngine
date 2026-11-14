#include "engine/assets/AssetManifest.hpp"

#include "engine/core/FileSystem.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <format>
#include <span>
#include <system_error>
#include <utility>

namespace engine::assets {

namespace {

using Json = nlohmann::json;
using OrderedJson = nlohmann::ordered_json;

constexpr std::array<std::string_view, 3> kEnvelopeFields{"format", "formatVersion", "assets"};
constexpr std::array<std::string_view, 3> kAssetFields{"id", "type", "path"};

// At most 40 bytes of text from the file, cut at a character boundary.
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

// A JSON value in a message: its type, and the value itself if it is short.
std::string describe(const Json& value)
{
    if (value.is_object() || value.is_array() || value.is_null()) {
        return value.type_name();
    }
    return std::format("{} {}", value.type_name(),
                       shortened(value.dump(-1, ' ', false, Json::error_handler_t::replace)));
}

bool isOneOf(std::string_view key, std::span<const std::string_view> known)
{
    for (const std::string_view candidate : known) {
        if (candidate == key) {
            return true;
        }
    }
    return false;
}

// A required string field of an object.
core::Result<std::string> readString(const Json& object, std::string_view key, std::string_view at)
{
    const auto found = object.find(key);
    if (found == object.end()) {
        return core::Error{std::format("{}.{}: missing", at, key)};
    }
    if (!found->is_string()) {
        return core::Error{
            std::format("{}.{}: expected a string, found {}", at, key, describe(*found))};
    }
    return found->get<std::string>();
}

std::string typeList()
{
    std::string list;
    for (const AssetType type : kAssetTypes) {
        list += list.empty() ? std::string(assetTypeName(type))
                             : ", " + std::string(assetTypeName(type));
    }
    return list;
}

// Finds where malformed JSON goes wrong, without exceptions: a parser that
// accepts every value and records the first syntax error (as in
// worldgen/SettingsFile.cpp).
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

std::optional<core::Error> readEnvelope(const Json& root)
{
    const auto formatField = root.find("format");
    if (formatField == root.end()) {
        return core::Error{std::format("format: missing, expected \"{}\"", kManifestFormat)};
    }
    if (!formatField->is_string() || formatField->get<std::string>() != kManifestFormat) {
        return core::Error{std::format("format: expected \"{}\", found {}", kManifestFormat,
                                       describe(*formatField))};
    }
    const auto version = root.find("formatVersion");
    if (version == root.end()) {
        return core::Error{"formatVersion: missing"};
    }
    if (!version->is_number_integer()) {
        return core::Error{
            std::format("formatVersion: expected an integer, found {}", describe(*version))};
    }
    const auto number = version->get<std::int64_t>();
    if (number < 1) {
        return core::Error{
            std::format("formatVersion: {} is not a version (the first is 1)", number)};
    }
    if (number > kManifestFormatVersion) {
        return core::Error{std::format("formatVersion: {} is newer than this program reads ({}); "
                                       "a newer version of the engine wrote the file",
                                       number, kManifestFormatVersion)};
    }
    return std::nullopt;
}

} // namespace

std::string manifestToJson(const AssetRegistry& registry)
{
    OrderedJson root = OrderedJson::object();
    root["format"] = std::string(kManifestFormat);
    root["formatVersion"] = kManifestFormatVersion;
    OrderedJson list = OrderedJson::array();
    for (const AssetMetadata& asset : registry.assets()) {
        OrderedJson entry = OrderedJson::object();
        entry["id"] = asset.id.toString();
        entry["type"] = std::string(assetTypeName(asset.type));
        entry["path"] = asset.path.text();
        list.push_back(std::move(entry));
    }
    root["assets"] = std::move(list);
    return root.dump(2, ' ', false, OrderedJson::error_handler_t::replace) + "\n";
}

core::Result<LoadedManifest> manifestFromJson(std::string_view text,
                                              const std::filesystem::path& projectRoot)
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
    if (auto error = readEnvelope(root)) {
        return *error;
    }

    LoadedManifest loaded{AssetRegistry(projectRoot), {}};
    for (const auto& item : root.items()) {
        if (!isOneOf(item.key(), kEnvelopeFields)) {
            loaded.warnings.push_back(
                std::format("unknown field '{}' ignored", shortened(item.key())));
        }
    }

    const auto list = root.find("assets");
    if (list == root.end()) {
        // No list: a project without assets.
        return loaded;
    }
    if (!list->is_array()) {
        return core::Error{std::format("assets: expected an array, found {}", describe(*list))};
    }
    if (list->size() > kMaxManifestAssets) {
        return core::Error{std::format("assets: {} entries, at most {} allowed", list->size(),
                                       kMaxManifestAssets)};
    }

    std::size_t index = 0;
    for (const Json& entry : *list) {
        const std::string at = std::format("assets[{}]", index++);
        if (!entry.is_object()) {
            return core::Error{
                std::format("{}: expected an object, found {}", at, describe(entry))};
        }
        auto idText = readString(entry, "id", at);
        if (!idText) {
            return idText.error();
        }
        auto id = AssetId::parse(idText.value());
        if (!id) {
            return core::Error{std::format("{}.id: {}", at, id.error().message)};
        }
        auto typeText = readString(entry, "type", at);
        if (!typeText) {
            return typeText.error();
        }
        const auto type = parseAssetType(typeText.value());
        if (!type) {
            return core::Error{std::format("{}.type: unknown asset type \"{}\"; known: {}", at,
                                           shortened(typeText.value()), typeList())};
        }
        auto pathText = readString(entry, "path", at);
        if (!pathText) {
            return pathText.error();
        }
        auto path = AssetPath::parse(pathText.value());
        if (!path) {
            return core::Error{std::format("{}.path: {}", at, path.error().message)};
        }
        for (const auto& item : entry.items()) {
            if (!isOneOf(item.key(), kAssetFields)) {
                loaded.warnings.push_back(
                    std::format("unknown field '{}.{}' ignored", at, shortened(item.key())));
            }
        }
        if (auto error = loaded.registry.registerAsset(id.value(), *type, path.value())) {
            return core::Error{std::format("{}: {}", at, error->message)};
        }
    }
    return loaded;
}

std::optional<core::Error> saveManifest(const std::filesystem::path& file,
                                        const AssetRegistry& registry)
{
    const std::string text = manifestToJson(registry);
    return core::writeBinaryFile(
        file, std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(text.data()),
                                            text.size()));
}

core::Result<LoadedManifest> loadManifest(const std::filesystem::path& file,
                                          const std::filesystem::path& projectRoot)
{
    std::error_code error;
    const auto size = std::filesystem::file_size(file, error);
    if (!error && size > kMaxManifestBytes) {
        return core::Error{std::format("'{}': {} bytes, at most {} allowed", core::toUtf8(file),
                                       size, kMaxManifestBytes)};
    }
    auto text = core::readTextFile(file);
    if (!text) {
        return text.error();
    }
    auto loaded = manifestFromJson(text.value(), projectRoot);
    if (!loaded) {
        return core::Error{std::format("'{}': {}", core::toUtf8(file), loaded.error().message)};
    }
    return loaded;
}

} // namespace engine::assets
