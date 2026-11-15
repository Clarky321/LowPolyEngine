// Raven built from third_party/raven/ runs headless (M5.1, ADR 0026): the null
// backend and the host platform, a window from XML text, input injected through
// the host platform, failed starts, repeated starts, and no allocation left
// after Rshutdown.

#include "RavenSession.hpp"

#include "engine/core/FileSystem.hpp"

#include <catch2/catch_test_macros.hpp>

#include <Raven.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace {

using lpe_test::RavenErrors;
using lpe_test::RavenSession;

// The release and commit ADR 0026 records; the build's pin (cmake/RavenDependency.cmake)
// must name the same, so an update cannot change one without the other.
constexpr std::string_view kReviewedVersion = "0.2.3";
constexpr std::string_view kReviewedCommit = "7ab270df3b618de3d11af6033912374b6cd621b7";

// A small form: a text field, a button and a label the button writes to.
constexpr std::string_view kFormXml =
    R"(<Window Title="LowPolyEngine headless test" Width="320" Height="200">
  <VBox Spacing="4">
    <TextInput Name="name" Placeholder="Name"/>
    <Button Name="apply" Text="Apply"/>
    <Label Name="status" Text="idle"/>
  </VBox>
</Window>)";

struct FormState {
    Rid status = R_INVALID_ID;
    int changes = 0;
    std::string lastChange;
    int clicks = 0;
    Rid clickedButton = R_INVALID_ID;
};

void onNameChanged(Rid /*input*/, const char* text, void* userData)
{
    auto& state = *static_cast<FormState*>(userData);
    ++state.changes;
    state.lastChange = text != nullptr ? text : "";
}

void onApplyClicked(Rid button, void* userData)
{
    auto& state = *static_cast<FormState*>(userData);
    ++state.clicks;
    state.clickedButton = button;
    const std::string status = "applied: " + state.lastChange;
    Rlabel_set_text(state.status, status.c_str());
}

} // namespace

TEST_CASE("Raven is the pinned upstream release", "[ui][raven]")
{
    // Raven's own version: Rget_version comes from its CMakeLists.txt, the
    // macros from raven_core.h. Neither needs Rinit.
    CHECK(std::string_view{Rget_version()} == kReviewedVersion);
    const std::string headerVersion = std::to_string(RAVEN_VERSION_MAJOR) + "." +
                                      std::to_string(RAVEN_VERSION_MINOR) + "." +
                                      std::to_string(RAVEN_VERSION_PATCH);
    CHECK(headerVersion == kReviewedVersion);

    // The build's pin and the vendored copy's README name the same release and commit.
    CHECK(std::string_view{LPE_RAVEN_VERSION} == kReviewedVersion);
    CHECK(std::string_view{LPE_RAVEN_COMMIT} == kReviewedCommit);
    // (engine::core::readTextFile: GCC 13 at -O3 reports a false null dereference
    // inside std::istreambuf_iterator, see the CI log of M5.1.)
    const auto readmeFile =
        engine::core::readTextFile(std::filesystem::path(LPE_RAVEN_SOURCE_DIRECTORY) / "README.md");
    REQUIRE(readmeFile);
    const std::string& readme = readmeFile.value();
    CHECK(readme.find(std::string(kReviewedCommit)) != std::string::npos);
    CHECK(readme.find("Raven " + std::string(kReviewedVersion)) != std::string::npos);
}

TEST_CASE("Raven runs headless: an XML window takes a click and typed text", "[ui][raven]")
{
    {
        RavenSession session;
        REQUIRE(session.started());
        CHECK(Ris_initialized() == R_TRUE);
        CHECK(Rhost_platform_is_active() == R_TRUE);

        const Rid window = lpe_test::createHostWindow(kFormXml, 320, 200);
        REQUIRE(window != R_INVALID_ID);
        CHECK(Rwindow_is_valid(window) == R_TRUE);
        int width = 0;
        int height = 0;
        Rwindow_get_framebuffer_size(window, &width, &height);
        CHECK(width == 320);
        CHECK(height == 200);

        const Rid input = Rwidget_find(window, "name");
        const Rid apply = Rwidget_find(window, "apply");
        FormState state;
        state.status = Rwidget_find(window, "status");
        REQUIRE(input != R_INVALID_ID);
        REQUIRE(apply != R_INVALID_ID);
        REQUIRE(state.status != R_INVALID_ID);
        CHECK(Rwidget_get_type(input) == R_WIDGET_TEXT_INPUT);
        CHECK(std::string_view{Rlabel_get_text(state.status)} == "idle");
        Rtext_input_set_changed_callback(input, &onNameChanged, &state);
        Rbutton_set_clicked_callback(apply, &onApplyClicked, &state);

        // A click gives the text field the keyboard; the host is asked for its cursor.
        CHECK(Rwidget_is_focused(input) == R_FALSE);
        lpe_test::clickWidget(window, input);
        Rdraw_frame();
        CHECK(Rwidget_is_focused(input) == R_TRUE);
        CHECK(Rwindow_wants_text_input(window) == R_TRUE);
        CHECK(session.host().cursorRequests > 0);

        // Typed code points arrive as UTF-8 text: ASCII and Cyrillic (U+041C U+0438 U+0440).
        for (const char letter : std::string_view{"Raven "}) {
            Rhost_inject_text(window, static_cast<std::uint32_t>(letter));
        }
        Rhost_inject_text(window, 0x041Cu);
        Rhost_inject_text(window, 0x0438u);
        Rhost_inject_text(window, 0x0440u);
        const std::string typed = "Raven \xD0\x9C\xD0\xB8\xD1\x80";
        CHECK(std::string_view{Rtext_input_get_text(input)} == typed);
        CHECK(state.changes == 9);
        CHECK(state.lastChange == typed);

        // An editing key: Backspace removes one code point (two UTF-8 bytes here).
        lpe_test::pressKey(window, R_KEY_BACKSPACE);
        const std::string edited = "Raven \xD0\x9C\xD0\xB8";
        CHECK(std::string_view{Rtext_input_get_text(input)} == edited);
        CHECK(state.lastChange == edited);

        // A click on the button runs its callback once, which changes the label.
        lpe_test::clickWidget(window, apply);
        Rdraw_frame();
        CHECK(state.clicks == 1);
        CHECK(state.clickedButton == apply);
        CHECK(std::string_view{Rlabel_get_text(state.status)} == "applied: " + edited);

        INFO(RavenErrors::instance().describe());
        CHECK(RavenErrors::instance().count() == 0);
        session.stop();
        CHECK(Ris_initialized() == R_FALSE);
    }
    // Nothing of Raven is left: every block returned, the heap intact.
    CHECK(lpe_test::ravenAllocationCount() == 0);
    CHECK(Rmemory_check() == R_TRUE);
}

TEST_CASE("Raven refuses a start it cannot do and stays clean", "[ui][raven]")
{
    RavenErrors& errors = RavenErrors::instance();
    errors.clear();
    Rerror_set_callback(&RavenErrors::record);
    REQUIRE(Ris_initialized() == R_FALSE);

    SECTION("calls before Rinit are refused")
    {
        constexpr std::string_view xml = "<Window/>";
        CHECK(Rwindow_create_from_xml(xml.data(), xml.size()) == R_INVALID_ID);
        CHECK(errors.contains(R_ERROR_INVALID_OPERATION, "Raven is not initialized"));
    }

    SECTION("backends this build does not have")
    {
        struct Case {
            RBackend backend;
            RError error;
            std::string_view detail;
        };
        // 0 selects Raven's default, Vulkan; 7 is no backend at all.
        const Case cases[] = {
            {R_BACKEND_VULKAN, R_ERROR_BACKEND, "built without the Vulkan backend"},
            {static_cast<RBackend>(0), R_ERROR_BACKEND, "built without the Vulkan backend"},
            {R_BACKEND_WEBGL, R_ERROR_BACKEND, "built without the WebGL backend"},
            {R_BACKEND_CUSTOM, R_ERROR_BACKEND, "needs Rbackend_register first"},
            {static_cast<RBackend>(7), R_ERROR_INVALID_ENUM, "unknown backend"},
        };
        for (const Case& item : cases) {
            INFO("backend " << static_cast<int>(item.backend));
            errors.clear();
            RInitDesc desc = lpe_test::headlessInitDesc();
            desc.backend = item.backend;
            CHECK(Rinit_ex(&desc) == R_FALSE);
            CHECK(Ris_initialized() == R_FALSE);
            CHECK(errors.contains(item.error, item.detail));
            CHECK(lpe_test::ravenAllocationCount() == 0);
        }
    }

    SECTION("a second Rinit while running")
    {
        RavenSession session;
        REQUIRE(session.started());
        errors.clear();
        const RInitDesc desc = lpe_test::headlessInitDesc();
        CHECK(Rinit_ex(&desc) == R_FALSE);
        CHECK(errors.contains(R_ERROR_INVALID_OPERATION, "already initialized"));
        // The running library is untouched and still works.
        CHECK(Ris_initialized() == R_TRUE);
        CHECK(lpe_test::createHostWindow(kFormXml, 320, 200) != R_INVALID_ID);
    }

    // After every failure a normal start works, and its shutdown leaves nothing.
    {
        RavenSession session;
        CHECK(session.started());
    }
    CHECK(lpe_test::ravenAllocationCount() == 0);
    CHECK(Rmemory_check() == R_TRUE);
}

TEST_CASE("Raven refuses broken XML and keeps running", "[ui][raven]")
{
    {
        RavenSession session;
        REQUIRE(session.started());
        RavenErrors& errors = RavenErrors::instance();

        constexpr std::string_view unclosed = "<Window><VBox><Label Text=\"a\"></VBox></Window>";
        CHECK(Rwindow_create_from_xml(unclosed.data(), unclosed.size()) == R_INVALID_ID);
        CHECK(errors.contains(R_ERROR_XML, "line 1"));

        errors.clear();
        constexpr std::string_view unknown = "<Window><NoSuchWidget/></Window>";
        CHECK(Rwindow_create_from_xml(unknown.data(), unknown.size()) == R_INVALID_ID);
        CHECK(errors.contains(R_ERROR_XML, "unknown widget <NoSuchWidget>"));

        errors.clear();
        constexpr std::string_view notWindow = "<VBox/>";
        CHECK(Rwindow_create_from_xml(notWindow.data(), notWindow.size()) == R_INVALID_ID);
        CHECK(errors.contains(R_ERROR_XML, "root element must be <Window>"));

        errors.clear();
        CHECK(Rwindow_create_from_xml(nullptr, 0) == R_INVALID_ID);
        CHECK(errors.contains(R_ERROR_INVALID_VALUE, "NULL text"));

        // A good document afterwards builds as usual.
        errors.clear();
        const Rid window = lpe_test::createHostWindow(kFormXml, 320, 200);
        CHECK(window != R_INVALID_ID);
        CHECK(Rwidget_find(window, "name") != R_INVALID_ID);
        INFO(errors.describe());
        CHECK(errors.count() == 0);
    }
    CHECK(lpe_test::ravenAllocationCount() == 0);
}

TEST_CASE("Raven starts and stops again and again", "[ui][raven]")
{
    // One Raven at a time (its state is per process), but any number in sequence.
    constexpr int kCycles = 5;
    for (int cycle = 0; cycle < kCycles; ++cycle) {
        INFO("cycle " << cycle);
        {
            RavenSession session;
            REQUIRE(session.started());
            const Rid window = lpe_test::createHostWindow(kFormXml, 320, 200);
            REQUIRE(window != R_INVALID_ID);
            const Rid input = Rwidget_find(window, "name");
            REQUIRE(input != R_INVALID_ID);
            lpe_test::clickWidget(window, input);
            Rhost_inject_text(window, static_cast<std::uint32_t>('a' + cycle));
            const std::string expected(1, static_cast<char>('a' + cycle));
            CHECK(std::string_view{Rtext_input_get_text(input)} == expected);
            Rdraw_frame();
            INFO(RavenErrors::instance().describe());
            CHECK(RavenErrors::instance().count() == 0);
        }
        CHECK(Ris_initialized() == R_FALSE);
        CHECK(lpe_test::ravenAllocationCount() == 0);
    }
    CHECK(Rmemory_check() == R_TRUE);
}
