#pragma once

// Headless Raven for the unit tests (M5.1, ADR 0026).
//
// Raven runs with its null backend (R_BACKEND_NULL: draws nothing, needs no
// GPU) and its host platform (raven_host.h: Raven opens no OS window and reads
// no OS event queue; the test gives it the window size, the focus and every
// input through Rhost_* calls). No SDL, no OpenGL, no display.
//
// Raven's state is per process: one Rinit at a time, and its error callback
// has no user data. A test therefore holds at most one RavenSession, and the
// error record is one per process (Raven may report from its own threads).

#include <Raven.h>

#include <atomic>
#include <cstddef>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lpe_test {

struct RavenError {
    RError code = R_NO_ERROR;
    RSeverity severity = R_SEVERITY_WEAK_WARNING;
    std::string detail;
};

// What Raven reported through Rerror_set_callback since the last clear().
class RavenErrors {
public:
    static RavenErrors& instance()
    {
        static RavenErrors errors;
        return errors;
    }

    // The function given to Rerror_set_callback.
    static void record(RError code, RSeverity severity, const char* detail)
    {
        instance().add(code, severity, detail != nullptr ? detail : "");
    }

    void clear()
    {
        const std::scoped_lock lock(mutex_);
        entries_.clear();
    }

    [[nodiscard]] std::size_t count() const
    {
        const std::scoped_lock lock(mutex_);
        return entries_.size();
    }

    // Whether an error with this code and a detail containing `detailPart` was reported.
    [[nodiscard]] bool contains(RError code, std::string_view detailPart) const
    {
        const std::scoped_lock lock(mutex_);
        for (const RavenError& entry : entries_) {
            if (entry.code == code && entry.detail.find(detailPart) != std::string::npos) {
                return true;
            }
        }
        return false;
    }

    // The details, one per line, for failure messages.
    [[nodiscard]] std::string describe() const
    {
        const std::scoped_lock lock(mutex_);
        std::string text;
        for (const RavenError& entry : entries_) {
            text += entry.detail;
            text += '\n';
        }
        return text;
    }

private:
    RavenErrors() = default;

    void add(RError code, RSeverity severity, std::string detail)
    {
        const std::scoped_lock lock(mutex_);
        entries_.push_back(RavenError{code, severity, std::move(detail)});
    }

    mutable std::mutex mutex_;
    std::vector<RavenError> entries_;
};

// What Raven asked of the test's host platform.
struct RavenHostState {
    std::string clipboard;
    std::atomic<int> wakes{0}; // Raven may wake the host from any thread.
    int cursorRequests = 0;
};

// The settings every test session starts Raven with: the null backend on the
// host platform, no render thread (Raven renders on the calling thread, as
// ADR 0019 runs it) and no worker threads, so background jobs run inline and
// every result is there when the call returns.
inline RInitDesc headlessInitDesc()
{
    RInitDesc desc{};
    desc.backend = R_BACKEND_NULL;
    desc.app_name = "LowPolyEngine unit tests";
    desc.worker_threads = -1;
    desc.disable_render_thread = R_TRUE;
    return desc;
}

// One running Raven: Rinit_ex in the constructor, Rshutdown in stop() or the
// destructor. The host platform is registered for this session only.
class RavenSession {
public:
    RavenSession()
    {
        RavenErrors::instance().clear();
        Rerror_set_callback(&RavenErrors::record);

        RHostPlatformDesc host{};
        host.set_cursor = &RavenSession::setCursor;
        host.clipboard_set = &RavenSession::setClipboard;
        host.clipboard_get = &RavenSession::clipboard;
        host.wake = &RavenSession::wake;
        host.user_data = &host_;
        Rhost_platform_register(&host);

        const RInitDesc desc = headlessInitDesc();
        started_ = Rinit_ex(&desc) == R_TRUE;
        if (started_) {
            // The tests read a widget's state right after an input: no animation in between.
            Rset_animations_enabled(R_FALSE);
            // Before the first document: nothing is taken from the current folder or
            // PATH, and no file is looked at again (ADR 0026).
            Rxml_set_auto_load(R_FALSE);
            Rxml_set_live_reload(R_FALSE);
        }
    }

    ~RavenSession() { stop(); }

    RavenSession(const RavenSession&) = delete;
    RavenSession& operator=(const RavenSession&) = delete;
    RavenSession(RavenSession&&) = delete;
    RavenSession& operator=(RavenSession&&) = delete;

    [[nodiscard]] bool started() const { return started_; }
    [[nodiscard]] RavenHostState& host() { return host_; }

    // Rshutdown, then the default platform again. Safe to call twice.
    void stop()
    {
        if (started_) {
            Rshutdown();
            started_ = false;
        }
        Rhost_platform_register(nullptr);
    }

private:
    static RavenHostState& state(void* userData) { return *static_cast<RavenHostState*>(userData); }

    static void setCursor(Rid /*window*/, RCursor /*cursor*/, void* userData)
    {
        ++state(userData).cursorRequests;
    }
    static void setClipboard(const char* text, void* userData)
    {
        state(userData).clipboard = text != nullptr ? text : "";
    }
    static const char* clipboard(void* userData) { return state(userData).clipboard.c_str(); }
    static void wake(void* userData) { ++state(userData).wakes; }

    RavenHostState host_;
    bool started_ = false;
};

// Blocks Raven still holds; 0 after Rshutdown, anything else is a leak.
inline std::size_t ravenAllocationCount()
{
    RMemoryStats stats{};
    Rmemory_get_stats(&stats);
    return stats.allocation_count;
}

// A window built from XML text, shown, sized in pixels at scale 1 (one pixel
// per logical unit) and given the keyboard, as a host does for its surfaces.
inline Rid createHostWindow(std::string_view xml, int width, int height)
{
    const Rid window = Rwindow_create_from_xml(xml.data(), xml.size());
    if (window == R_INVALID_ID) {
        return window;
    }
    Rwindow_show(window);
    Rhost_window_set_size(window, width, height, 1.0f);
    Rhost_inject_focus(window, R_TRUE);
    Rdraw_frame();
    return window;
}

// A left click in the middle of a widget, in pixels of its host window (scale 1).
inline void clickWidget(Rid window, Rid widget)
{
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    Rwidget_get_rect(widget, &x, &y, &width, &height);
    const int px = static_cast<int>(x + width * 0.5f);
    const int py = static_cast<int>(y + height * 0.5f);
    Rhost_inject_mouse_move(window, px, py, 0);
    Rhost_inject_mouse_button(window, px, py, R_MOUSE_LEFT, R_TRUE, 0);
    Rhost_inject_mouse_button(window, px, py, R_MOUSE_LEFT, R_FALSE, 0);
}

// A key pressed and released, as a host forwards it.
inline void pressKey(Rid window, RKey key)
{
    Rhost_inject_key(window, key, R_TRUE, 0, R_FALSE);
    Rhost_inject_key(window, key, R_FALSE, 0, R_FALSE);
}

} // namespace lpe_test
