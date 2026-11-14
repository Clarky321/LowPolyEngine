// EnTT, nlohmann_json and Lua are private dependencies of the engine (ADR
// 0021, ADR 0004, ADR 0024): no public header may include them, so their types cannot leak
// into public signatures. Every header under engine/include is checked.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

// The included path of an #include line, or empty.
std::string includedPath(std::string_view line)
{
    const std::size_t hash = line.find_first_not_of(" \t");
    if (hash == std::string_view::npos || line[hash] != '#') {
        return {};
    }
    std::string_view rest = line.substr(hash + 1);
    rest.remove_prefix(std::min(rest.find_first_not_of(" \t"), rest.size()));
    if (!rest.starts_with("include")) {
        return {};
    }
    const std::size_t open = rest.find_first_of("<\"");
    if (open == std::string_view::npos) {
        return {};
    }
    const std::size_t close = rest.find_first_of(">\"", open + 1);
    if (close == std::string_view::npos) {
        return {};
    }
    return std::string(rest.substr(open + 1, close - open - 1));
}

bool isPrivateDependency(std::string_view path)
{
    return path.starts_with("entt/") || path == "entt.hpp" || path.starts_with("nlohmann/") ||
           path == "lua.h" || path == "lauxlib.h" || path == "lualib.h" || path == "luaconf.h" ||
           path == "lua.hpp";
}

} // namespace

TEST_CASE("The include check recognizes include lines", "[scene][headers]")
{
    CHECK(includedPath("#include <entt/entity/registry.hpp>") == "entt/entity/registry.hpp");
    CHECK(includedPath("  #  include \"nlohmann/json.hpp\"") == "nlohmann/json.hpp");
    CHECK(includedPath("// #include <entt/entt.hpp>").empty());
    CHECK(includedPath("#pragma once").empty());
    CHECK(isPrivateDependency("entt/entt.hpp"));
    CHECK(isPrivateDependency("nlohmann/json_fwd.hpp"));
    CHECK_FALSE(isPrivateDependency("engine/scene/Scene.hpp"));
    CHECK(isPrivateDependency("lua.h"));
    CHECK(isPrivateDependency("lauxlib.h"));
    CHECK_FALSE(isPrivateDependency("glm/vec3.hpp"));
}

TEST_CASE("No public engine header includes EnTT, nlohmann_json or Lua", "[scene][headers]")
{
    const std::filesystem::path root{LPE_ENGINE_INCLUDE_DIRECTORY};
    REQUIRE(std::filesystem::is_directory(root / "engine" / "scene"));

    std::vector<std::string> offenders;
    std::size_t headers = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".hpp") {
            continue;
        }
        ++headers;
        std::ifstream file(entry.path());
        REQUIRE(file);
        std::string line;
        while (std::getline(file, line)) {
            const std::string path = includedPath(line);
            if (isPrivateDependency(path)) {
                offenders.push_back(std::filesystem::relative(entry.path(), root).generic_string() +
                                    ": " + path);
            }
        }
    }
    // The walk found the headers (41 when this test was written).
    CHECK(headers > 30);
    CHECK(offenders == std::vector<std::string>{});
}
