#pragma once

// The console panel (M5.7): the console's lines (ConsoleLog) in Raven's
// ListView, in virtual mode (the list asks for the text of the rows it
// draws), each row in its level's color: errors red, warnings amber, debug
// lines dimmed, the rest in the theme's text color. One checkbox per level
// shows or hides its lines and tells how many there are. The list follows the
// newest line unless the user selected one; changing the filter clears the
// selection, so it follows again.
//
// update(), once per frame outside Raven's frame, shows what changed since
// the previous one. The widgets are in the shell's layout; Raven holds a
// pointer to the panel while it is attached.

#include "editor/panels/ConsoleLog.hpp"

#include "engine/core/Log.hpp"
#include "engine/core/Result.hpp"

#include <Raven.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace editor {

// The color a console row of the level is drawn in (red, green, blue, alpha),
// or empty for the theme's text color.
struct ConsoleColor {
    float r = 0.0F;
    float g = 0.0F;
    float b = 0.0F;
    float a = 1.0F;
};
[[nodiscard]] std::optional<ConsoleColor> consoleColor(engine::core::LogLevel level) noexcept;

class ConsolePanel {
public:
    ConsolePanel() = default;
    ConsolePanel(const ConsolePanel&) = delete;
    ConsolePanel& operator=(const ConsolePanel&) = delete;
    ConsolePanel(ConsolePanel&&) = delete;
    ConsolePanel& operator=(ConsolePanel&&) = delete;
    ~ConsolePanel() { detach(); }

    // Takes the console's widgets of `window` (the shell's layout) and shows
    // `log`, which must outlive the attachment. Fails, attaching nothing, when
    // a widget is missing.
    [[nodiscard]] std::optional<engine::core::Error> attach(Rid window, ConsoleLog& log);
    void detach() noexcept;
    [[nodiscard]] bool attached() const noexcept { return list_ != R_INVALID_ID; }

    // Shows the log's lines and counts when they changed.
    void update();

    [[nodiscard]] Rid list() const noexcept { return list_; }
    // The checkbox of a level's lines.
    [[nodiscard]] Rid filter(engine::core::LogLevel level) const noexcept;
    // The rows the list was last given.
    [[nodiscard]] std::size_t rowCount() const noexcept { return rows_; }
    // The text of one of those rows ("" beyond them), as the list draws it.
    [[nodiscard]] const char* rowText(std::size_t row) const noexcept;

private:
    static const char* onText(Rid list, int index, void* user) noexcept;
    static int onColor(Rid list, int index, float* r, float* g, float* b, float* a,
                       void* user) noexcept;
    static void onFilter(Rid checkbox, int checked, void* user) noexcept;

    [[nodiscard]] const ConsoleLine* line(std::size_t row) const noexcept;
    void setShown(Rid checkbox, bool shown);

    ConsoleLog* log_ = nullptr;
    Rid list_ = R_INVALID_ID;
    // In the order of engine::core::LogLevel: debug, info, warning, error.
    std::array<Rid, 4> filters_{R_INVALID_ID, R_INVALID_ID, R_INVALID_ID, R_INVALID_ID};
    std::uint64_t version_ = 0;
    bool shown_ = false;
    std::size_t rows_ = 0;
};

} // namespace editor
