// The configuration the project builds Raven with (M5.1, ADR 0026), proven by
// its behaviour: no native window system, live reload and auto-loading off at
// run time, and the HTTP(S) image download compiled out (the one local change
// in third_party/raven/, see its README.md).

#include "RavenSession.hpp"

#include <catch2/catch_test_macros.hpp>

#include <Raven.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

using lpe_test::RavenErrors;
using lpe_test::RavenSession;

// A folder removed at scope exit.
class TempFolder {
public:
    explicit TempFolder(std::string_view name)
        : path_(std::filesystem::temp_directory_path() / std::filesystem::path(name))
    {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
        std::filesystem::create_directories(path_);
    }
    ~TempFolder()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }
    TempFolder(const TempFolder&) = delete;
    TempFolder& operator=(const TempFolder&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

void writeFile(const std::filesystem::path& path, std::string_view text)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << text;
}

std::string utf8Path(const std::filesystem::path& path)
{
    const std::u8string text = path.u8string();
    return {text.begin(), text.end()};
}

#if !defined(_WIN32)
// PATH with one folder in front of it, restored at scope exit.
class PathPrefix {
public:
    explicit PathPrefix(const std::filesystem::path& folder)
    {
        const char* value = std::getenv("PATH");
        hadPath_ = value != nullptr;
        oldPath_ = hadPath_ ? value : "";
        std::string path = folder.string();
        if (!oldPath_.empty()) {
            path += ':';
            path += oldPath_;
        }
        set_ = ::setenv("PATH", path.c_str(), 1) == 0;
    }
    ~PathPrefix()
    {
        if (hadPath_) {
            ::setenv("PATH", oldPath_.c_str(), 1);
        } else {
            ::unsetenv("PATH");
        }
    }
    PathPrefix(const PathPrefix&) = delete;
    PathPrefix& operator=(const PathPrefix&) = delete;

    [[nodiscard]] bool set() const { return set_; }

private:
    std::string oldPath_;
    bool hadPath_ = false;
    bool set_ = false;
};
#endif

} // namespace

TEST_CASE("Raven has no native window system: without a host it stays headless", "[ui][raven]")
{
    // No host platform registered and `headless` not asked for: a default Raven
    // build would open a Win32 or X11 window here. This one has only its null
    // and host platforms (RAVEN_PLATFORM=null), so it falls back to the null one.
    RavenErrors::instance().clear();
    Rerror_set_callback(&RavenErrors::record);
    Rhost_platform_register(nullptr);
    RInitDesc desc = lpe_test::headlessInitDesc();
    desc.headless = R_FALSE;
    REQUIRE(Rinit_ex(&desc) == R_TRUE);
    CHECK(Rhost_platform_is_active() == R_FALSE);

    const Rid window = Rwindow_create("no OS window", 200, 100, 0);
    REQUIRE(window != R_INVALID_ID);
    Rwindow_show(window);
    Rdraw_frame();
    RNativeWindow native{};
    CHECK(Rwindow_get_native_handle(window, &native) == R_FALSE);
    CHECK(native.system == R_WINDOW_SYSTEM_NONE);

    Rshutdown();
    INFO(RavenErrors::instance().describe());
    CHECK(RavenErrors::instance().count() == 0);
    CHECK(lpe_test::ravenAllocationCount() == 0);
}

TEST_CASE("Live reload is off: a layout file changed on disk is not reloaded", "[ui][raven]")
{
    const TempFolder folder("lpe_raven_live_reload");
    const auto layout = folder.path() / "layout.xml";
    writeFile(layout,
              R"(<Window Width="200" Height="100"><Label Name="status" Text="before"/></Window>)");

    {
        RavenSession session; // turns live reload off right after Rinit
        REQUIRE(session.started());
        CHECK(Rxml_get_live_reload() == R_FALSE);

        const std::string path = utf8Path(layout);
        const Rid window = Rwindow_create_from_xml_file(path.c_str());
        REQUIRE(window != R_INVALID_ID);
        Rwindow_show(window);
        Rhost_window_set_size(window, 200, 100, 1.0f);
        Rdraw_frame();
        const Rid status = Rwidget_find(window, "status");
        REQUIRE(status != R_INVALID_ID);
        CHECK(std::string_view{Rlabel_get_text(status)} == "before");

        // The file changes (another length, so its stamp differs whatever the
        // clock's resolution). Frames go on; the interface does not follow.
        writeFile(
            layout,
            R"(<Window Width="200" Height="100"><Label Name="status" Text="after, changed on disk"/></Window>)");
        for (int frame = 0; frame < 10; ++frame) {
            Rdraw_frame();
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        CHECK(std::string_view{Rlabel_get_text(status)} == "before");

        // What stays (ADR 0026): an explicit Rxml_check_files still reloads, and
        // the widget keeps its handle.
        CHECK(Rxml_check_files() >= 1);
        CHECK(std::string_view{Rlabel_get_text(status)} == "after, changed on disk");
        INFO(RavenErrors::instance().describe());
        CHECK(RavenErrors::instance().count() == 0);
    }
    CHECK(lpe_test::ravenAllocationCount() == 0);
}

TEST_CASE("Auto-loading is off: declarations in the search places are not taken", "[ui][raven]")
{
    // A declaration of the tag <LpeProbe> in the base path, in a file named after
    // the tag: auto-loading would take it both ways (a <Widgets> file in a search
    // place, and <Tag>.xml for an unknown tag).
    const TempFolder folder("lpe_raven_auto_load");
    const auto declarations = folder.path() / "LpeProbe.xml";
    writeFile(
        declarations,
        R"(<Widgets><Widget Name="LpeProbe"><Label Name="probe" Text="declared"/></Widget></Widgets>)");
    constexpr std::string_view document = "<Window><VBox><LpeProbe/></VBox></Window>";

    {
        RavenSession session; // turns auto-loading off right after Rinit
        REQUIRE(session.started());
        CHECK(Rxml_get_auto_load() == R_FALSE);
        const std::string base = utf8Path(folder.path());
        Rxml_set_base_path(base.c_str());

        RavenErrors& errors = RavenErrors::instance();
        CHECK(Rwindow_create_from_xml(document.data(), document.size()) == R_INVALID_ID);
        CHECK(errors.contains(R_ERROR_XML, "unknown widget <LpeProbe>"));
        CHECK(Rxml_is_widget_registered("LpeProbe") == R_FALSE);

        // Registered explicitly, the same file is used.
        errors.clear();
        const std::string path = utf8Path(declarations);
        CHECK(Rxml_register_widgets_from_file(path.c_str()) == 1);
        CHECK(Rxml_is_widget_registered("LpeProbe") == R_TRUE);
        const Rid window = lpe_test::createHostWindow(document, 200, 100);
        REQUIRE(window != R_INVALID_ID);
        const Rid probe = Rwidget_find(window, "probe");
        REQUIRE(probe != R_INVALID_ID);
        CHECK(std::string_view{Rlabel_get_text(probe)} == "declared");
        INFO(errors.describe());
        CHECK(errors.count() == 0);
        Rxml_set_base_path(nullptr);
    }
    CHECK(lpe_test::ravenAllocationCount() == 0);
}

TEST_CASE("HTTP(S) image sources are compiled out: no request, no process", "[ui][raven]")
{
#if !defined(_WIN32)
    // Upstream Raven downloads with a `curl` child process found through PATH.
    // A `curl` of the test's own comes first in PATH and leaves a mark if it runs.
    const TempFolder folder("lpe_raven_http");
    const auto marker = folder.path() / "curl-was-run";
    const auto fakeCurl = folder.path() / "curl";
    writeFile(fakeCurl, "#!/bin/sh\necho run > \"" + marker.string() + "\"\nexit 1\n");
    std::filesystem::permissions(fakeCurl, std::filesystem::perms::owner_all);
    const PathPrefix path(folder.path());
    REQUIRE(path.set());
#else
    // Upstream Raven downloads with WinHTTP, which makes the program import winhttp.dll.
    CHECK(GetModuleHandleW(L"winhttp.dll") == nullptr);
#endif

    {
        RavenSession session;
        REQUIRE(session.started());
        constexpr std::string_view url = "https://lpe-raven-http.invalid/image.png";
        const std::string document = std::string("<Window><VBox><Image Name=\"remote\" Source=\"") +
                                     std::string(url) + "\"/></VBox></Window>";
        const Rid window = lpe_test::createHostWindow(document, 200, 100);
        REQUIRE(window != R_INVALID_ID);
        Rdraw_frame(); // the image's job reports its result through a posted task
        CHECK(Rwidget_find(window, "remote") != R_INVALID_ID);
        // The source fails like an unreadable file.
        INFO(RavenErrors::instance().describe());
        CHECK(RavenErrors::instance().contains(R_ERROR_INVALID_FILE_FORMAT,
                                               "cannot decode " + std::string(url)));
    }
    CHECK(lpe_test::ravenAllocationCount() == 0);

#if !defined(_WIN32)
    CHECK_FALSE(std::filesystem::exists(marker));
#else
    CHECK(GetModuleHandleW(L"winhttp.dll") == nullptr);
#endif
}
