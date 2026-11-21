// The project document (engine/project/ProjectDocument.hpp, M5.4, ADR 0028):
// its exact text, the round trip, missing, unknown and wrongly typed fields,
// newer versions, paths that are absolute, outside the project, the same as
// another or reserved, and the name's rules. No files.

#include "engine/project/ProjectDocument.hpp"

#include <catch2/catch_test_macros.hpp>

#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>

using engine::project::defaultProjectDocument;
using engine::project::ProjectDocument;
using engine::project::projectDocumentFromJson;
using engine::project::projectDocumentToJson;

namespace {

constexpr std::string_view kDefaultText = R"({
  "format": "lpe.project",
  "formatVersion": 1,
  "name": "Sky Islands",
  "assetManifest": "assets.json",
  "startScene": "scenes/main.scene.json",
  "worldSettings": "worlds/world.json"
}
)";

// A document's text with one field's JSON value replaced (or the field left
// out when `value` is empty).
std::string withField(std::string_view key, std::string_view value)
{
    std::string text = "{\"format\": \"lpe.project\", \"formatVersion\": 1";
    for (const auto& [name, defaultValue] :
         {std::pair<std::string_view, std::string_view>{"name", "\"P\""},
          {"assetManifest", "\"assets.json\""},
          {"startScene", "\"scenes/main.scene.json\""},
          {"worldSettings", "\"worlds/world.json\""}}) {
        const std::string_view used = name == key ? value : defaultValue;
        if (!used.empty()) {
            text += ", \"" + std::string(name) + "\": " + std::string(used);
        }
    }
    return text + "}";
}

std::string errorOf(std::string_view text)
{
    const auto loaded = projectDocumentFromJson(text);
    REQUIRE_FALSE(loaded.hasValue());
    return loaded.error().message;
}

bool contains(const std::string& text, std::string_view part)
{
    return text.find(part) != std::string::npos;
}

ProjectDocument documentWithPaths(std::string_view manifest, std::string_view scene,
                                  std::string_view world)
{
    auto document = defaultProjectDocument("P");
    REQUIRE(document.hasValue());
    auto m = engine::assets::AssetPath::parse(manifest);
    auto s = engine::assets::AssetPath::parse(scene);
    auto w = engine::assets::AssetPath::parse(world);
    REQUIRE(m.hasValue());
    REQUIRE(s.hasValue());
    REQUIRE(w.hasValue());
    document.value().assetManifest = m.value();
    document.value().startScene = s.value();
    document.value().worldSettings = w.value();
    return document.value();
}

} // namespace

TEST_CASE("A project document has an exact text and reads back the same", "[project][document]")
{
    const auto document = defaultProjectDocument("Sky Islands");
    REQUIRE(document.hasValue());
    CHECK(projectDocumentToJson(document.value()) == kDefaultText);

    const auto loaded = projectDocumentFromJson(kDefaultText);
    REQUIRE(loaded.hasValue());
    CHECK(loaded.value().warnings.empty());
    const ProjectDocument& read = loaded.value().document;
    CHECK(read.name == "Sky Islands");
    CHECK(read.assetManifest.text() == "assets.json");
    CHECK(read.startScene.text() == "scenes/main.scene.json");
    CHECK(read.worldSettings.text() == "worlds/world.json");
    CHECK(projectDocumentToJson(read) == kDefaultText);
}

TEST_CASE("A project document keeps a UTF-8 name and writes normalized paths",
          "[project][document]")
{
    const std::string text = withField("name", "\"Небесные острова\"");
    const auto loaded = projectDocumentFromJson(
        "{\"format\": \"lpe.project\", \"formatVersion\": 1, \"name\": \"Небесные острова\", "
        "\"assetManifest\": \"./data//assets.json\", \"startScene\": "
        "\"scenes/../scenes/start.scene.json\", \"worldSettings\": \"world.json\"}");
    REQUIRE(loaded.hasValue());
    CHECK(loaded.value().document.name == "Небесные острова");
    CHECK(loaded.value().document.assetManifest.text() == "data/assets.json");
    CHECK(loaded.value().document.startScene.text() == "scenes/start.scene.json");
    const std::string written = projectDocumentToJson(loaded.value().document);
    CHECK(contains(written, "\"assetManifest\": \"data/assets.json\""));
    CHECK(projectDocumentFromJson(text).hasValue());
}

TEST_CASE("Unknown fields of a project document are ignored with a warning", "[project][document]")
{
    const auto loaded = projectDocumentFromJson(
        "{\"format\": \"lpe.project\", \"formatVersion\": 1, \"name\": \"P\", \"assetManifest\": "
        "\"assets.json\", \"startScene\": \"scenes/main.scene.json\", \"worldSettings\": "
        "\"worlds/world.json\", \"author\": \"someone\", \"scenes\": []}");
    REQUIRE(loaded.hasValue());
    REQUIRE(loaded.value().warnings.size() == 2U);
    CHECK(loaded.value().warnings[0] == "unknown field 'author' ignored");
    CHECK(loaded.value().warnings[1] == "unknown field 'scenes' ignored");
}

TEST_CASE("Missing and wrongly typed fields of a project document are refused",
          "[project][document]")
{
    for (const std::string_view key : {"name", "assetManifest", "startScene", "worldSettings"}) {
        INFO("field " << key);
        CHECK(errorOf(withField(key, "")) == std::string(key) + ": missing");
        CHECK(errorOf(withField(key, "3")) ==
              std::string(key) + ": expected a string, found number 3");
        CHECK(contains(errorOf(withField(key, "null")), std::string(key) + ": expected a string"));
        CHECK(
            contains(errorOf(withField(key, "[\"a\"]")), std::string(key) + ": expected a string"));
    }
}

TEST_CASE("The envelope of a project document is checked", "[project][document]")
{
    CHECK(contains(errorOf("{\"format\": \"lpe.scene\", \"formatVersion\": 1}"), "lpe.project"));
    CHECK(contains(errorOf("{\"formatVersion\": 1}"), "format"));
    CHECK(contains(errorOf("{\"format\": \"lpe.project\"}"), "formatVersion"));
    CHECK(
        contains(errorOf("{\"format\": \"lpe.project\", \"formatVersion\": 0}"), "formatVersion"));
    CHECK(contains(errorOf("{\"format\": \"lpe.project\", \"formatVersion\": 1.5}"),
                   "formatVersion"));
    // A newer engine wrote it.
    const std::string newer = errorOf("{\"format\": \"lpe.project\", \"formatVersion\": 2, "
                                      "\"name\": \"P\"}");
    CHECK(contains(newer, "newer"));
    CHECK(contains(errorOf("[1, 2]"), "expected a JSON object"));
    CHECK(contains(errorOf(""), "malformed JSON"));
    const std::string malformed = errorOf("{\n  \"format\": \"lpe.project\",\n  oops\n}");
    CHECK(contains(malformed, "malformed JSON"));
    CHECK(contains(malformed, "line 3"));
}

TEST_CASE("The paths of a project document stay inside the project", "[project][document]")
{
    CHECK(contains(errorOf(withField("startScene", "\"C:/scenes/main.scene.json\"")),
                   "startScene: "));
    CHECK(contains(errorOf(withField("startScene", "\"C:/scenes/main.scene.json\"")),
                   "drive letter"));
    CHECK(contains(errorOf(withField("assetManifest", "\"/assets.json\"")), "assetManifest: "));
    CHECK(contains(errorOf(withField("worldSettings", "\"../world.json\"")), "worldSettings: "));
    CHECK(contains(errorOf(withField("worldSettings", "\"worlds\\\\world.json\"")),
                   "worldSettings: "));
    CHECK(contains(errorOf(withField("startScene", "\"\"")), "startScene: "));
}

TEST_CASE("The parts of a project are different files", "[project][document]")
{
    using engine::project::validate;
    CHECK_FALSE(validate(documentWithPaths("assets.json", "s.json", "w.json")).has_value());

    const auto same = validate(documentWithPaths("assets.json", "assets.json", "w.json"));
    REQUIRE(same.has_value());
    CHECK(same->message == "startScene: 'assets.json' is also the assetManifest");

    const auto caseOnly = validate(documentWithPaths("a.json", "s.json", "A.json"));
    REQUIRE(caseOnly.has_value());
    CHECK(caseOnly->message ==
          "worldSettings: 'A.json' differs from the assetManifest 'a.json' only in letter case");

    const auto inside = validate(documentWithPaths("data", "data/main.scene.json", "w.json"));
    REQUIRE(inside.has_value());
    CHECK(inside->message == "startScene: 'data/main.scene.json' lies inside the assetManifest "
                             "'data'");

    for (const std::string_view reserved :
         {"project.json", "Project.JSON", "project.saving.json"}) {
        INFO(reserved);
        const auto error = validate(documentWithPaths("assets.json", "s.json", reserved));
        REQUIRE(error.has_value());
        CHECK(contains(error->message, "worldSettings: '"));
        CHECK(contains(error->message, "reserved for the project's own files"));
    }
    // The same checks apply when reading.
    CHECK(errorOf(withField("startScene", "\"assets.json\"")) ==
          "startScene: 'assets.json' is also the assetManifest");
}

TEST_CASE("A project's name follows the rules of scene names", "[project][document]")
{
    CHECK(contains(errorOf(withField("name", "\"\"")), "name: "));
    CHECK(contains(errorOf(withField("name", "\"a\\u0007b\"")), "name: "));
    CHECK(contains(errorOf(withField("name", "\"" + std::string(257, 'x') + "\"")), "name: "));
    CHECK(withField("name", "\"" + std::string(256, 'x') + "\"").size() > 256U);
    CHECK(
        projectDocumentFromJson(withField("name", "\"" + std::string(256, 'x') + "\"")).hasValue());
    CHECK_FALSE(defaultProjectDocument("").hasValue());
    CHECK_FALSE(defaultProjectDocument(std::string_view("a\nb")).hasValue());
}
