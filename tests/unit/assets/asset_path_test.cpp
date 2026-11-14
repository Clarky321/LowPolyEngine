#include "engine/assets/AssetPath.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <string_view>

using engine::assets::AssetPath;

namespace {

std::string normalized(std::string_view text)
{
    const auto path = AssetPath::parse(text);
    REQUIRE(path);
    return path.value().text();
}

std::string errorOf(std::string_view text)
{
    const auto path = AssetPath::parse(text);
    REQUIRE_FALSE(path);
    return path.error().message;
}

} // namespace

TEST_CASE("Asset paths are normalized", "[assets][paths]")
{
    CHECK(normalized("models/tree.obj") == "models/tree.obj");
    CHECK(normalized("tree.obj") == "tree.obj");
    CHECK(normalized("./models//tree.obj") == "models/tree.obj");
    CHECK(normalized("models/./trees/./oak.obj") == "models/trees/oak.obj");
    CHECK(normalized("models/../textures/a.png") == "textures/a.png");
    CHECK(normalized("a/b/../../c/d.png") == "c/d.png");
    CHECK(normalized("models/") == "models");
    // Letter case, spaces inside names and other printable ASCII are kept.
    CHECK(normalized("Models/Big Tree (v2)/Oak_1-a.OBJ") == "Models/Big Tree (v2)/Oak_1-a.OBJ");
    CHECK(normalized(" leading space.png") == " leading space.png");
    CHECK(normalized(".hidden/.gitkeep") == ".hidden/.gitkeep");
    // Device names only as a whole stem.
    CHECK(normalized("console.png") == "console.png");
    CHECK(normalized("com10.png") == "com10.png");
    CHECK(normalized("com0.png") == "com0.png");
    CHECK(normalized(std::string(256, 'a')) == std::string(256, 'a'));
}

TEST_CASE("Asset paths give their file name, extension and folded form", "[assets][paths]")
{
    const AssetPath path = AssetPath::parse("Models/Trees/Oak.Tree.OBJ").value();
    CHECK(path.fileName() == "Oak.Tree.OBJ");
    CHECK(path.extension() == ".OBJ");
    CHECK(path.folded() == "models/trees/oak.tree.obj");
    CHECK(AssetPath::parse("Makefile").value().extension().empty());
    CHECK(AssetPath::parse("a/.gitignore").value().extension().empty());
    CHECK(engine::assets::foldCase("AbC-1") == "abc-1");

    const std::filesystem::path root = std::filesystem::path("project") / "root";
    CHECK(path.resolve(root) == root / "Models" / "Trees" / "Oak.Tree.OBJ");

    // Equality is exact: the registry treats a difference in case as a conflict.
    CHECK(AssetPath::parse("a/b.png").value() == AssetPath::parse("./a//b.png").value());
    CHECK_FALSE(AssetPath::parse("A/b.png").value() == AssetPath::parse("a/b.png").value());
    CHECK(AssetPath::parse("a.png").value() < AssetPath::parse("b.png").value());
}

TEST_CASE("Asset paths reject empty, long and non-ASCII text", "[assets][paths]")
{
    CHECK(errorOf("") == "asset path: must not be empty");
    CHECK(errorOf(std::string(257, 'a')) == "asset path: 257 bytes, at most 256 allowed");
    CHECK(errorOf("models/\xC3") == "asset path: invalid UTF-8 at byte 7");
    CHECK(errorOf("models/caf\xC3\xA9.png") ==
          "asset path: character U+00E9 at byte 10 is not allowed; asset paths are printable "
          "ASCII");
    CHECK(errorOf("маяк.obj").starts_with("asset path: character U+043C at byte 0"));
    CHECK(errorOf("a\tb.png") == "asset path: control character U+0009 at byte 1");
    CHECK(errorOf(std::string("a\0b.png", 7)) == "asset path: control character U+0000 at byte 1");
    CHECK(errorOf("a\x7F.png") == "asset path: control character U+007F at byte 1");
}

TEST_CASE("Asset paths reject machine-specific forms", "[assets][paths]")
{
    CHECK(errorOf("C:/Users/me/tree.obj") ==
          "asset path 'C:/Users/me/tree.obj': a drive letter is not allowed; asset paths are "
          "relative to the project root");
    CHECK(errorOf("d:tree.obj")
              .ends_with("a drive letter is not allowed; asset paths are "
                         "relative to the project root"));
    CHECK(errorOf("/home/me/tree.obj") ==
          "asset path '/home/me/tree.obj': an absolute path is not allowed; asset paths are "
          "relative to the project root");
    CHECK(errorOf("/tmp/tree.obj").find("an absolute path") != std::string::npos);
    CHECK(errorOf("\\\\server\\share\\tree.obj").find("a UNC path is not allowed") !=
          std::string::npos);
    CHECK(errorOf("//server/share/tree.obj").find("a UNC path is not allowed") !=
          std::string::npos);
    CHECK(errorOf("~/tree.obj") ==
          "asset path '~/tree.obj': a leading '~' is not allowed: it names a home folder");
    CHECK(errorOf("~me/tree.obj").find("a leading '~'") != std::string::npos);
}

TEST_CASE("Asset paths reject characters and names that are not portable", "[assets][paths]")
{
    CHECK(errorOf("models\\tree.obj") ==
          "asset path 'models\\tree.obj': '\\' at byte 6: use '/' between folders");
    for (const char c : std::string_view("<>:\"|?*")) {
        // "ab" first: "a:" would be a drive letter.
        const std::string text = std::string("ab") + c + "c.png";
        CHECK(errorOf(text) == "asset path '" + text + "': '" + std::string(1, c) +
                                   "' at byte 2 is not allowed (Windows forbids < > : \" | ? * in "
                                   "file names)");
    }
    CHECK(errorOf("models./tree.obj") ==
          "asset path 'models./tree.obj': 'models.' ends with a dot or a space, which Windows "
          "drops from file names");
    CHECK(errorOf("models/tree.obj ").find("'tree.obj ' ends with a dot or a space") !=
          std::string::npos);
    CHECK(errorOf("models/.../a.png").find("'...' ends with a dot") != std::string::npos);
    for (const std::string_view device : {"CON", "prn.png", "Aux.obj", "nul", "COM1.png", "com9",
                                          "LPT1.txt", "lpt9.obj", "nul.tar.gz", "a/con/b.png"}) {
        CHECK(errorOf(device).find("is a device name on Windows") != std::string::npos);
    }
}

TEST_CASE("Asset paths stay inside the project root", "[assets][paths]")
{
    CHECK(errorOf("../tree.obj") == "asset path '../tree.obj': '..' leaves the project root");
    CHECK(errorOf("models/../../tree.obj").ends_with("'..' leaves the project root"));
    CHECK(errorOf("a/b/../../../c.png").ends_with("'..' leaves the project root"));
    CHECK(errorOf(".") == "asset path '.': names no file");
    CHECK(errorOf("./") == "asset path './': names no file");
    CHECK(errorOf("a/..") == "asset path 'a/..': names no file");
    CHECK(errorOf("/") == "asset path '/': an absolute path is not allowed; asset paths are "
                          "relative to the project root");
}
