// The project module (M5.4, plan decision 5, ADR 0028) depends on no editor,
// UI, window or renderer code, so a runtime application can open a project:
// its public headers and its sources include only the standard library, the
// engine's core, assets, scene, worldgen and project, and the engine's
// internal JSON support. Every file of engine/include/engine/project/ and
// engine/src/project/ is checked.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct Include {
    std::string path;
    bool angled = false;
};

// The included path of an #include line, if it is one.
bool readInclude(std::string_view line, Include& include)
{
    const std::size_t hash = line.find_first_not_of(" \t");
    if (hash == std::string_view::npos || line[hash] != '#') {
        return false;
    }
    std::string_view rest = line.substr(hash + 1);
    rest.remove_prefix(std::min(rest.find_first_not_of(" \t"), rest.size()));
    if (!rest.starts_with("include")) {
        return false;
    }
    const std::size_t open = rest.find_first_of("<\"");
    if (open == std::string_view::npos) {
        return false;
    }
    const std::size_t close = rest.find_first_of(">\"", open + 1);
    if (close == std::string_view::npos) {
        return false;
    }
    include.path = std::string(rest.substr(open + 1, close - open - 1));
    include.angled = rest[open] == '<';
    return true;
}

bool allowed(const Include& include)
{
    if (include.angled) {
        // The standard library only: no "SDL3/SDL.h", "glad/gl.h", "Raven.h",
        // "imgui.h", "entt/...", "lua.h".
        return include.path.find('/') == std::string::npos &&
               include.path.find('.') == std::string::npos;
    }
    constexpr std::array<std::string_view, 5> kEngineModules{
        "engine/core/", "engine/assets/", "engine/scene/", "engine/worldgen/", "engine/project/",
    };
    for (const std::string_view module : kEngineModules) {
        if (include.path.starts_with(module)) {
            return true;
        }
    }
    // The engine's internal JSON support, and the module's own internal headers.
    return include.path == "../core/JsonSupport.hpp" || include.path.find('/') == std::string::npos;
}

} // namespace

TEST_CASE("The project module uses no editor, UI, window or renderer code", "[project][headers]")
{
    CHECK(allowed({"engine/scene/Scene.hpp", false}));
    CHECK(allowed({"format", true}));
    CHECK_FALSE(allowed({"engine/render/Renderer.hpp", false}));
    CHECK_FALSE(allowed({"engine/ui/Ui.hpp", false}));
    CHECK_FALSE(allowed({"engine/platform/Window.hpp", false}));
    CHECK_FALSE(allowed({"editor/document/Document.hpp", false}));
    CHECK_FALSE(allowed({"SDL3/SDL.h", true}));
    CHECK_FALSE(allowed({"Raven.h", true}));
    CHECK_FALSE(allowed({"glad/gl.h", true}));

    const std::array<std::filesystem::path, 2> folders{
        std::filesystem::path(LPE_ENGINE_INCLUDE_DIRECTORY) / "engine" / "project",
        std::filesystem::path(LPE_ENGINE_SOURCE_DIRECTORY) / "project",
    };
    std::vector<std::string> offenders;
    std::size_t files = 0;
    for (const std::filesystem::path& folder : folders) {
        REQUIRE(std::filesystem::is_directory(folder));
        for (const auto& entry : std::filesystem::directory_iterator(folder)) {
            const auto extension = entry.path().extension();
            if (!entry.is_regular_file() || (extension != ".hpp" && extension != ".cpp")) {
                continue;
            }
            ++files;
            std::ifstream file(entry.path());
            REQUIRE(file);
            std::string line;
            Include include;
            while (std::getline(file, line)) {
                if (readInclude(line, include) && !allowed(include)) {
                    offenders.push_back(entry.path().filename().string() + ": " + include.path);
                }
            }
        }
    }
    // Two public headers, the sources and the internal headers (8 when this
    // test was written).
    CHECK(files >= 8U);
    CHECK(offenders == std::vector<std::string>{});
}
