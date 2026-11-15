// The editor shell's layout (editor/layouts/shell.xml, M5.2) laid out by Raven
// headless (null backend, host platform): the five areas exist, have a size,
// sit where the shell puts them and stay inside the window at 100 %, 150 % and
// 200 %, also when the scale changes while the window is open.

#include "RavenSession.hpp"

#include "application/EditorShell.hpp"
#include "application/ShellLayout.hpp"

#include <catch2/catch_test_macros.hpp>

#include <Raven.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <string>

namespace {

using lpe_test::RavenErrors;
using lpe_test::RavenSession;

// An area in the window's pixels.
struct Area {
    float x = 0.0F;
    float y = 0.0F;
    float width = 0.0F;
    float height = 0.0F;

    [[nodiscard]] float right() const { return x + width; }
    [[nodiscard]] float bottom() const { return y + height; }
};

constexpr std::size_t kToolbar = 0;
constexpr std::size_t kViewport = 1;
constexpr std::size_t kHierarchy = 2;
constexpr std::size_t kInspector = 3;
constexpr std::size_t kConsole = 4;
static_assert(editor::kShellAreas[kToolbar] == "toolbar");
static_assert(editor::kShellAreas[kConsole] == "console");

// The shell in a window of `width` x `height` pixels at `scale`.
Rid openShell(int width, int height, float scale)
{
    const std::string_view xml = editor::layouts::shell();
    const Rid window = Rwindow_create_from_xml(xml.data(), xml.size());
    if (window == R_INVALID_ID) {
        return window;
    }
    Rwindow_show(window);
    Rhost_window_set_size(window, width, height, scale);
    Rhost_inject_focus(window, R_TRUE);
    Rdraw_frame();
    return window;
}

std::array<Area, editor::kShellAreas.size()> areasOf(Rid window)
{
    const float scale = Rwindow_get_scale(window);
    std::array<Area, editor::kShellAreas.size()> areas{};
    for (std::size_t i = 0; i < editor::kShellAreas.size(); ++i) {
        const std::string name(editor::kShellAreas[i]);
        const Rid widget = Rwidget_find(window, name.c_str());
        REQUIRE(widget != R_INVALID_ID);
        Area& area = areas[i];
        Rwidget_get_rect(widget, &area.x, &area.y, &area.width, &area.height);
        area.x *= scale;
        area.y *= scale;
        area.width *= scale;
        area.height *= scale;
    }
    return areas;
}

bool overlap(const Area& a, const Area& b)
{
    return a.x < b.right() && b.x < a.right() && a.y < b.bottom() && b.y < a.bottom();
}

// Every area has a size and lies inside the window; the docked panels are
// where the shell puts them and do not overlap.
void checkShell(Rid window, int width, int height)
{
    const auto areas = areasOf(window);
    constexpr float kTolerance = 1.0F;
    for (std::size_t i = 0; i < areas.size(); ++i) {
        INFO("area " << editor::kShellAreas[i]);
        CHECK(areas[i].width > 1.0F);
        CHECK(areas[i].height > 1.0F);
        CHECK(areas[i].x >= -kTolerance);
        CHECK(areas[i].y >= -kTolerance);
        CHECK(areas[i].right() <= static_cast<float>(width) + kTolerance);
        CHECK(areas[i].bottom() <= static_cast<float>(height) + kTolerance);
    }
    const Area& toolbar = areas[kToolbar];
    const Area& viewport = areas[kViewport];
    const Area& hierarchy = areas[kHierarchy];
    const Area& inspector = areas[kInspector];
    const Area& console = areas[kConsole];
    // The toolbar above the panels, the hierarchy left, the inspector right,
    // the console below the viewport.
    CHECK(toolbar.bottom() <= hierarchy.y + kTolerance);
    CHECK(hierarchy.right() <= viewport.x + kTolerance);
    CHECK(viewport.right() <= inspector.x + kTolerance);
    CHECK(viewport.bottom() <= console.y + kTolerance);
    CHECK(std::abs(console.x - viewport.x) <= kTolerance);
    const std::array<std::size_t, 4> docked{kViewport, kHierarchy, kInspector, kConsole};
    for (std::size_t a = 0; a < docked.size(); ++a) {
        for (std::size_t b = a + 1; b < docked.size(); ++b) {
            INFO(editor::kShellAreas[docked[a]] << " and " << editor::kShellAreas[docked[b]]);
            CHECK_FALSE(overlap(areas[docked[a]], areas[docked[b]]));
        }
    }
}

} // namespace

TEST_CASE("The editor shell's five areas fit the window at 100, 150 and 200 percent",
          "[ui][raven][editor]")
{
    struct Case {
        int width;
        int height;
        float scale;
    };
    // The default window, the same at 150 % (853x507 logical units), at 200 %,
    // and a small window at 150 %.
    const std::array<Case, 4> cases{{
        {1280, 760, 1.0F},
        {1280, 760, 1.5F},
        {1280, 760, 2.0F},
        {800, 500, 1.5F},
    }};
    for (const Case& item : cases) {
        INFO(item.width << "x" << item.height << " at scale " << item.scale);
        {
            RavenSession session;
            REQUIRE(session.started());
            const Rid window = openShell(item.width, item.height, item.scale);
            REQUIRE(window != R_INVALID_ID);
            CHECK(Rwindow_get_scale(window) == item.scale);
            checkShell(window, item.width, item.height);
            INFO(RavenErrors::instance().describe());
            CHECK(RavenErrors::instance().count() == 0);
        }
        CHECK(lpe_test::ravenAllocationCount() == 0);
    }
}

TEST_CASE("The editor shell follows a scale change while it is open", "[ui][raven][editor]")
{
    {
        RavenSession session;
        REQUIRE(session.started());
        const Rid window = openShell(1280, 760, 1.0F);
        REQUIRE(window != R_INVALID_ID);
        const auto before = areasOf(window);
        checkShell(window, 1280, 760);

        // 100 % to 150 %, as when the display's scale is changed: same pixels.
        Rhost_window_set_size(window, 1280, 760, 1.5F);
        Rdraw_frame();
        CHECK(Rwindow_get_scale(window) == 1.5F);
        checkShell(window, 1280, 760);
        const auto after = areasOf(window);
        // The toolbar's height is in logical units: it grows with the scale.
        CHECK(after[kToolbar].height > before[kToolbar].height * 1.4F);
        // The docked panels keep their share of the window.
        CHECK(std::abs(after[kHierarchy].width - before[kHierarchy].width) <
              before[kHierarchy].width * 0.2F);

        // And back.
        Rhost_window_set_size(window, 1280, 760, 1.0F);
        Rdraw_frame();
        checkShell(window, 1280, 760);
        INFO(RavenErrors::instance().describe());
        CHECK(RavenErrors::instance().count() == 0);
    }
    CHECK(lpe_test::ravenAllocationCount() == 0);
}
