// The editor's document, commands and selection (M5.5, ADR 0029) are UI-free:
// their headers and sources include only the standard library, the engine's
// core, assets, scene, worldgen and project, and each other. No Raven, SDL,
// OpenGL, Dear ImGui, or the engine's UI, window or renderer code.

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
        // The standard library only.
        return include.path.find('/') == std::string::npos &&
               include.path.find('.') == std::string::npos;
    }
    constexpr std::array<std::string_view, 8> kAllowed{
        "engine/core/",    "engine/assets/",   "engine/scene/",    "engine/worldgen/",
        "engine/project/", "editor/document/", "editor/commands/", "editor/selection/",
    };
    return std::any_of(kAllowed.begin(), kAllowed.end(),
                       [&](std::string_view prefix) { return include.path.starts_with(prefix); });
}

} // namespace

TEST_CASE("The editor's document and commands are free of UI code", "[editor][headers]")
{
    CHECK(allowed({"editor/document/Command.hpp", false}));
    CHECK(allowed({"vector", true}));
    CHECK_FALSE(allowed({"Raven.h", true}));
    CHECK_FALSE(allowed({"SDL3/SDL.h", true}));
    CHECK_FALSE(allowed({"imgui.h", true}));
    CHECK_FALSE(allowed({"engine/render/Renderer.hpp", false}));
    CHECK_FALSE(allowed({"engine/ui/Ui.hpp", false}));
    CHECK_FALSE(allowed({"viewport/WorldViewport.hpp", false}));

    const std::filesystem::path root{LPE_EDITOR_SOURCE_DIRECTORY};
    const std::array<std::filesystem::path, 6> folders{
        root / "include" / "editor" / "document",
        root / "include" / "editor" / "commands",
        root / "include" / "editor" / "selection",
        root / "src" / "document",
        root / "src" / "commands",
        root / "src" / "selection",
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
    // 12 when this test was written.
    CHECK(files >= 12U);
    CHECK(offenders == std::vector<std::string>{});
}
