#include "engine/core/FileSystem.hpp"

#include "engine/core/Uuid.hpp"

#include <cstddef>
#include <exception>
#include <fstream>
#include <ios>
#include <system_error>

namespace engine::core {

Result<std::string> readTextFile(const std::filesystem::path& path)
{
    // Check the file type first: on Linux an ifstream can open a directory,
    // and seeking in it reports a nonsensical size.
    std::error_code error;
    const auto status = std::filesystem::status(path, error);
    if (error || !std::filesystem::exists(status)) {
        return Error{"cannot open file '" + toUtf8(path) + "'"};
    }
    if (!std::filesystem::is_regular_file(status)) {
        return Error{"'" + toUtf8(path) + "' is not a regular file"};
    }
    const auto size = std::filesystem::file_size(path, error);
    if (error) {
        return Error{"cannot determine the size of '" + toUtf8(path) + "'"};
    }

    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file) {
        return Error{"cannot open file '" + toUtf8(path) + "'"};
    }

    // One read() call; istreambuf_iterator would also trigger a GCC 13
    // -Wnull-dereference false positive at -O3.
    std::string contents;
    try {
        contents.resize(static_cast<std::size_t>(size));
    } catch (const std::exception&) {
        // bad_alloc/length_error must not escape: callers expect a Result.
        return Error{"file too large to load: '" + toUtf8(path) + "'"};
    }
    if (!file.read(contents.data(), static_cast<std::streamsize>(size))) {
        return Error{"error while reading file '" + toUtf8(path) + "'"};
    }
    return contents;
}

std::optional<Error> writeBinaryFile(const std::filesystem::path& path,
                                     std::span<const std::uint8_t> bytes)
{
    std::error_code error;
    if (const auto parent = path.parent_path(); !parent.empty()) {
        std::filesystem::create_directories(parent, error);
        if (error) {
            return Error{"cannot create folder '" + toUtf8(parent) + "': " + error.message()};
        }
    }

    std::ofstream file(path, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!file) {
        return Error{"cannot open file '" + toUtf8(path) + "' for writing"};
    }
    // char and std::uint8_t have the same size and representation.
    file.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    file.close();
    if (!file) {
        return Error{"error while writing file '" + toUtf8(path) + "'"};
    }
    return std::nullopt;
}

std::optional<Error> writeFileAtomically(const std::filesystem::path& path,
                                         std::span<const std::uint8_t> bytes)
{
    const std::string name = toUtf8(path);
    if (path.empty() || !path.has_filename()) {
        return Error{"cannot write '" + name + "': not a file path"};
    }
    std::error_code error;
    const std::filesystem::path folder =
        path.has_parent_path() ? path.parent_path() : std::filesystem::path(".");
    if (!std::filesystem::is_directory(folder, error)) {
        return Error{"cannot write '" + name + "': the folder '" + toUtf8(folder) +
                     "' does not exist"};
    }
    if (std::filesystem::is_directory(path, error)) {
        return Error{"cannot write '" + name + "': it is a folder"};
    }

    // In the same folder, so that the rename stays on one file system. The
    // random part keeps two writers apart.
    std::filesystem::path temporary = path;
    temporary += ".tmp-" + toString(UuidGenerator::fromEntropy().next()).substr(0, 8);
    {
        std::ofstream file(temporary, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!file) {
            return Error{"cannot write '" + name + "': cannot create the temporary file '" +
                         toUtf8(temporary) + "'"};
        }
        // char and std::uint8_t have the same size and representation.
        file.write(reinterpret_cast<const char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
        file.close();
        if (!file) {
            std::filesystem::remove(temporary, error);
            return Error{"cannot write '" + name + "': error while writing the temporary file"};
        }
    }
    std::filesystem::rename(temporary, path, error);
    if (error) {
        // The generic message ("permission denied") is the same English text
        // everywhere; the system one may be in the system's code page.
        const std::string reason = error.default_error_condition().message();
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return Error{"cannot replace '" + name + "': " + reason};
    }
    return std::nullopt;
}

std::string toUtf8(const std::filesystem::path& path)
{
    const std::u8string text = path.generic_u8string();
    // char8_t and char have the same representation; this only changes the type.
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

} // namespace engine::core
