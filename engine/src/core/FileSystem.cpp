#include "engine/core/FileSystem.hpp"

#include <fstream>
#include <iterator>

namespace engine::core {

Result<std::string> readTextFile(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file) {
        return Error{"cannot open file '" + toUtf8(path) + "'"};
    }

    std::string contents{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    if (file.bad()) {
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
