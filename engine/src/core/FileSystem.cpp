#include "engine/core/FileSystem.hpp"

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

std::string toUtf8(const std::filesystem::path& path)
{
    const std::u8string text = path.generic_u8string();
    // char8_t and char have the same representation; this only changes the type.
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

} // namespace engine::core
