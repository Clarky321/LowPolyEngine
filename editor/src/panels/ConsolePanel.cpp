#include "ConsolePanel.hpp"

#include <algorithm>
#include <climits>
#include <exception>
#include <format>
#include <string>
#include <string_view>

namespace editor {

using engine::core::LogLevel;

namespace {

constexpr const char* kListName = "console-lines";

struct Filter {
    LogLevel level;
    const char* widget;
    std::string_view label;
};
// In the order of LogLevel, as ConsolePanel::filters_.
constexpr std::array<Filter, 4> kFilters{{
    {LogLevel::Debug, "console-debug", "Debug"},
    {LogLevel::Info, "console-info", "Info"},
    {LogLevel::Warning, "console-warnings", "Warnings"},
    {LogLevel::Error, "console-errors", "Errors"},
}};

} // namespace

std::optional<ConsoleColor> consoleColor(LogLevel level) noexcept
{
    switch (level) {
    case LogLevel::Error:
        return ConsoleColor{0.95F, 0.42F, 0.38F, 1.0F};
    case LogLevel::Warning:
        return ConsoleColor{0.95F, 0.76F, 0.32F, 1.0F};
    case LogLevel::Debug:
        return ConsoleColor{0.58F, 0.60F, 0.64F, 1.0F};
    case LogLevel::Info:
        return std::nullopt;
    }
    return std::nullopt;
}

std::optional<engine::core::Error> ConsolePanel::attach(Rid window, ConsoleLog& log)
{
    detach();
    const Rid listId = Rwidget_find(window, kListName);
    if (listId == R_INVALID_ID) {
        return engine::core::Error{std::string("the shell layout has no widget named '") +
                                   kListName + "'"};
    }
    std::array<Rid, 4> filterIds{};
    for (std::size_t i = 0; i < kFilters.size(); ++i) {
        filterIds[i] = Rwidget_find(window, kFilters[i].widget);
        if (filterIds[i] == R_INVALID_ID) {
            return engine::core::Error{std::string("the shell layout has no widget named '") +
                                       kFilters[i].widget + "'"};
        }
    }
    log_ = &log;
    list_ = listId;
    filters_ = filterIds;
    for (std::size_t i = 0; i < kFilters.size(); ++i) {
        Rcheckbox_set_checked(filters_[i], log.shows(kFilters[i].level) ? R_TRUE : R_FALSE);
        Rcheckbox_set_changed_callback(filters_[i], &ConsolePanel::onFilter, this);
    }
    Rlist_view_set_item_color_callback(list_, &ConsolePanel::onColor, this);
    shown_ = false;
    rows_ = 0;
    update();
    return std::nullopt;
}

void ConsolePanel::detach() noexcept
{
    if (list_ == R_INVALID_ID) {
        return;
    }
    if (Ris_initialized() != R_FALSE) {
        if (Rwidget_is_valid(list_) != R_FALSE) {
            Rlist_view_set_virtual_items(list_, 0, nullptr, nullptr);
            Rlist_view_set_item_color_callback(list_, nullptr, nullptr);
        }
        for (const Rid filter : filters_) {
            if (Rwidget_is_valid(filter) != R_FALSE) {
                Rcheckbox_set_changed_callback(filter, nullptr, nullptr);
            }
        }
    }
    list_ = R_INVALID_ID;
    filters_.fill(R_INVALID_ID);
    log_ = nullptr;
    rows_ = 0;
}

void ConsolePanel::update()
{
    if (!attached() || log_ == nullptr) {
        return;
    }
    if (shown_ && log_->version() == version_) {
        return;
    }
    rows_ = log_->shown().size();
    const int count = static_cast<int>(std::min<std::size_t>(rows_, INT_MAX));
    // Keeps the selection and the scroll position of the live list.
    Rlist_view_set_virtual_items(list_, count, &ConsolePanel::onText, this);
    for (std::size_t i = 0; i < kFilters.size(); ++i) {
        const std::string text =
            std::format("{} ({})", kFilters[i].label, log_->count(kFilters[i].level));
        Rcheckbox_set_text(filters_[i], text.c_str());
    }
    if (count > 0 && Rlist_view_get_selected(list_) < 0) {
        Rlist_view_scroll_to_item(list_, count - 1);
    }
    version_ = log_->version();
    shown_ = true;
}

Rid ConsolePanel::filter(LogLevel level) const noexcept
{
    for (std::size_t i = 0; i < kFilters.size(); ++i) {
        if (kFilters[i].level == level) {
            return filters_[i];
        }
    }
    return R_INVALID_ID;
}

const char* ConsolePanel::rowText(std::size_t row) const noexcept
{
    const ConsoleLine* shown = line(row);
    return shown != nullptr ? shown->text.c_str() : "";
}

const ConsoleLine* ConsolePanel::line(std::size_t row) const noexcept
{
    if (log_ == nullptr || row >= rows_ || row >= log_->shown().size()) {
        return nullptr;
    }
    const std::size_t index = log_->shown()[row];
    return index < log_->lines().size() ? &log_->lines()[index] : nullptr;
}

const char* ConsolePanel::onText(Rid /*list*/, int index, void* user) noexcept
{
    if (user == nullptr || index < 0) {
        return "";
    }
    // Valid until the console's lines change, which happens in update() only,
    // never while Raven draws.
    return static_cast<const ConsolePanel*>(user)->rowText(static_cast<std::size_t>(index));
}

int ConsolePanel::onColor(Rid /*list*/, int index, float* r, float* g, float* b, float* a,
                          void* user) noexcept
{
    if (user == nullptr || index < 0 || r == nullptr || g == nullptr || b == nullptr ||
        a == nullptr) {
        return R_FALSE;
    }
    const ConsoleLine* shown =
        static_cast<const ConsolePanel*>(user)->line(static_cast<std::size_t>(index));
    if (shown == nullptr) {
        return R_FALSE;
    }
    const std::optional<ConsoleColor> color = consoleColor(shown->level);
    if (!color) {
        return R_FALSE;
    }
    *r = color->r;
    *g = color->g;
    *b = color->b;
    *a = color->a;
    return R_TRUE;
}

void ConsolePanel::onFilter(Rid checkbox, int checked, void* user) noexcept
{
    if (user == nullptr) {
        return;
    }
    try {
        static_cast<ConsolePanel*>(user)->setShown(checkbox, checked != R_FALSE);
    } catch (const std::exception& error) {
        engine::core::logError("editor", "console: filtering failed: {}", error.what());
    } catch (...) {
        engine::core::logError("editor", "console: filtering failed");
    }
}

void ConsolePanel::setShown(Rid checkbox, bool shown)
{
    if (log_ == nullptr) {
        return;
    }
    for (std::size_t i = 0; i < kFilters.size(); ++i) {
        if (filters_[i] == checkbox) {
            log_->show(kFilters[i].level, shown);
            // The selected row's index means another line now: follow the newest again.
            Rlist_view_set_selected(list_, -1);
            return;
        }
    }
}

} // namespace editor
