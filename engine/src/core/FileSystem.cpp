#include "engine/core/FileSystem.hpp"

#include <cstddef>
#include <fstream>
#include <ios>

namespace engine::core {

Result<std::string> readTextFile(const std::filesystem::path& path)
{
    // Opened at the end (ios::ate), so tellg() returns the file size. Reading
    // with one read() call is simpler than istreambuf_iterator, whose inlined
    // code also triggers a GCC 13 -Wnull-dereference false positive at -O3.
    std::ifstream file(path, std::ios::in | std::ios::binary | std::ios::ate);
    if (!file) {
        return Error{"cannot open file '" + toUtf8(path) + "'"};
    }

    const std::streamoff size = file.tellg();
    if (size < 0) {
        // E.g. a directory or a stream without a size.
        return Error{"cannot determine the size of '" + toUtf8(path) + "'"};
    }

    std::string contents(static_cast<std::size_t>(size), '\0');
    file.seekg(0, std::ios::beg);
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
