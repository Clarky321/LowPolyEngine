#include "engine/project/ProjectDocument.hpp"

#include "../core/JsonSupport.hpp"

#include "engine/scene/Scene.hpp"

#include <array>
#include <format>
#include <span>
#include <utility>

namespace engine::project {

namespace {

using core::json::describe;
using core::json::Json;
using core::json::OrderedJson;
using core::json::shortened;

constexpr std::array<std::string_view, 6> kKnownFields{
    "format", "formatVersion", "name", "assetManifest", "startScene", "worldSettings",
};

bool isOneOf(std::string_view key, std::span<const std::string_view> known)
{
    for (const std::string_view candidate : known) {
        if (candidate == key) {
            return true;
        }
    }
    return false;
}

// A required string field of the document's object.
core::Result<std::string> readString(const Json& object, std::string_view key)
{
    const auto found = object.find(key);
    if (found == object.end()) {
        return core::Error{std::format("{}: missing", key)};
    }
    if (!found->is_string()) {
        return core::Error{std::format("{}: expected a string, found {}", key, describe(*found))};
    }
    return found->get<std::string>();
}

core::Result<assets::AssetPath> readPath(const Json& object, std::string_view key)
{
    auto text = readString(object, key);
    if (!text) {
        return text.error();
    }
    auto path = assets::AssetPath::parse(text.value());
    if (!path) {
        return core::Error{std::format("{}: {}", key, path.error().message)};
    }
    return path;
}

struct PathField {
    std::string_view key;
    const assets::AssetPath* path;
};

bool startsWithFolder(const std::string& path, const std::string& folder)
{
    return path.size() > folder.size() && path.starts_with(folder) && path[folder.size()] == '/';
}

} // namespace

core::Result<ProjectDocument> defaultProjectDocument(std::string_view name)
{
    if (auto error = scene::validateName(name, "name")) {
        return *error;
    }
    auto manifest = assets::AssetPath::parse(kDefaultManifestPath);
    auto scene = assets::AssetPath::parse(kDefaultStartScenePath);
    auto world = assets::AssetPath::parse(kDefaultWorldSettingsPath);
    if (!manifest || !scene || !world) {
        return core::Error{"the default project paths are invalid"};
    }
    return ProjectDocument{std::string(name), std::move(manifest).value(), std::move(scene).value(),
                           std::move(world).value()};
}

std::optional<core::Error> validate(const ProjectDocument& document)
{
    if (auto error = scene::validateName(document.name, "name")) {
        return error;
    }
    const std::array<PathField, 3> fields{{
        {"assetManifest", &document.assetManifest},
        {"startScene", &document.startScene},
        {"worldSettings", &document.worldSettings},
    }};
    const std::string documentName = assets::foldCase(kProjectFileName);
    const std::string journalName = assets::foldCase(kSaveJournalFileName);
    for (const PathField& field : fields) {
        const std::string folded = field.path->folded();
        if (folded == documentName || folded == journalName) {
            return core::Error{std::format("{}: '{}' is reserved for the project's own files",
                                           field.key, field.path->text())};
        }
    }
    for (std::size_t i = 0; i < fields.size(); ++i) {
        for (std::size_t j = i + 1; j < fields.size(); ++j) {
            const PathField& first = fields[i];
            const PathField& second = fields[j];
            const std::string a = first.path->folded();
            const std::string b = second.path->folded();
            if (a == b) {
                if (first.path->text() == second.path->text()) {
                    return core::Error{std::format("{}: '{}' is also the {}", second.key,
                                                   second.path->text(), first.key)};
                }
                return core::Error{
                    std::format("{}: '{}' differs from the {} '{}' only in letter case", second.key,
                                second.path->text(), first.key, first.path->text())};
            }
            if (startsWithFolder(b, a)) {
                return core::Error{std::format("{}: '{}' lies inside the {} '{}'", second.key,
                                               second.path->text(), first.key, first.path->text())};
            }
            if (startsWithFolder(a, b)) {
                return core::Error{std::format("{}: '{}' lies inside the {} '{}'", first.key,
                                               first.path->text(), second.key,
                                               second.path->text())};
            }
        }
    }
    return std::nullopt;
}

std::string projectDocumentToJson(const ProjectDocument& document)
{
    OrderedJson root = OrderedJson::object();
    root["format"] = std::string(kProjectFormat);
    root["formatVersion"] = kProjectFormatVersion;
    root["name"] = document.name;
    root["assetManifest"] = document.assetManifest.text();
    root["startScene"] = document.startScene.text();
    root["worldSettings"] = document.worldSettings.text();
    return root.dump(2, ' ', false, OrderedJson::error_handler_t::replace) + "\n";
}

core::Result<LoadedProjectDocument> projectDocumentFromJson(std::string_view text)
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
    if (auto version = core::json::readEnvelope(root, kProjectFormat, kProjectFormatVersion);
        !version) {
        return version.error();
    }

    auto name = readString(root, "name");
    if (!name) {
        return name.error();
    }
    if (auto error = scene::validateName(name.value(), "name")) {
        return *error;
    }
    auto manifest = readPath(root, "assetManifest");
    if (!manifest) {
        return manifest.error();
    }
    auto scene = readPath(root, "startScene");
    if (!scene) {
        return scene.error();
    }
    auto world = readPath(root, "worldSettings");
    if (!world) {
        return world.error();
    }

    LoadedProjectDocument loaded{
        ProjectDocument{std::move(name).value(), std::move(manifest).value(),
                        std::move(scene).value(), std::move(world).value()},
        {},
    };
    if (auto error = validate(loaded.document)) {
        return *error;
    }
    for (const auto& item : root.items()) {
        if (!isOneOf(item.key(), kKnownFields)) {
            loaded.warnings.push_back(
                std::format("unknown field '{}' ignored", shortened(item.key())));
        }
    }
    return loaded;
}

} // namespace engine::project
