#include "ProjectFiles.hpp"

#include "engine/core/FileSystem.hpp"

#include <format>
#include <fstream>
#include <ios>
#include <system_error>
#include <utility>

namespace engine::project::detail {

namespace {

// The generic message of an error code ("permission denied"): the same
// English text everywhere; the system's own may be in its code page.
std::string reason(const std::error_code& error)
{
    return error.default_error_condition().message();
}

class DiskFiles final : public ProjectFiles {
public:
    bool isFile(const std::filesystem::path& path) override
    {
        std::error_code error;
        return std::filesystem::is_regular_file(path, error);
    }

    bool isFolder(const std::filesystem::path& path) override
    {
        std::error_code error;
        return std::filesystem::is_directory(path, error);
    }

    core::Result<std::string> read(const std::filesystem::path& path,
                                   std::uintmax_t maxBytes) override
    {
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error)) {
            return core::Error{"the file is missing"};
        }
        const std::uintmax_t size = std::filesystem::file_size(path, error);
        if (error) {
            return core::Error{"cannot read its size: " + reason(error)};
        }
        if (size > maxBytes) {
            return core::Error{std::format("{} bytes, at most {} allowed", size, maxBytes)};
        }
        auto text = core::readTextFile(path);
        if (!text) {
            return core::Error{"cannot read it"};
        }
        return text;
    }

    std::optional<core::Error> write(const std::filesystem::path& path,
                                     std::string_view bytes) override
    {
        std::ofstream file(path, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!file) {
            return core::Error{"cannot create '" + core::toUtf8(path) + "'"};
        }
        file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        file.close();
        if (!file) {
            return core::Error{"error while writing '" + core::toUtf8(path) + "'"};
        }
        return std::nullopt;
    }

    std::optional<core::Error> rename(const std::filesystem::path& from,
                                      const std::filesystem::path& to) override
    {
        std::error_code error;
        std::filesystem::rename(from, to, error);
        if (error) {
            return core::Error{"cannot rename '" + core::toUtf8(from) + "' to '" +
                               core::toUtf8(to) + "': " + reason(error)};
        }
        return std::nullopt;
    }

    std::optional<core::Error> remove(const std::filesystem::path& path) override
    {
        std::error_code error;
        std::filesystem::remove(path, error);
        if (error) {
            return core::Error{"cannot remove '" + core::toUtf8(path) + "': " + reason(error)};
        }
        return std::nullopt;
    }

    std::optional<core::Error> createFolders(const std::filesystem::path& path) override
    {
        std::error_code error;
        std::filesystem::create_directories(path, error);
        if (error || !std::filesystem::is_directory(path, error)) {
            return core::Error{"cannot create the folder '" + core::toUtf8(path) + "'" +
                               (error ? ": " + reason(error) : std::string())};
        }
        return std::nullopt;
    }

    core::Result<std::vector<std::string>> fileNames(const std::filesystem::path& folder) override
    {
        std::vector<std::string> names;
        std::error_code error;
        if (!std::filesystem::is_directory(folder, error)) {
            return names;
        }
        // Without exceptions: increment() with an error code.
        for (std::filesystem::directory_iterator entry(folder, error), end; !error && entry != end;
             entry.increment(error)) {
            std::error_code ignored;
            if (entry->is_regular_file(ignored)) {
                names.push_back(core::toUtf8(entry->path().filename()));
            }
        }
        if (error) {
            return core::Error{"cannot list '" + core::toUtf8(folder) + "': " + reason(error)};
        }
        return names;
    }
};

} // namespace

ProjectFiles& diskFiles()
{
    static DiskFiles files;
    return files;
}

} // namespace engine::project::detail
