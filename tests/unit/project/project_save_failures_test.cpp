// A project saved consistently as a whole (M5.4, ADR 0028, plan addition 2):
// every file operation of a save is made to fail in turn, once as an error
// (later operations work, so the save cleans up after itself) and once as a
// crash (that operation and every later one fail; a write leaves half of its
// bytes). Afterwards the project opens entirely as the previous save or
// entirely as the new one, never as a mixture; opening writes nothing; and
// the next save finishes the interrupted one and leaves no leftover file.

#include "ProjectTestSupport.hpp"

#include "project/ProjectFiles.hpp"

#include "engine/project/Project.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using engine::core::UuidGenerator;
using engine::project::detail::ProjectFiles;
using lpe_test::filesBelow;
using lpe_test::ProjectTexts;
using lpe_test::projectVersion;
using lpe_test::TempProject;
using lpe_test::textsOf;

namespace {

enum class Failure { Error, Crash };

const char* nameOf(Failure failure)
{
    return failure == Failure::Error ? "error" : "crash";
}

// The real file system, except that the changing operation number `failAt`
// (writes, renames, removals, new folders, counted from 0) fails: alone
// (Error), or with every later one (Crash: the program stopped there; a write
// it was doing leaves half of its bytes). A negative `failAt` fails nothing.
class FaultyFiles final : public ProjectFiles {
public:
    FaultyFiles(Failure failure, int failAt) : failure_(failure), failAt_(failAt) {}

    [[nodiscard]] int operations() const noexcept { return count_; }
    [[nodiscard]] bool injected() const noexcept { return injected_; }

    bool isFile(const std::filesystem::path& path) override { return disk().isFile(path); }
    bool isFolder(const std::filesystem::path& path) override { return disk().isFolder(path); }
    engine::core::Result<std::string> read(const std::filesystem::path& path,
                                           std::uintmax_t maxBytes) override
    {
        return disk().read(path, maxBytes);
    }
    engine::core::Result<std::vector<std::string>>
    fileNames(const std::filesystem::path& folder) override
    {
        return disk().fileNames(folder);
    }

    std::optional<engine::core::Error> write(const std::filesystem::path& path,
                                             std::string_view bytes) override
    {
        if (fails()) {
            if (failure_ == Failure::Crash) {
                (void)disk().write(path, bytes.substr(0, bytes.size() / 2));
            }
            return injectedError();
        }
        return disk().write(path, bytes);
    }
    std::optional<engine::core::Error> rename(const std::filesystem::path& from,
                                              const std::filesystem::path& to) override
    {
        return fails() ? injectedError() : disk().rename(from, to);
    }
    std::optional<engine::core::Error> remove(const std::filesystem::path& path) override
    {
        return fails() ? injectedError() : disk().remove(path);
    }
    std::optional<engine::core::Error> createFolders(const std::filesystem::path& path) override
    {
        return fails() ? injectedError() : disk().createFolders(path);
    }

private:
    static ProjectFiles& disk() { return engine::project::detail::diskFiles(); }
    static engine::core::Error injectedError() { return engine::core::Error{"injected failure"}; }

    bool fails()
    {
        const int index = count_++;
        const bool fail =
            failAt_ >= 0 && (failure_ == Failure::Error ? index == failAt_ : index >= failAt_);
        injected_ = injected_ || fail;
        return fail;
    }

    Failure failure_;
    int failAt_;
    int count_ = 0;
    bool injected_ = false;
};

bool contains(const std::string& text, std::string_view part)
{
    return text.find(part) != std::string::npos;
}

bool hasTemporaryFile(const std::vector<std::string>& files)
{
    return std::any_of(files.begin(), files.end(),
                       [](const std::string& file) { return contains(file, ".saving-"); });
}

ProjectTexts openTexts(const std::filesystem::path& root, std::vector<std::string>* warnings)
{
    const auto opened = engine::project::openProject(root, UuidGenerator{5});
    REQUIRE(opened.hasValue());
    if (warnings != nullptr) {
        *warnings = opened.value().warnings;
    }
    return textsOf(opened.value().project);
}

// The project's files after saves of versions 1 to `last`: the default
// files and every version's model files.
std::vector<std::string> expectedFiles(int last)
{
    std::vector<std::string> files = lpe_test::kDefaultFiles;
    for (int version = 1; version <= last; ++version) {
        for (int i = 1; i <= version; ++i) {
            files.push_back(std::format("models/v{}-{}.obj", version, i));
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

// A project saved as version 1, then saved as version 2 through `files`.
struct Interrupted {
    ProjectTexts before;
    ProjectTexts after;
    engine::core::Result<engine::project::SaveReport> result;
};

Interrupted saveThrough(const TempProject& folder, ProjectFiles& files)
{
    const std::filesystem::path& root = folder.root();
    REQUIRE(engine::project::createProject(root, "P", UuidGenerator{1}).hasValue());
    const engine::project::Project first = projectVersion(folder, root, 1);
    REQUIRE(engine::project::saveProject(first).hasValue());
    const engine::project::Project second = projectVersion(folder, root, 2);
    return {textsOf(first), textsOf(second), engine::project::detail::saveProject(files, second)};
}

// After an interrupted save: the project opens as one of the two versions,
// the one the save's result names; opening writes nothing; the next save
// finishes everything and leaves exactly the project's files.
void checkWhole(const TempProject& folder, const Interrupted& save, Failure failure)
{
    const std::filesystem::path& root = folder.root();
    const std::vector<std::string> filesBefore = filesBelow(root);
    std::vector<std::string> warnings;
    const ProjectTexts opened = openTexts(root, &warnings);
    CHECK((opened == save.before || opened == save.after));
    if (save.result.hasValue()) {
        CHECK(opened == save.after);
    } else if (contains(save.result.error().message, "the project is unchanged") ||
               contains(save.result.error().message, "nothing was written")) {
        CHECK(opened == save.before);
        if (failure == Failure::Error) {
            // The save removed its own temporary files.
            CHECK_FALSE(hasTemporaryFile(filesBelow(root)));
        }
    } else {
        CHECK(contains(save.result.error().message, "after its commit"));
        CHECK(opened == save.after);
    }
    const bool fromTemporaryFiles =
        std::any_of(warnings.begin(), warnings.end(),
                    [](const std::string& warning) { return contains(warning, "interrupted"); });
    CHECK(fromTemporaryFiles == (opened == save.after && hasTemporaryFile(filesBefore) &&
                                 std::find(filesBefore.begin(), filesBefore.end(),
                                           "project.saving.json") != filesBefore.end()));
    // Opening wrote nothing.
    CHECK(filesBelow(root) == filesBefore);

    // The next save finishes what was left and removes every leftover.
    const engine::project::Project third = projectVersion(folder, root, 3);
    const auto saved = engine::project::saveProject(third);
    REQUIRE(saved.hasValue());
    CHECK(openTexts(root, nullptr) == textsOf(third));
    CHECK(filesBelow(root) == expectedFiles(3));
}

// The changing operations of a save from version 1 to 2 when nothing fails.
int operationsOfASave()
{
    const TempProject folder("lpe_project_save_operations");
    FaultyFiles counting(Failure::Error, -1);
    const Interrupted save = saveThrough(folder, counting);
    REQUIRE(save.result.hasValue());
    return counting.operations();
}

} // namespace

TEST_CASE("A save that fails at any operation leaves the previous or the new project",
          "[project][save]")
{
    const int operations = operationsOfASave();
    // Folders, four temporary files, the journal's file and its rename, four
    // renames, the journal's removal.
    REQUIRE(operations >= 14);
    for (const Failure failure : {Failure::Error, Failure::Crash}) {
        for (int failAt = 0; failAt < operations; ++failAt) {
            INFO(nameOf(failure) << " at operation " << failAt << " of " << operations);
            const TempProject folder("lpe_project_save_failure");
            FaultyFiles faulty(failure, failAt);
            const Interrupted save = saveThrough(folder, faulty);
            REQUIRE(faulty.injected());
            if (!save.result.hasValue()) {
                INFO(save.result.error().message);
            }
            checkWhole(folder, save, failure);
        }
    }
}

TEST_CASE("A save after an interrupted one finishes it, and fails safely itself", "[project][save]")
{
    // The first operation after the commit: a crash there leaves the journal
    // and every part still in its temporary file.
    const int operations = operationsOfASave();
    int afterCommit = -1;
    for (int failAt = 0; failAt < operations && afterCommit < 0; ++failAt) {
        const TempProject folder("lpe_project_save_commit");
        FaultyFiles faulty(Failure::Crash, failAt);
        const Interrupted save = saveThrough(folder, faulty);
        if (!save.result.hasValue() && contains(save.result.error().message, "after its commit")) {
            afterCommit = failAt;
        }
    }
    REQUIRE(afterCommit > 0);

    for (const Failure failure : {Failure::Error, Failure::Crash}) {
        for (int failAt = 0;; ++failAt) {
            INFO(nameOf(failure) << " at operation " << failAt << " of the next save");
            const TempProject folder("lpe_project_save_recovery");
            FaultyFiles crash(Failure::Crash, afterCommit);
            const Interrupted first = saveThrough(folder, crash);
            REQUIRE_FALSE(first.result.hasValue());
            const std::filesystem::path& root = folder.root();
            REQUIRE(openTexts(root, nullptr) == first.after);

            // The next save (version 3), failing at `failAt`.
            const engine::project::Project third = projectVersion(folder, root, 3);
            FaultyFiles faulty(failure, failAt);
            const auto saved = engine::project::detail::saveProject(faulty, third);
            if (!faulty.injected()) {
                // Past the last operation: the save went through.
                REQUIRE(saved.hasValue());
                CHECK(openTexts(root, nullptr) == textsOf(third));
                CHECK(filesBelow(root) == expectedFiles(3));
                CHECK(failAt > 10);
                break;
            }
            const ProjectTexts opened = openTexts(root, nullptr);
            CHECK((opened == first.after || opened == textsOf(third)));
            if (!saved.hasValue() && contains(saved.error().message, "nothing was written")) {
                // Failed while finishing the earlier save: still version 2.
                CHECK(opened == first.after);
            }
            // A clean save afterwards.
            const engine::project::Project fourth = projectVersion(folder, root, 4);
            REQUIRE(engine::project::saveProject(fourth).hasValue());
            CHECK(openTexts(root, nullptr) == textsOf(fourth));
            CHECK(filesBelow(root) == expectedFiles(4));
        }
    }
}

TEST_CASE("An interrupted save is opened as saved and finished by the next save", "[project][save]")
{
    const TempProject folder("lpe_project_save_interrupted");
    const std::filesystem::path& root = folder.root();
    // Stopped right after the commit: the journal and every temporary file.
    int afterCommit = 0;
    for (;; ++afterCommit) {
        const TempProject probe("lpe_project_save_interrupted_probe");
        FaultyFiles faulty(Failure::Crash, afterCommit);
        const Interrupted save = saveThrough(probe, faulty);
        REQUIRE_FALSE(save.result.hasValue());
        if (contains(save.result.error().message, "after its commit")) {
            break;
        }
    }
    FaultyFiles crash(Failure::Crash, afterCommit);
    const Interrupted save = saveThrough(folder, crash);
    const std::vector<std::string> files = filesBelow(root);
    CHECK(std::find(files.begin(), files.end(), "project.saving.json") != files.end());
    CHECK(std::count_if(files.begin(), files.end(),
                        [](const std::string& file) { return contains(file, ".saving-"); }) == 4);

    std::vector<std::string> warnings;
    CHECK(openTexts(root, &warnings) == save.after);
    REQUIRE(warnings.size() == 1U);
    CHECK(warnings.front() == "project.saving.json: a save was interrupted after its commit; the "
                              "project opens as that save, and the next save finishes it");
    // The parts on disk still hold the previous save; only open knows better.
    CHECK(lpe_test::fileText(root / "worlds" / "world.json") == save.before.world);

    const auto opened = engine::project::openProject(root, UuidGenerator{1});
    REQUIRE(opened.hasValue());
    const auto saved = engine::project::saveProject(opened.value().project);
    REQUIRE(saved.hasValue());
    CHECK(saved.value().warnings ==
          std::vector<std::string>{"finished an earlier save of the project that was interrupted"});
    CHECK(lpe_test::textsOnDisk(root) == save.after);
    CHECK(filesBelow(root) == expectedFiles(2));
}

TEST_CASE("Temporary files of a save that failed before its commit are ignored, then removed",
          "[project][save]")
{
    const TempProject folder("lpe_project_save_leftovers");
    const std::filesystem::path& root = folder.root();
    REQUIRE(engine::project::createProject(root, "P", UuidGenerator{1}).hasValue());
    const ProjectTexts saved = lpe_test::textsOnDisk(root);
    folder.write("assets.json.saving-0123abcd", "{ broken");
    folder.write("scenes/main.scene.json.saving-ffffffff", "");
    folder.write("project.saving.json.saving-00000000", "{");
    // Not a save's temporary file: kept.
    folder.write("scenes/notes.txt.saving-12345678", "mine");
    folder.write("scenes/main.scene.json.saving-1234", "mine too");

    std::vector<std::string> warnings;
    CHECK(openTexts(root, &warnings) == saved);
    CHECK(warnings.empty());

    const auto opened = engine::project::openProject(root, UuidGenerator{1});
    REQUIRE(opened.hasValue());
    const auto report = engine::project::saveProject(opened.value().project);
    REQUIRE(report.hasValue());
    CHECK(report.value().warnings ==
          std::vector<std::string>{
              "removed 3 leftover file(s) of an earlier save that failed before its commit"});
    std::vector<std::string> expected = lpe_test::kDefaultFiles;
    expected.insert(expected.end(),
                    {"scenes/main.scene.json.saving-1234", "scenes/notes.txt.saving-12345678"});
    std::sort(expected.begin(), expected.end());
    CHECK(filesBelow(root) == expected);
}
