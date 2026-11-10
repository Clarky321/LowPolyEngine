#include "engine/core/FileSystem.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

namespace {

// Creates a file in the system temp folder and removes it at scope exit.
class TempFile {
public:
    TempFile(const std::string& name, const std::string& contents)
        : path_(std::filesystem::temp_directory_path() / name)
    {
        std::ofstream file(path_, std::ios::out | std::ios::binary | std::ios::trunc);
        file << contents;
    }
    ~TempFile()
    {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }
    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

} // namespace

TEST_CASE("readTextFile returns the exact file bytes", "[core][filesystem]")
{
    // \r\n must survive: shader sources and data files are read verbatim.
    const TempFile file("lpe_filesystem_test_bytes.txt", "line one\r\nline two\n");
    const auto contents = engine::core::readTextFile(file.path());
    REQUIRE(contents.hasValue());
    CHECK(contents.value() == "line one\r\nline two\n");
}

TEST_CASE("readTextFile reads an empty file as an empty string", "[core][filesystem]")
{
    const TempFile file("lpe_filesystem_test_empty.txt", "");
    const auto contents = engine::core::readTextFile(file.path());
    REQUIRE(contents.hasValue());
    CHECK(contents.value().empty());
}

TEST_CASE("readTextFile reports a missing file with its path", "[core][filesystem]")
{
    const auto missing = std::filesystem::temp_directory_path() / "lpe_does_not_exist.txt";
    const auto contents = engine::core::readTextFile(missing);
    REQUIRE_FALSE(contents.hasValue());
    CHECK(contents.error().message.find("lpe_does_not_exist.txt") != std::string::npos);
}

TEST_CASE("readTextFile reports a directory as an error", "[core][filesystem]")
{
    // On Linux an ifstream opens directories; this once led to a huge
    // allocation and std::bad_alloc instead of an error.
    const auto contents = engine::core::readTextFile(std::filesystem::temp_directory_path());
    REQUIRE_FALSE(contents.hasValue());
    CHECK(contents.error().message.find("not a regular file") != std::string::npos);
}

TEST_CASE("toUtf8 keeps non-ASCII characters", "[core][filesystem]")
{
    // Expected bytes are spelled out so the test does not depend on how the
    // compiler decodes this source file.
    const std::filesystem::path path(u8"assets/острова/island.json");
    const std::string expected = "assets/"
                                 "\xD0\xBE\xD1\x81\xD1\x82\xD1\x80\xD0\xBE\xD0\xB2\xD0\xB0"
                                 "/island.json";
    CHECK(engine::core::toUtf8(path) == expected);
}
