#pragma once

// A project folder for asset tests: created empty under the system's temporary
// folder and removed again at scope exit.

#include "engine/assets/AssetId.hpp"
#include "engine/core/Uuid.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>

namespace lpe_test {

class TempProject {
public:
    explicit TempProject(std::string_view name)
        : root_(std::filesystem::temp_directory_path() / std::filesystem::path(name))
    {
        std::error_code ignored;
        std::filesystem::remove_all(root_, ignored);
        std::filesystem::create_directories(root_);
    }
    ~TempProject()
    {
        std::error_code ignored;
        std::filesystem::remove_all(root_, ignored);
    }
    TempProject(const TempProject&) = delete;
    TempProject& operator=(const TempProject&) = delete;

    [[nodiscard]] const std::filesystem::path& root() const { return root_; }

    // Writes a file at a '/'-separated path below the root, creating folders.
    void write(std::string_view relative, std::string_view contents = "data") const
    {
        const std::filesystem::path file = root_ / std::filesystem::path(relative);
        std::filesystem::create_directories(file.parent_path());
        std::ofstream stream(file, std::ios::binary);
        REQUIRE(stream);
        stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    }

    void copy(const std::filesystem::path& from, std::string_view relative) const
    {
        const std::filesystem::path file = root_ / std::filesystem::path(relative);
        std::filesystem::create_directories(file.parent_path());
        std::filesystem::copy_file(from, file, std::filesystem::copy_options::overwrite_existing);
    }

    void remove(std::string_view relative) const
    {
        std::filesystem::remove(root_ / std::filesystem::path(relative));
    }

private:
    std::filesystem::path root_;
};

// A fixed asset ID in the version 4 layout; different numbers give different IDs.
inline engine::assets::AssetId assetId(std::uint64_t number)
{
    return engine::assets::AssetId{engine::core::Uuid{0xA55E7000A55E4000ULL | (number << 16U),
                                                      0x8000000000000000ULL | number}};
}

} // namespace lpe_test
