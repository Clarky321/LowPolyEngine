#include "SaveJournal.hpp"

#include "../core/JsonSupport.hpp"

#include <format>
#include <utility>

namespace engine::project::detail {

namespace {

using core::json::describe;
using core::json::Json;
using core::json::OrderedJson;

bool isLowerHex(char c) noexcept
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
}

core::Result<assets::AssetPath> readPath(const Json& entry, std::string_view key,
                                         const std::string& at)
{
    const auto found = entry.find(key);
    if (found == entry.end()) {
        return core::Error{std::format("{}.{}: missing", at, key)};
    }
    if (!found->is_string()) {
        return core::Error{
            std::format("{}.{}: expected a string, found {}", at, key, describe(*found))};
    }
    auto path = assets::AssetPath::parse(found->get<std::string>());
    if (!path) {
        return core::Error{std::format("{}.{}: {}", at, key, path.error().message)};
    }
    return path;
}

} // namespace

bool isSaveId(std::string_view saveId) noexcept
{
    if (saveId.size() != kSaveIdDigits) {
        return false;
    }
    for (const char c : saveId) {
        if (!isLowerHex(c)) {
            return false;
        }
    }
    return true;
}

core::Result<assets::AssetPath> temporaryPath(const assets::AssetPath& target,
                                              std::string_view saveId)
{
    if (!isSaveId(saveId)) {
        return core::Error{std::format("'{}' is not a save's id", saveId)};
    }
    return assets::AssetPath::parse(target.text() + std::string(kTemporaryMarker) +
                                    std::string(saveId));
}

bool isTemporaryOf(std::string_view fileName, std::string_view targetFileName) noexcept
{
    const std::size_t prefix = targetFileName.size() + kTemporaryMarker.size();
    return fileName.size() == prefix + kSaveIdDigits && fileName.starts_with(targetFileName) &&
           fileName.substr(targetFileName.size(), kTemporaryMarker.size()) == kTemporaryMarker &&
           isSaveId(fileName.substr(prefix));
}

std::string journalToJson(std::span<const JournalEntry> entries)
{
    OrderedJson root = OrderedJson::object();
    root["format"] = std::string(kSaveJournalFormat);
    root["formatVersion"] = kSaveJournalFormatVersion;
    OrderedJson list = OrderedJson::array();
    for (const JournalEntry& entry : entries) {
        OrderedJson item = OrderedJson::object();
        item["target"] = entry.target.text();
        item["temporary"] = entry.temporary.text();
        list.push_back(std::move(item));
    }
    root["files"] = std::move(list);
    return root.dump(2, ' ', false, OrderedJson::error_handler_t::replace) + "\n";
}

core::Result<std::vector<JournalEntry>> journalFromJson(std::string_view text)
{
    if (auto syntax = core::json::syntaxError(text)) {
        return core::Error{"malformed JSON: " + *syntax};
    }
    const Json root = Json::parse(text, nullptr, false);
    if (root.is_discarded()) {
        return core::Error{"malformed JSON"};
    }
    if (!root.is_object()) {
        return core::Error{std::format("expected a JSON object, found {}", describe(root))};
    }
    if (auto version =
            core::json::readEnvelope(root, kSaveJournalFormat, kSaveJournalFormatVersion);
        !version) {
        return version.error();
    }
    const auto list = root.find("files");
    if (list == root.end()) {
        return core::Error{"files: missing"};
    }
    if (!list->is_array()) {
        return core::Error{std::format("files: expected an array, found {}", describe(*list))};
    }
    if (list->empty() || list->size() > kMaxJournalEntries) {
        return core::Error{
            std::format("files: {} entries, expected 1 to {}", list->size(), kMaxJournalEntries)};
    }

    std::vector<JournalEntry> entries;
    std::size_t index = 0;
    for (const Json& item : *list) {
        const std::string at = std::format("files[{}]", index++);
        if (!item.is_object()) {
            return core::Error{std::format("{}: expected an object, found {}", at, describe(item))};
        }
        auto target = readPath(item, "target", at);
        if (!target) {
            return target.error();
        }
        auto temporary = readPath(item, "temporary", at);
        if (!temporary) {
            return temporary.error();
        }
        const std::string& targetText = target.value().text();
        const std::string& temporaryText = temporary.value().text();
        const std::size_t prefix = targetText.size() + kTemporaryMarker.size();
        if (temporaryText.size() != prefix + kSaveIdDigits ||
            !temporaryText.starts_with(targetText + std::string(kTemporaryMarker)) ||
            !isSaveId(std::string_view(temporaryText).substr(prefix))) {
            return core::Error{std::format(
                "{}.temporary: '{}' is not a temporary file of '{}' (expected '{}{}' and {} "
                "hex digits)",
                at, temporaryText, targetText, targetText, kTemporaryMarker, kSaveIdDigits)};
        }
        for (const JournalEntry& earlier : entries) {
            if (earlier.target.folded() == target.value().folded()) {
                return core::Error{std::format("{}.target: '{}' is named twice", at, targetText)};
            }
        }
        entries.push_back({std::move(target).value(), std::move(temporary).value()});
    }
    return entries;
}

} // namespace engine::project::detail
