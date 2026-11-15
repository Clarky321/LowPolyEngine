#include "engine/core/FileSystem.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

constexpr auto kAllWrite = std::filesystem::perms::owner_write |
                           std::filesystem::perms::group_write |
                           std::filesystem::perms::others_write;

// A folder removed at scope exit, with write permission restored first.
class TempFolder {
public:
    explicit TempFolder(std::string_view name)
        : path_(std::filesystem::temp_directory_path() / std::filesystem::path(name))
    {
        cleanUp();
        std::filesystem::create_directories(path_);
    }
    ~TempFolder() { cleanUp(); }
    TempFolder(const TempFolder&) = delete;
    TempFolder& operator=(const TempFolder&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

private:
    void cleanUp() const
    {
        std::error_code ignored;
        constexpr auto kWrite = std::filesystem::perms::owner_write;
        if (std::filesystem::exists(path_, ignored)) {
            std::filesystem::permissions(path_, kWrite, std::filesystem::perm_options::add,
                                         ignored);
            for (const auto& entry : std::filesystem::directory_iterator(path_, ignored)) {
                std::filesystem::permissions(entry.path(), kWrite,
                                             std::filesystem::perm_options::add, ignored);
            }
        }
        std::filesystem::remove_all(path_, ignored);
    }

    std::filesystem::path path_;
};

std::span<const std::uint8_t> bytesOf(std::string_view text)
{
    return {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()};
}

std::string contents(const std::filesystem::path& file)
{
    auto text = engine::core::readTextFile(file);
    REQUIRE(text);
    return text.value();
}

std::vector<std::string> fileNames(const std::filesystem::path& folder)
{
    std::vector<std::string> names;
    for (const auto& entry : std::filesystem::directory_iterator(folder)) {
        names.push_back(engine::core::toUtf8(entry.path().filename()));
    }
    return names;
}

std::string messageOf(const std::optional<engine::core::Error>& error)
{
    REQUIRE(error.has_value());
    return error->message;
}

} // namespace

TEST_CASE("An atomic write creates and replaces a file", "[core][filesystem]")
{
    const TempFolder folder("lpe_atomic_write");
    const std::filesystem::path file = folder.path() / "scene.json";
    REQUIRE_FALSE(engine::core::writeFileAtomically(file, bytesOf("first")).has_value());
    CHECK(contents(file) == "first");
    REQUIRE_FALSE(engine::core::writeFileAtomically(file, bytesOf("second, longer")).has_value());
    CHECK(contents(file) == "second, longer");
    REQUIRE_FALSE(engine::core::writeFileAtomically(file, bytesOf("")).has_value());
    CHECK(contents(file).empty());
    // No temporary file stays behind.
    CHECK(fileNames(folder.path()) == std::vector<std::string>{"scene.json"});
}

TEST_CASE("An atomic write needs an existing folder and a file path", "[core][filesystem]")
{
    const TempFolder folder("lpe_atomic_write_paths");
    const std::filesystem::path missing = folder.path() / "missing" / "scene.json";
    CHECK(messageOf(engine::core::writeFileAtomically(missing, bytesOf("x"))) ==
          "cannot write '" + engine::core::toUtf8(missing) + "': the folder '" +
              engine::core::toUtf8(missing.parent_path()) + "' does not exist");
    CHECK_FALSE(std::filesystem::exists(missing.parent_path()));

    CHECK(messageOf(engine::core::writeFileAtomically(folder.path(), bytesOf("x")))
              .ends_with("': it is a folder"));
    CHECK(messageOf(engine::core::writeFileAtomically({}, bytesOf("x"))) ==
          "cannot write '': not a file path");

    // A folder path ending in a separator has no file name.
    CHECK(messageOf(engine::core::writeFileAtomically(folder.path() / "", bytesOf("x")))
              .ends_with("': not a file path"));
}

TEST_CASE("A failed atomic write leaves the old file as it was", "[core][filesystem]")
{
    const TempFolder folder("lpe_atomic_write_read_only");
    const std::filesystem::path file = folder.path() / "scene.json";
    REQUIRE_FALSE(engine::core::writeFileAtomically(file, bytesOf("old")).has_value());

    // Read-only file (Windows refuses to replace it) in a read-only folder
    // (Linux refuses to create the temporary file there).
    std::filesystem::permissions(file, kAllWrite, std::filesystem::perm_options::remove);
    std::filesystem::permissions(folder.path(), kAllWrite, std::filesystem::perm_options::remove);
    const auto error = engine::core::writeFileAtomically(file, bytesOf("new"));
    std::filesystem::permissions(folder.path(), std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::add);
    std::filesystem::permissions(file, std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::add);

    REQUIRE(error.has_value());
    CHECK(error->message.starts_with("cannot "));
    CHECK(contents(file) == "old");
    CHECK(fileNames(folder.path()) == std::vector<std::string>{"scene.json"});
}
