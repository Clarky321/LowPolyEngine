// The save journal of a project (engine/src/project/SaveJournal, M5.4, ADR
// 0028): temporary file names, the journal's text and what reading refuses;
// a broken journal, or one that names a file outside the project's parts,
// fails the open with the journal's name.

#include "ProjectTestSupport.hpp"

#include "project/SaveJournal.hpp"

#include "engine/project/Project.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>
#include <vector>

using engine::project::detail::isSaveId;
using engine::project::detail::isTemporaryOf;
using engine::project::detail::JournalEntry;
using engine::project::detail::journalFromJson;
using engine::project::detail::journalToJson;
using engine::project::detail::temporaryPath;

namespace {

engine::assets::AssetPath pathOf(std::string_view text)
{
    auto path = engine::assets::AssetPath::parse(text);
    REQUIRE(path.hasValue());
    return path.value();
}

std::string journalError(std::string_view text)
{
    const auto journal = journalFromJson(text);
    REQUIRE_FALSE(journal.hasValue());
    return journal.error().message;
}

std::string journalWith(std::string_view files)
{
    return "{\"format\": \"lpe.project.save\", \"formatVersion\": 1, \"files\": " +
           std::string(files) + "}";
}

bool contains(const std::string& text, std::string_view part)
{
    return text.find(part) != std::string::npos;
}

} // namespace

TEST_CASE("A save's temporary files are named after their file and the save", "[project][save]")
{
    CHECK(isSaveId("0123abcd"));
    CHECK_FALSE(isSaveId("0123ABCD"));
    CHECK_FALSE(isSaveId("0123abc"));
    CHECK_FALSE(isSaveId("0123abcde"));
    CHECK_FALSE(isSaveId("0123abcg"));

    const auto temporary = temporaryPath(pathOf("scenes/main.scene.json"), "00ff00ff");
    REQUIRE(temporary.hasValue());
    CHECK(temporary.value().text() == "scenes/main.scene.json.saving-00ff00ff");
    CHECK_FALSE(temporaryPath(pathOf("a.json"), "xyz").hasValue());

    CHECK(isTemporaryOf("main.scene.json.saving-00ff00ff", "main.scene.json"));
    CHECK_FALSE(isTemporaryOf("main.scene.json.saving-00ff00f", "main.scene.json"));
    CHECK_FALSE(isTemporaryOf("main.scene.json.saving-00ff00ff", "other.json"));
    CHECK_FALSE(isTemporaryOf("main.scene.json.tmp-00ff00ff", "main.scene.json"));
    CHECK_FALSE(isTemporaryOf("main.scene.json", "main.scene.json"));
}

TEST_CASE("A save journal reads back what it lists", "[project][save]")
{
    const std::vector<JournalEntry> entries{
        {pathOf("assets.json"), pathOf("assets.json.saving-0a0b0c0d")},
        {pathOf("scenes/main.scene.json"), pathOf("scenes/main.scene.json.saving-0a0b0c0d")},
    };
    const std::string text = journalToJson(entries);
    CHECK(text == R"({
  "format": "lpe.project.save",
  "formatVersion": 1,
  "files": [
    {
      "target": "assets.json",
      "temporary": "assets.json.saving-0a0b0c0d"
    },
    {
      "target": "scenes/main.scene.json",
      "temporary": "scenes/main.scene.json.saving-0a0b0c0d"
    }
  ]
}
)");
    const auto read = journalFromJson(text);
    REQUIRE(read.hasValue());
    REQUIRE(read.value().size() == 2U);
    CHECK(read.value()[1].target.text() == "scenes/main.scene.json");
    CHECK(read.value()[1].temporary.text() == "scenes/main.scene.json.saving-0a0b0c0d");
}

TEST_CASE("A save journal that could make open read another file is refused", "[project][save]")
{
    CHECK(contains(journalError(journalWith("[]")), "files: 0 entries"));
    CHECK(contains(journalError("{\"format\": \"lpe.project.save\", \"formatVersion\": 1}"),
                   "files: missing"));
    CHECK(contains(journalError(journalWith("{}")), "files: expected an array"));
    CHECK(contains(journalError(journalWith("[1]")), "files[0]: expected an object"));
    CHECK(contains(journalError(journalWith("[{\"target\": \"a.json\"}]")),
                   "files[0].temporary: missing"));
    CHECK(contains(journalError(journalWith(
                       "[{\"target\": \"a.json\", \"temporary\": \"b.json.saving-00000000\"}]")),
                   "is not a temporary file of 'a.json'"));
    CHECK(contains(journalError(journalWith(
                       "[{\"target\": \"a.json\", \"temporary\": \"a.json.saving-0000\"}]")),
                   "is not a temporary file of 'a.json'"));
    CHECK(contains(journalError(journalWith(
                       "[{\"target\": \"../a.json\", \"temporary\": \"a.json.saving-00000000\"}]")),
                   "files[0].target: "));
    CHECK(contains(journalError(journalWith(
                       "[{\"target\": \"a.json\", \"temporary\": \"a.json.saving-00000000\"}, "
                       "{\"target\": \"A.json\", \"temporary\": \"A.json.saving-00000000\"}]")),
                   "files[1].target: 'A.json' is named twice"));
    CHECK(contains(journalError("{\"format\": \"lpe.project.save\", \"formatVersion\": 2}"),
                   "newer"));
    CHECK(contains(journalError("{\"format\": \"lpe.project\", \"formatVersion\": 1}"),
                   "lpe.project.save"));
}

TEST_CASE("A broken save journal fails the open with its name", "[project][save]")
{
    const lpe_test::TempProject folder("lpe_project_journal_broken");
    const std::filesystem::path& root = folder.root();
    REQUIRE(engine::project::createProject(root, "P", engine::core::UuidGenerator{1}).hasValue());

    folder.write("project.saving.json", "{ not json");
    auto opened = engine::project::openProject(root, engine::core::UuidGenerator{1});
    REQUIRE_FALSE(opened.hasValue());
    CHECK(opened.error().message.starts_with("project.saving.json: malformed JSON"));
    // A save refuses too, before writing anything: it cannot know which save is whole.
    const lpe_test::ProjectTexts before = lpe_test::textsOnDisk(root);
    const engine::project::Project project = lpe_test::projectVersion(folder, root, 1);
    const auto saved = engine::project::saveProject(project);
    REQUIRE_FALSE(saved.hasValue());
    CHECK(contains(saved.error().message, "project.saving.json: malformed JSON"));
    CHECK(contains(saved.error().message, "nothing was written"));
    CHECK(lpe_test::textsOnDisk(root) == before);

    // A journal that names a file which is not one of the project's parts.
    folder.write("models/tree.obj", "v 0 0 0\n");
    folder.write("models/tree.obj.saving-00000000", "v 1 1 1\n");
    folder.write("project.saving.json",
                 journalWith("[{\"target\": \"models/tree.obj\", \"temporary\": "
                             "\"models/tree.obj.saving-00000000\"}]"));
    opened = engine::project::openProject(root, engine::core::UuidGenerator{1});
    REQUIRE_FALSE(opened.hasValue());
    CHECK(opened.error().message ==
          "project.saving.json: 'models/tree.obj' is not a part of the project");
    CHECK(lpe_test::fileText(root / "models" / "tree.obj") == "v 0 0 0\n");
}
