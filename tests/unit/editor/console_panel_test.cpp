// The console panel (editor/src/panels/ConsolePanel, M5.7), headless: Raven's
// null backend and host platform with the editor's shell layout. The log's
// lines reach the list in the frame they were logged in, the level
// checkboxes filter them and count them, and each level has its color.

#include "../ui/RavenSession.hpp"

#include "application/ShellLayout.hpp"
#include "editor/panels/ConsoleLog.hpp"
#include "panels/ConsolePanel.hpp"

#include "engine/core/Log.hpp"

#include <catch2/catch_test_macros.hpp>

#include <Raven.h>

#include <cmath>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace {

using editor::ConsoleLine;
using editor::ConsolePanel;
using engine::core::LogLevel;
using lpe_test::RavenErrors;
using lpe_test::RavenSession;

class Fixture {
public:
    Fixture()
    {
        REQUIRE(session.started());
        const std::string_view xml = editor::layouts::shell();
        window = Rwindow_create_from_xml(xml.data(), xml.size());
        REQUIRE(window != R_INVALID_ID);
        Rwindow_show(window);
        Rhost_window_set_size(window, 1280, 760, 1.0F);
        Rhost_inject_focus(window, R_TRUE);
        Rdraw_frame();
        inbox.listen();
        const auto error = panel.attach(window, log);
        INFO((error ? error->message : std::string()));
        REQUIRE_FALSE(error.has_value());
    }

    ~Fixture()
    {
        panel.detach();
        session.stop();
    }

    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;
    Fixture(Fixture&&) = delete;
    Fixture& operator=(Fixture&&) = delete;

    // One frame of the editor's console: the lines logged so far reach it.
    void frame()
    {
        log.append(inbox.take());
        panel.update();
        Rdraw_frame();
    }

    void clickWidget(Rid widget) const
    {
        float x = 0.0F;
        float y = 0.0F;
        float width = 0.0F;
        float height = 0.0F;
        Rwidget_get_rect(widget, &x, &y, &width, &height);
        const int px = static_cast<int>(x + width * 0.5F);
        const int py = static_cast<int>(y + height * 0.5F);
        Rhost_inject_mouse_move(window, px, py, 0);
        Rhost_inject_mouse_button(window, px, py, R_MOUSE_LEFT, R_TRUE, 0);
        Rhost_inject_mouse_button(window, px, py, R_MOUSE_LEFT, R_FALSE, 0);
    }

    // The rows the list shows, as it draws them.
    [[nodiscard]] std::vector<std::string> rows() const
    {
        const int count = Rlist_view_get_item_count(panel.list());
        std::vector<std::string> texts;
        for (int i = 0; i < count; ++i) {
            texts.emplace_back(panel.rowText(static_cast<std::size_t>(i)));
        }
        return texts;
    }

    [[nodiscard]] std::string checkboxText(LogLevel level) const
    {
        const char* text = Rcheckbox_get_text(panel.filter(level));
        return text != nullptr ? text : "";
    }

    RavenSession session;
    Rid window = R_INVALID_ID;
    editor::LogInbox inbox;
    editor::ConsoleLog log;
    ConsolePanel panel;
};

} // namespace

TEST_CASE("The console lists the log's lines in the frame they were logged in",
          "[editor][console][ui]")
{
    Fixture f;
    f.frame();
    CHECK(Rlist_view_get_item_count(f.panel.list()) == 0);
    CHECK(f.checkboxText(LogLevel::Error) == "Errors (0)");

    engine::core::logInfo("console-test", "opened {}", "a project");
    engine::core::logWarning("console-test", "project: an unknown field");
    engine::core::logError("console-test", "something failed");
    f.frame();
    CHECK(f.rows() == std::vector<std::string>{"[info] [console-test] opened a project",
                                               "[warning] [console-test] project: an unknown field",
                                               "[error] [console-test] something failed"});
    CHECK(f.panel.rowCount() == 3);
    CHECK(f.checkboxText(LogLevel::Error) == "Errors (1)");
    CHECK(f.checkboxText(LogLevel::Warning) == "Warnings (1)");
    CHECK(f.checkboxText(LogLevel::Info) == "Info (1)");
    CHECK(f.checkboxText(LogLevel::Debug) == "Debug (0)");
    // Beyond the rows: nothing.
    CHECK(std::string(f.panel.rowText(3)).empty());

    // Lines logged while Raven draws arrive with the next frame.
    engine::core::logInfo("console-test", "later");
    CHECK(f.panel.rowCount() == 3);
    f.frame();
    CHECK(f.panel.rowCount() == 4);
    CHECK(f.rows().back() == "[info] [console-test] later");
    CHECK(RavenErrors::instance().count() == 0);
}

TEST_CASE("The console's checkboxes show and hide each level's lines", "[editor][console][ui]")
{
    Fixture f;
    f.log.append({ConsoleLine{LogLevel::Info, "i"}, ConsoleLine{LogLevel::Warning, "w"},
                  ConsoleLine{LogLevel::Error, "e"}, ConsoleLine{LogLevel::Debug, "d"}});
    f.frame();
    CHECK(f.rows() == std::vector<std::string>{"i", "w", "e", "d"});

    for (const LogLevel level :
         {LogLevel::Debug, LogLevel::Info, LogLevel::Warning, LogLevel::Error}) {
        CHECK(Rcheckbox_is_checked(f.panel.filter(level)) != R_FALSE);
    }
    // A selected line stops the list from following; a filter change clears it.
    Rlist_view_set_selected(f.panel.list(), 1);
    f.clickWidget(f.panel.filter(LogLevel::Warning));
    f.frame();
    CHECK(Rcheckbox_is_checked(f.panel.filter(LogLevel::Warning)) == R_FALSE);
    CHECK_FALSE(f.log.shows(LogLevel::Warning));
    CHECK(f.rows() == std::vector<std::string>{"i", "e", "d"});
    CHECK(Rlist_view_get_selected(f.panel.list()) == -1);
    // The count still includes the hidden lines.
    CHECK(f.checkboxText(LogLevel::Warning) == "Warnings (1)");

    f.clickWidget(f.panel.filter(LogLevel::Info));
    f.clickWidget(f.panel.filter(LogLevel::Debug));
    f.frame();
    CHECK(f.rows() == std::vector<std::string>{"e"});

    f.clickWidget(f.panel.filter(LogLevel::Warning));
    f.frame();
    CHECK(f.rows() == std::vector<std::string>{"w", "e"});
    CHECK(RavenErrors::instance().count() == 0);
}

TEST_CASE("Each level has its color; info lines keep the theme's", "[editor][console]")
{
    const auto error = editor::consoleColor(LogLevel::Error);
    const auto warning = editor::consoleColor(LogLevel::Warning);
    const auto debug = editor::consoleColor(LogLevel::Debug);
    REQUIRE(error.has_value());
    REQUIRE(warning.has_value());
    REQUIRE(debug.has_value());
    CHECK_FALSE(editor::consoleColor(LogLevel::Info).has_value());
    // Red, amber and grey, opaque.
    CHECK(error->r > error->g + 0.3F);
    CHECK(warning->r > warning->b + 0.3F);
    CHECK(warning->g > warning->b + 0.3F);
    CHECK(std::abs(debug->r - debug->b) < 0.1F);
    CHECK(error->a == 1.0F);
    CHECK(warning->a == 1.0F);
    CHECK(debug->a == 1.0F);
}

TEST_CASE("The console needs its widgets in the layout", "[editor][console][ui]")
{
    RavenSession session;
    REQUIRE(session.started());
    constexpr std::string_view kNoConsole =
        R"(<Window Title="no console" Width="320" Height="200"><VBox Name="console"/></Window>)";
    const Rid window = lpe_test::createHostWindow(kNoConsole, 320, 200);
    REQUIRE(window != R_INVALID_ID);
    editor::ConsoleLog log;
    ConsolePanel panel;
    const auto error = panel.attach(window, log);
    REQUIRE(error.has_value());
    CHECK(error->message == "the shell layout has no widget named 'console-lines'");
    CHECK_FALSE(panel.attached());
    session.stop();
}
